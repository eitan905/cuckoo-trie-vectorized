#!/bin/bash

RESULTS_FILE="benchmark_results.txt"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

echo "=== Cuckoo Trie Comprehensive Benchmarks ===" | tee $RESULTS_FILE
echo "Timestamp: $TIMESTAMP" | tee -a $RESULTS_FILE
echo | tee -a $RESULTS_FILE

cd /local/home/eitansha/cuckoo-trie-vectorized-main/cuckoo-trie-vectorized-main

benchmarks=(
    "insert"
    "mt-insert"
    "pos-lookup"
    "mt-pos-lookup"
    "ycsb-a"
    "mt-ycsb-a"
)

key_sizes=("rand-8" "rand-16" "rand-32" "rand-64")

declare -A results

for key_size in "${key_sizes[@]}"; do
    echo "========================================" | tee -a $RESULTS_FILE
    echo "Testing with $key_size" | tee -a $RESULTS_FILE
    echo "========================================" | tee -a $RESULTS_FILE
    
    for bench in "${benchmarks[@]}"; do
        echo "Running: $bench $key_size" | tee -a $RESULTS_FILE
        output=$(./benchmark $bench $key_size 2>&1)
        echo "$output" | tee -a $RESULTS_FILE
        
        # Extract ops and ms from RESULT line
        result_line=$(echo "$output" | grep "RESULT:")
        if [[ $result_line =~ ops=([0-9]+).*ms=([0-9]+) ]]; then
            ops=${BASH_REMATCH[1]}
            ms=${BASH_REMATCH[2]}
            throughput=$(echo "scale=2; $ops * 1000 / $ms / 1000000" | bc -l)
            results["${bench}_${key_size}"]="$throughput"
        fi
        echo | tee -a $RESULTS_FILE
    done
done

echo "========================================" | tee -a $RESULTS_FILE
echo "PERFORMANCE SUMMARY (millions ops/sec)" | tee -a $RESULTS_FILE
echo "========================================" | tee -a $RESULTS_FILE
printf "%-15s %-12s %-12s %-12s %-12s\n" "Benchmark" "rand-8" "rand-16" "rand-32" "rand-64" | tee -a $RESULTS_FILE
echo "-----------------------------------------------------------------------" | tee -a $RESULTS_FILE

for bench in "${benchmarks[@]}"; do
    printf "%-15s " "$bench" | tee -a $RESULTS_FILE
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
