#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdint.h>
#include <immintrin.h>
#include "config.h"
#include "cuckoo_trie_internal.h"
#include "atomics.h"

#define ITERATIONS 10000000
#define WARMUP_ITERATIONS 1000000

// Forward declarations - these functions are defined in main.c but not in headers
extern ct_entry_storage* find_entry_in_bucket_by_color_vectorized(ct_bucket* bucket,
                                                                  ct_entry_local_copy* result, uint64_t is_secondary,
                                                                  uint64_t tag, uint64_t color);

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
    
    // Create entries with proper structure
    for (int i = 0; i < CUCKOO_BUCKET_SIZE; i++) {
        ct_entry temp_entry;
        memset(&temp_entry, 0, sizeof(ct_entry));
        
        // Set up entry 2 to match our search criteria
        if (i == 2) {
            temp_entry.color_and_tag = (2 << TAG_BITS) | 2;  // color=2, tag=2
            temp_entry.parent_color_and_flags = 0;  // not secondary
        } else {
            temp_entry.color_and_tag = (i << TAG_BITS) | i;
            temp_entry.parent_color_and_flags = 0;
        }
        temp_entry.last_symbol = i;
        
        // Copy to storage (only copy the non-padded part)
        memcpy(&bucket->cells[i], &temp_entry, sizeof(ct_entry_storage));
    }
    
    uint64_t tag = 2;
    uint64_t color = 2;
    uint64_t is_secondary = 0;
    
    printf("Microbenchmark: Vectorized vs Scalar find_entry_in_bucket_by_color\n");
    printf("Bucket size: %d, Iterations: %d, Warmup: %d\n\n", 
           CUCKOO_BUCKET_SIZE, ITERATIONS, WARMUP_ITERATIONS);
    
    // Verify both functions find the same result
    ct_entry_local_copy scalar_result, vectorized_result;
    ct_entry_storage* scalar_found = find_entry_in_bucket_by_color_scalar(bucket, &scalar_result, is_secondary, tag, color);
    ct_entry_storage* vectorized_found = find_entry_in_bucket_by_color_vectorized(bucket, &vectorized_result, is_secondary, tag, color);
    
    if (scalar_found != vectorized_found) {
        printf("ERROR: Functions return different results!\n");
        printf("Scalar: %p, Vectorized: %p\n", (void*)scalar_found, (void*)vectorized_found);
        return 1;
    }
    
    if (scalar_found) {
        printf("Both functions found entry at position %ld\n", scalar_found - &bucket->cells[0]);
    } else {
        printf("Both functions returned NULL (no match found)\n");
    }
    
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
    
    printf("\nResults:\n");
    printf("Scalar version:     %lu cycles total, %.2f cycles/call\n", 
           scalar_cycles, (double)scalar_cycles / ITERATIONS);
    printf("Vectorized version: %lu cycles total, %.2f cycles/call\n", 
           vectorized_cycles, (double)vectorized_cycles / ITERATIONS);
    
    if (vectorized_cycles > 0) {
        printf("Speedup: %.2fx\n", (double)scalar_cycles / vectorized_cycles);
    }
    
    free(bucket);
    return 0;
}
