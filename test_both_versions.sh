#!/bin/bash

echo "Building and testing scalar version..."
make clean
make VECTORIZED=
./benchmark insert rand-8 > scalar_results.txt

echo "Building and testing vectorized version..."  
make clean
make VECTORIZED=-DUSE_VECTORIZED_SEARCH
./benchmark insert rand-8 > vectorized_results.txt

echo "Results comparison:"
echo "Scalar:"
cat scalar_results.txt
echo "Vectorized:"
cat vectorized_results.txt
