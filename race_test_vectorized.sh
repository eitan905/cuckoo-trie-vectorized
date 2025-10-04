#!/bin/bash

echo "Building race condition stress test for VECTORIZED implementation..."

# Build the test with vectorized library
gcc -march=haswell -mavx2 -O3 -Wl,-rpath=. -o race_test_vectorized race_test.c libcuckoo_trie_vectorized.so -lpthread

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

echo "Running stress test on VECTORIZED implementation..."
echo "Press Ctrl+C to stop early"

run_count=0
total_errors=0

while true; do
    run_count=$((run_count + 1))
    echo ""
    echo "=== VECTORIZED RUN $run_count ==="
    
    ./race_test_vectorized
    exit_code=$?
    
    if [ $exit_code -eq 1 ]; then
        echo "*** RACE CONDITION FOUND IN VECTORIZED RUN $run_count ***"
        total_errors=$((total_errors + 1))
    fi
    
    echo "Completed runs: $run_count, Race conditions found: $total_errors"
    
    sleep 1
done
