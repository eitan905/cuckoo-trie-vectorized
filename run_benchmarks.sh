#!/bin/bash

RESULTS_FILE="benchmark_results.txt"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

echo "=== Cuckoo Trie Comprehensive Benchmarks ===" | tee $RESULTS_FILE
echo "Timestamp: $TIMESTAMP" | tee -a $RESULTS_FILE
echo "Dataset: rand-8 (8-byte random keys)" | tee -a $RESULTS_FILE
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

declare -A results

for bench in "${benchmarks[@]}"; do
    echo "Running: $bench" | tee -a $RESULTS_FILE
    output=$(./benchmark $bench rand-8 2>&1)
    echo "$output" | tee -a $RESULTS_FILE
    
    # Extract ops and ms from RESULT line
    result_line=$(echo "$output" | grep "RESULT:")
    if [[ $result_line =~ ops=([0-9]+).*ms=([0-9]+) ]]; then
        ops=${BASH_REMATCH[1]}
        ms=${BASH_REMATCH[2]}
        throughput=$(echo "scale=2; $ops * 1000 / $ms" | bc -l)
        results[$bench]="$throughput ops/sec ($ops ops in ${ms}ms)"
    fi
    echo | tee -a $RESULTS_FILE
done

echo "========================================" | tee -a $RESULTS_FILE
echo "PERFORMANCE SUMMARY" | tee -a $RESULTS_FILE
echo "========================================" | tee -a $RESULTS_FILE
printf "%-15s %s\n" "Benchmark" "Throughput" | tee -a $RESULTS_FILE
echo "----------------------------------------" | tee -a $RESULTS_FILE

for bench in "${benchmarks[@]}"; do
    if [[ -n "${results[$bench]}" ]]; then
        printf "%-15s %s\n" "$bench" "${results[$bench]}" | tee -a $RESULTS_FILE
    fi
done

echo | tee -a $RESULTS_FILE
echo "Results saved to: $RESULTS_FILE"
