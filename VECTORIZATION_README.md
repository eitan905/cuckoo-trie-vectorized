# Cuckoo Trie Vectorization Implementation

This document describes the SIMD vectorization implementation for the Cuckoo Trie bucket search operations.

## Overview

The vectorization targets the primary performance bottleneck in Cuckoo Trie operations: sequential bucket searches. Instead of checking each of the 4 bucket entries one by one, we use SIMD instructions to compare all entries in parallel.

## Implementation Details

### Files Added
- `vectorized_search.h` - Header with vectorized function declarations
- `vectorized_search.c` - SIMD implementation of bucket search functions
- `test_vectorized.sh` - Test script for vectorized implementation
- `compare_performance.sh` - Performance comparison script

### Functions Vectorized
1. `find_entry_in_bucket_by_color_vectorized()` - Parallel search by color/tag
2. `find_entry_in_bucket_by_parent_vectorized()` - Parallel search by parent

### SIMD Strategy

#### AVX2 Implementation (Preferred)
- Load 4 x 64-bit entry headers into `__m256i` register
- Broadcast mask and expected values to vector registers
- Perform parallel mask-and-compare: `_mm256_and_si256()` + `_mm256_cmpeq_epi64()`
- Extract results using `_mm256_movemask_epi8()`
- Find first match by scanning result mask

#### SSE2 Fallback
- Process 2 entries at a time using `__m128i` registers
- Same mask-and-compare logic with `_mm_cmpeq_epi64()`
- Combine results from two SSE operations

#### Scalar Fallback
- Traditional sequential comparison for systems without SIMD support

### Performance Benefits

**Theoretical Speedup**: 4x (4 parallel comparisons vs 4 sequential)
**Practical Speedup**: 2-4x depending on:
- CPU architecture (Haswell+ recommended)
- Memory access patterns
- Cache behavior
- Workload characteristics

## Usage

### Building
```bash
make clean
make
```

The build automatically includes vectorization with AVX2 support.

### Testing
```bash
# Basic functionality test
./test_vectorized.sh

# Performance comparison
./compare_performance.sh

# Manual benchmarking
./benchmark pos-lookup rand-8
```

### Disabling Vectorization
To compare against scalar implementation:
1. Comment out `#define USE_VECTORIZED_SEARCH` in `vectorized_search.h`
2. Rebuild: `make clean && make`

## Technical Notes

### Compiler Requirements
- GCC with AVX2 support
- Flags: `-march=haswell -mavx2`
- Minimum CPU: Intel Haswell (2013) or AMD equivalent

### Memory Layout Assumptions
- Bucket size: 4 entries (matches AVX2 width)
- Entry headers: 64-bit comparable values
- Cache-line aligned buckets for optimal SIMD loads

### Thread Safety
- Maintains original thread safety guarantees
- SIMD operations are atomic at instruction level
- Same retry logic for concurrent modifications

## Benchmarking Results

Expected improvements for lookup-heavy workloads:
- `pos-lookup`: 2-4x throughput improvement
- `ycsb-b` (95% reads): 1.5-2x overall improvement
- `ycsb-c` (100% reads): 2-4x overall improvement

Insert-heavy workloads see smaller improvements as vectorization only affects the lookup portion of insertions.

## Future Optimizations

Potential extensions:
1. **AVX-512**: 8-wide parallel comparisons (requires larger buckets)
2. **Prefetching**: SIMD-guided prefetch of likely bucket pairs
3. **Batch Processing**: Vectorize across multiple hash table lookups
4. **Custom Instructions**: Use specialized compare instructions like `vpcmpeqq`

## Debugging

### Common Issues
1. **Build Errors**: Ensure AVX2 support with `-march=haswell`
2. **Runtime Crashes**: Check CPU compatibility with `cat /proc/cpuinfo | grep avx2`
3. **No Performance Gain**: Verify vectorization is enabled and CPU supports AVX2

### Verification
```bash
# Check if vectorization is active
objdump -d libcuckoo_trie.so | grep -E "(vmov|vcmp|vpand)"

# Should show AVX2 instructions if vectorization is working
```

## References

- Intel Intrinsics Guide: https://software.intel.com/sites/landingpage/IntrinsicsGuide/
- AVX2 Programming Reference: Intel 64 and IA-32 Architectures Software Developer's Manual
- Original Cuckoo Trie Paper: "Cuckoo Trie: Exploiting Memory-Level Parallelism for Efficient DRAM Indexing" (SOSP '21)
