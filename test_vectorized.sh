#!/bin/bash

echo "Building Cuckoo Trie with vectorization..."

# Clean and build
make clean
make

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful!"

# Test basic functionality
echo "Running basic tests..."
./test_debug insert

if [ $? -ne 0 ]; then
    echo "Basic test failed!"
    exit 1
fi

echo "Basic tests passed!"

# Run a simple benchmark to verify performance
echo "Running simple benchmark..."
./benchmark insert rand-8

echo "Vectorized Cuckoo Trie implementation complete!"
echo ""
echo "To compare performance:"
echo "1. Comment out '#define USE_VECTORIZED_SEARCH' in vectorized_search.h"
echo "2. Rebuild with 'make clean && make'"
echo "3. Run the same benchmark and compare results"
