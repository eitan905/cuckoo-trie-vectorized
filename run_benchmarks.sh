#!/bin/bash

RESULTS_FILE="vectorization_benchmark_results.txt"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
RUNS=5

echo "=== Cuckoo Trie Vectorization Benchmarks ===" | tee $RESULTS_FILE
echo "Timestamp: $TIMESTAMP" | tee -a $RESULTS_FILE
echo "Runs per test: $RUNS" | tee -a $RESULTS_FILE

# Check if vectorization is enabled
cat > /tmp/check_vectorization.c << 'EOF'
#include <stdio.h>
int main() {
#ifdef USE_VECTORIZED_SEARCH
    printf("ENABLED\n");
#else
    printf("DISABLED\n");
#endif
    return 0;
}
EOF

VECTORIZATION_STATUS=$(gcc -DUSE_VECTORIZED_SEARCH /tmp/check_vectorization.c -o /tmp/check_vectorization && /tmp/check_vectorization)
echo "Vectorization: $VECTORIZATION_STATUS" | tee -a $RESULTS_FILE
rm -f /tmp/check_vectorization.c /tmp/check_vectorization

echo | tee -a $RESULTS_FILE

cd /local/home/eitansha/cuckoo-trie-vectorized

# Essential benchmarks for vectorization testing
benchmarks=(
    "pos-lookup"
    "mt-pos-lookup --threads 4"
    "mt-pos-lookup --threads 8"
    "ycsb-b"
    "ycsb-c"
    "insert"
)

key_sizes=("rand-8" "rand-16" "rand-32")

declare -A results

for key_size in "${key_sizes[@]}"; do
    echo "========================================" | tee -a $RESULTS_FILE
    echo "Testing with $key_size" | tee -a $RESULTS_FILE
    echo "========================================" | tee -a $RESULTS_FILE
    
    for bench in "${benchmarks[@]}"; do
        echo "Running: $bench $key_size ($RUNS times)"
        
        total_throughput=0
        valid_runs=0
        
        for ((run=1; run<=RUNS; run++)); do
            echo -n "  Run $run/$RUNS... "
            output=$(eval "./benchmark $bench $key_size" 2>&1)
            
            # Extract ops and ms from RESULT line
            result_line=$(echo "$output" | grep "RESULT:")
            if [[ $result_line =~ ops=([0-9]+).*ms=([0-9]+) ]]; then
                ops=${BASH_REMATCH[1]}
                ms=${BASH_REMATCH[2]}
                throughput=$(echo "scale=3; $ops * 1000 / $ms / 1000000" | bc -l)
                total_throughput=$(echo "scale=3; $total_throughput + $throughput" | bc -l)
                valid_runs=$((valid_runs + 1))
                echo "${throughput} Mops/s"
            else
                echo "FAILED"
            fi
        done
        
        if [[ $valid_runs -gt 0 ]]; then
            avg_throughput=$(echo "scale=2; $total_throughput / $valid_runs" | bc -l)
            echo "  Average: ${avg_throughput} Mops/s"
            results["${bench}_${key_size}"]="$avg_throughput"
        else
            echo "  Average: N/A"
            results["${bench}_${key_size}"]="N/A"
        fi
        echo
    done
done

echo "========================================" | tee -a $RESULTS_FILE
echo "AVERAGE PERFORMANCE SUMMARY (Mops/s)" | tee -a $RESULTS_FILE
echo "Average of $RUNS runs per test" | tee -a $RESULTS_FILE
echo "========================================" | tee -a $RESULTS_FILE
printf "%-25s %-12s %-12s %-12s\n" "Benchmark" "rand-8" "rand-16" "rand-32" | tee -a $RESULTS_FILE
echo "---------------------------------------------------------------------" | tee -a $RESULTS_FILE

for bench in "${benchmarks[@]}"; do
    printf "%-25s " "$bench" | tee -a $RESULTS_FILE
    for key_size in "${key_sizes[@]}"; do
        key="${bench}_${key_size}"
        if [[ -n "${results[$key]}" ]]; then
            printf "%-12s " "${results[$key]}" | tee -a $RESULTS_FILE
        else
            printf "%-12s " "N/A" | tee -a $RESULTS_FILE
        fi
    done
    echo | tee -a $RESULTS_FILE
done

echo | tee -a $RESULTS_FILE
echo "Results saved to: $RESULTS_FILE"
