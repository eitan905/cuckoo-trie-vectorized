#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "config.h"
#include "cuckoo_trie_internal.h"
#include "atomics.h"

#define ITERATIONS 10000000
#define WARMUP_ITERATIONS 1000000

// High-resolution timing
static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

// Declare external functions from main.c
extern ct_entry_storage* find_entry_in_bucket_by_color_vectorized(ct_bucket* bucket,
                                                                  ct_entry_local_copy* result, uint64_t is_secondary,
                                                                  uint64_t tag, uint64_t color);

// Simple scalar version for comparison
static ct_entry_storage* scalar_search(ct_bucket* bucket, ct_entry_local_copy* result, 
                                      uint64_t is_secondary, uint64_t tag, uint64_t color) {
    uint64_t header_mask = 0;
    uint64_t header_values = 0;
    
    header_mask |= ((1ULL << TAG_BITS) - 1) << (8*offsetof(ct_entry, color_and_tag));
    header_values |= tag << (8*offsetof(ct_entry, color_and_tag));    
    header_mask |= ((uint64_t)((0xFF << TAG_BITS) & 0xFF)) << (8*offsetof(ct_entry, color_and_tag));
    header_values |= color << (8*offsetof(ct_entry, color_and_tag) + TAG_BITS);
    header_mask |= FLAG_SECONDARY_BUCKET << (8*offsetof(ct_entry, parent_color_and_flags));
    if (is_secondary)
        header_values |= FLAG_SECONDARY_BUCKET << (8*offsetof(ct_entry, parent_color_and_flags));

    for (int i = 0; i < CUCKOO_BUCKET_SIZE; i++) {
        read_entry_non_atomic(&(bucket->cells[i]), &(result->value));
        uint64_t header = *((uint64_t*) (&(result->value)));
        if ((header & header_mask) == header_values) {
            result->last_pos = &(bucket->cells[i]);
            return result->last_pos;
        }
    }
    return NULL;
}

int main() {
    ct_bucket* bucket = aligned_alloc(64, sizeof(ct_bucket));
    ct_entry_local_copy result;
    
    memset(bucket, 0, sizeof(ct_bucket));
    
    // Set up test data - entry 2 matches our search
    for (int i = 0; i < CUCKOO_BUCKET_SIZE; i++) {
        ct_entry temp;
        memset(&temp, 0, sizeof(ct_entry));
        temp.color_and_tag = (2 << TAG_BITS) | 2;  // All entries match for consistent timing
        temp.parent_color_and_flags = 0;
        memcpy(&bucket->cells[i], &temp, sizeof(ct_entry_storage));
    }
    
    uint64_t tag = 2, color = 2, is_secondary = 0;
    
    printf("Microbenchmark: Vectorized vs Scalar search\n");
    printf("Iterations: %d\n\n", ITERATIONS);
    
    // Warmup scalar
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        scalar_search(bucket, &result, is_secondary, tag, color);
    }
    
    // Benchmark scalar
    uint64_t start = rdtsc();
    for (int i = 0; i < ITERATIONS; i++) {
        scalar_search(bucket, &result, is_secondary, tag, color);
    }
    uint64_t scalar_cycles = rdtsc() - start;
    
    // Warmup vectorized
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        find_entry_in_bucket_by_color_vectorized(bucket, &result, is_secondary, tag, color);
    }
    
    // Benchmark vectorized
    start = rdtsc();
    for (int i = 0; i < ITERATIONS; i++) {
        find_entry_in_bucket_by_color_vectorized(bucket, &result, is_secondary, tag, color);
    }
    uint64_t vectorized_cycles = rdtsc() - start;
    
    printf("Scalar:     %.2f cycles/call\n", (double)scalar_cycles / ITERATIONS);
    printf("Vectorized: %.2f cycles/call\n", (double)vectorized_cycles / ITERATIONS);
    printf("Speedup:    %.2fx\n", (double)scalar_cycles / vectorized_cycles);
    
    free(bucket);
    return 0;
}
