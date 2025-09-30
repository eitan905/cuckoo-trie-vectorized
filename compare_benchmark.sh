#!/bin/bash

echo "=== Testing Scalar Version ==="
./benchmark pos-lookup rand-8

echo ""
echo "=== Testing Vectorized Version ==="
./benchmark_vectorized pos-lookup rand-8
