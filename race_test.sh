#!/bin/bash

echo "Building race condition stress test..."

# Build the test
gcc -march=haswell -mavx2 -O3 -Wl,-rpath=. -o race_test race_test.c libcuckoo_trie.so -lpthread

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

echo "Running stress test (this will take 30 seconds per run)..."
echo "Press Ctrl+C to stop early"

run_count=0
total_errors=0

while true; do
    run_count=$((run_count + 1))
    echo ""
    echo "=== RUN $run_count ==="
    
    ./race_test
    exit_code=$?
    
    if [ $exit_code -eq 1 ]; then
        echo "*** RACE CONDITION FOUND IN RUN $run_count ***"
        total_errors=$((total_errors + 1))
    fi
    
    echo "Completed runs: $run_count, Race conditions found: $total_errors"
    
    # Optional: uncomment to stop after first error
    # if [ $exit_code -eq 1 ]; then
    #     break
    # fi
    
    sleep 1
done
