#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <immintrin.h>
#include "main.h"
#include "cuckoo_trie_internal.h"

#define ITERATIONS 10000000
#define WARMUP_ITERATIONS 1000000

// High-resolution timing
static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

// Non-vectorized version (copy from original)
ct_entry_storage* find_entry_in_bucket_by_color_scalar(ct_bucket* bucket,
                                                       ct_entry_local_copy* result, uint64_t is_secondary,
                                                       uint64_t tag, uint64_t color) {
    int i;
    uint64_t header_mask = 0;
    uint64_t header_values = 0;
    
    header_mask |= ((1ULL << TAG_BITS) - 1) << (8*offsetof(ct_entry, color_and_tag));
    header_values |= tag << (8*offsetof(ct_entry, color_and_tag));    
    header_mask |= ((uint64_t)((0xFF << TAG_BITS) & 0xFF)) << (8*offsetof(ct_entry, color_and_tag));
    header_values |= color << (8*offsetof(ct_entry, color_and_tag) + TAG_BITS);
    header_mask |= FLAG_SECONDARY_BUCKET << (8*offsetof(ct_entry, parent_color_and_flags));
    if (is_secondary)
        header_values |= FLAG_SECONDARY_BUCKET << (8*offsetof(ct_entry, parent_color_and_flags));

    for (i = 0; i < CUCKOO_BUCKET_SIZE; i++) {
        read_entry_non_atomic(&(bucket->cells[i]), &(result->value));
        uint64_t header = *((uint64_t*) (&(result->value)));
        if ((header & header_mask) == header_values)
            break;
    }
    if (i == CUCKOO_BUCKET_SIZE)
        return NULL;

    result->last_pos = &(bucket->cells[i]);
    return result->last_pos;
}

int main() {
    // Setup test data
    ct_bucket* bucket = aligned_alloc(64, sizeof(ct_bucket));
    ct_entry_local_copy result;
    
    // Initialize bucket with test data
    memset(bucket, 0, sizeof(ct_bucket));
    for (int i = 0; i < CUCKOO_BUCKET_SIZE; i++) {
        bucket->cells[i].color_and_tag = (i << TAG_BITS) | i;
        bucket->cells[i].parent_color_and_flags = 0;
        bucket->cells[i].last_symbol = i;
    }
    
    uint64_t tag = 2;
    uint64_t color = 2;
    uint64_t is_secondary = 0;
    
    printf("Microbenchmark: Vectorized vs Scalar find_entry_in_bucket_by_color\n");
    printf("Iterations: %d, Warmup: %d\n\n", ITERATIONS, WARMUP_ITERATIONS);
    
    // Warmup - Scalar
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        find_entry_in_bucket_by_color_scalar(bucket, &result, is_secondary, tag, color);
    }
    
    // Benchmark - Scalar
    uint64_t start = rdtsc();
    for (int i = 0; i < ITERATIONS; i++) {
        find_entry_in_bucket_by_color_scalar(bucket, &result, is_secondary, tag, color);
    }
    uint64_t end = rdtsc();
    uint64_t scalar_cycles = end - start;
    
    // Warmup - Vectorized
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        find_entry_in_bucket_by_color_vectorized(bucket, &result, is_secondary, tag, color);
    }
    
    // Benchmark - Vectorized
    start = rdtsc();
    for (int i = 0; i < ITERATIONS; i++) {
        find_entry_in_bucket_by_color_vectorized(bucket, &result, is_secondary, tag, color);
    }
    end = rdtsc();
    uint64_t vectorized_cycles = end - start;
    
    printf("Results:\n");
    printf("Scalar version:     %lu cycles total, %.2f cycles/call\n", 
           scalar_cycles, (double)scalar_cycles / ITERATIONS);
    printf("Vectorized version: %lu cycles total, %.2f cycles/call\n", 
           vectorized_cycles, (double)vectorized_cycles / ITERATIONS);
    printf("Speedup: %.2fx\n", (double)scalar_cycles / vectorized_cycles);
    
    free(bucket);
    return 0;
}
