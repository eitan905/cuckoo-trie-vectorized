#!/bin/bash

echo "Cuckoo Trie Vectorization Performance Comparison"
echo "==============================================="

# Function to run benchmark multiple times and get average
run_benchmark() {
    local name=$1
    local runs=5
    echo "Running $name benchmark ($runs runs)..."
    
    local total_time=0
    for i in $(seq 1 $runs); do
        echo "  Run $i/$runs..."
        # Extract time from benchmark output (assuming it outputs time in a parseable format)
        local time=$(./benchmark pos-lookup rand-8 2>/dev/null | grep -E "(time|throughput|ops)" | tail -1)
        echo "    $time"
    done
}

# Build and test vectorized version
echo "Building vectorized version..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "Failed to build vectorized version!"
    exit 1
fi

echo "Testing vectorized version..."
run_benchmark "Vectorized"

# Disable vectorization and test scalar version
echo ""
echo "Building scalar version..."
sed -i 's/#define USE_VECTORIZED_SEARCH/\/\/#define USE_VECTORIZED_SEARCH/' vectorized_search.h
make clean > /dev/null 2>&1
make > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "Failed to build scalar version!"
    exit 1
fi

echo "Testing scalar version..."
run_benchmark "Scalar"

# Restore vectorized version
sed -i 's/\/\/#define USE_VECTORIZED_SEARCH/#define USE_VECTORIZED_SEARCH/' vectorized_search.h
make clean > /dev/null 2>&1
make > /dev/null 2>&1

echo ""
echo "Performance comparison complete!"
echo "The vectorized version should show improved performance for lookup operations."
