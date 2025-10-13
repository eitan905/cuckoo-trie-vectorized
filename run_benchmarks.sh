#!/bin/bash

RESULTS_FILE="vectorization_benchmark_results.txt"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')
RUNS=5
BENCHMARK_BINARY="./benchmark"

# Parse command line arguments
while getopts "i:v" opt; do
    case $opt in
        i)
            RUNS=$OPTARG
            ;;
        v)
            BENCHMARK_BINARY="./benchmark_vectorized"
            ;;
        \?)
            echo "Usage: $0 [-i iterations] [-v]"
            echo "  -i: Number of runs per test (default: 5)"
            echo "  -v: Use vectorized benchmark binary"
            exit 1
            ;;
    esac
done

echo "=== Cuckoo Trie Vectorization Benchmarks ===" | tee $RESULTS_FILE
echo "Timestamp: $TIMESTAMP" | tee -a $RESULTS_FILE
echo "Runs per test: $RUNS" | tee -a $RESULTS_FILE
echo "Benchmark binary: $BENCHMARK_BINARY" | tee -a $RESULTS_FILE

echo | tee -a $RESULTS_FILE

# Use current directory or script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Essential benchmarks for vectorization testing
benchmarks=(
    "pos-lookup"
    "mt-pos-lookup --threads 4"
    "mt-pos-lookup --threads 8"
    "ycsb-a"
    "mt-ycsb-a --threads 8"
    "ycsb-f"
    "mt-ycsb-f --threads 8"
    "insert"
    )

key_sizes=("rand-8" "rand-64" "rand-256")

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
            output=$(eval "$BENCHMARK_BINARY $bench $key_size" 2>&1)
            
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
            avg_throughput=$(echo "scale=3; $total_throughput / $valid_runs" | bc -l)
            echo "  Average: ${avg_throughput} Mops/s"
            results["${bench}_${key_size}"]="$avg_throughput"
        else
            echo "  Average: N/A"
            results["${bench}_${key_size}"]="N/A"
        fi
        echo
        sleep 1
    done
done

echo "========================================" | tee -a $RESULTS_FILE
echo "AVERAGE PERFORMANCE SUMMARY (Mops/s)" | tee -a $RESULTS_FILE
echo "Average of $RUNS runs per test" | tee -a $RESULTS_FILE
echo "========================================" | tee -a $RESULTS_FILE
printf "%-25s %-12s %-12s %-12s\n" "Benchmark" "rand-8" "rand-64" "rand-256" | tee -a $RESULTS_FILE
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
