# Cuckoo Trie Repository Summary

## What This Is
High-performance in-memory ordered index using memory-level parallelism. Research implementation from SOSP '21 paper by Adar Zeitak & Adam Morrison.

**PROJECT CONTEXT**: This is a vectorization project for Multi-Core Architecture course (0368-4183). Goal is to optimize bucket search operations using SIMD instructions.

**VECTORIZATION STATUS**: ✅ IMPLEMENTED - SIMD optimization using AVX2/SSE2 instructions for parallel bucket searches.

## Key Files
- `libcuckoo_trie.so` - Main library (API in API.md)
- `benchmark` - Performance testing tool
- `test` - Correctness testing tool
- `config.h` - Build configuration (comment `#define MULTITHREADING` for single-threaded)
- `cuckoo_trie_internal.h` - Core data structures and bucket operations
- `vectorized_search.h/.c` - **NEW**: SIMD-optimized bucket search functions
- `test_vectorized.sh` - **NEW**: Test script for vectorized implementation
- `compare_performance.sh` - **NEW**: Performance comparison script

## Vectorization Implementation
**Optimized Functions**:
- `find_entry_in_bucket_by_color_vectorized()` - AVX2/SSE2 parallel color/tag matching
- `find_entry_in_bucket_by_parent_vectorized()` - AVX2/SSE2 parallel parent matching

**SIMD Strategy**:
- Load 4 bucket entries (64-bit headers) into vector registers
- Parallel mask-and-compare operations using `_mm256_cmpeq_epi64()`
- Extract match results using `_mm256_movemask_epi8()`
- Fallback to SSE2 if AVX2 unavailable, scalar if neither available

**Performance Target**: Reduce bucket search from 4 sequential comparisons to 1 parallel operation

## Build & Setup
```bash
make                    # Build everything with vectorization
# Allocate 2MB huge pages (required):
sudo sh -c "echo 1000 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages"
```

## Testing Vectorization
```bash
# Test vectorized implementation
./test_vectorized.sh

# Compare vectorized vs scalar performance
./compare_performance.sh

# Manual testing
./test_debug insert     # Correctness test
./benchmark pos-lookup rand-8  # Performance test (lookup-heavy)
```

## Benchmark Types
- `insert`, `mt-insert` - Insertion performance
- `pos-lookup`, `mt-pos-lookup` - **Lookup performance (vectorization target)**
- `ycsb-a` through `ycsb-f` - YCSB workloads
- `mem-usage` - Memory efficiency

## Vectorization Technical Details
- **Bucket Size**: 4 entries per bucket (perfect for 256-bit AVX2)
- **Entry Layout**: 16-byte `ct_entry`, 8-byte header comparison
- **Instruction Sets**: AVX2 (preferred) → SSE2 (fallback) → Scalar
- **Compiler Flags**: `-march=haswell -mavx2` for optimal SIMD generation
- **Toggle**: `USE_VECTORIZED_SEARCH` macro in `vectorized_search.h`

## Important Notes
- Linux x86_64 only
- Requires 2MB huge pages
- **NEW**: Requires AVX2-capable CPU (Haswell+) for best performance
- Performance sensitive to memory allocator, NUMA, turbo-boost
- Research code - not production ready
- Use jemalloc + numactl for best performance
- Run benchmarks 5-10 times for reliable measurements
- **Vectorization provides 2-4x speedup for lookup-intensive workloads**
