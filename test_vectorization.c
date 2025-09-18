#include <stdio.h>
#include <stdint.h>
#include <immintrin.h>
#include <string.h>
#include <assert.h>

// Minimal definitions needed for testing
#define CUCKOO_BUCKET_SIZE 4
#define TAG_BITS 3
#define PARENT_COLOR_SHIFT 1
#define FLAG_SECONDARY_BUCKET 1

typedef struct {
    uint8_t parent_color_and_flags;
    uint8_t color_and_tag;
    uint8_t last_symbol;
    uint8_t data[8]; // Simplified entry
} test_entry;

typedef struct {
    test_entry cells[CUCKOO_BUCKET_SIZE];
    uint32_t write_lock_and_seq;
} test_bucket;

// Test the vectorized comparison logic
int test_vectorized_search() {
    test_bucket bucket = {0};
    
    // Set up test data
    bucket.cells[0].parent_color_and_flags = 0x02;
    bucket.cells[0].color_and_tag = 0x15; // tag=5, color=2
    bucket.cells[0].last_symbol = 0x41;
    
    bucket.cells[1].parent_color_and_flags = 0x04;
    bucket.cells[1].color_and_tag = 0x23; // tag=3, color=4
    bucket.cells[1].last_symbol = 0x42;
    
    bucket.cells[2].parent_color_and_flags = 0x06;
    bucket.cells[2].color_and_tag = 0x37; // tag=7, color=6
    bucket.cells[2].last_symbol = 0x43;
    
    bucket.cells[3].parent_color_and_flags = 0x08;
    bucket.cells[3].color_and_tag = 0x41; // tag=1, color=8
    bucket.cells[3].last_symbol = 0x44;
    
    // Test search for entry 1 (tag=3, color=4)
    uint64_t tag = 3;
    uint64_t color = 4;
    uint64_t is_secondary = 0;
    
    uint64_t header_mask = 0;
    uint64_t header_values = 0;
    
    header_mask |= ((1ULL << TAG_BITS) - 1) << (8*1); // color_and_tag offset
    header_values |= tag << (8*1);
    
    header_mask |= ((uint64_t)((0xFF << TAG_BITS) & 0xFF)) << (8*1);
    header_values |= color << (8*1 + TAG_BITS);
    
    header_mask |= FLAG_SECONDARY_BUCKET << (8*0); // parent_color_and_flags offset
    if (is_secondary)
        header_values |= FLAG_SECONDARY_BUCKET << (8*0);
    
    printf("Searching for tag=%lu, color=%lu\n", tag, color);
    printf("header_mask=0x%lx, header_values=0x%lx\n", header_mask, header_values);
    
    // Load first 8 bytes of each entry
    uint64_t headers[4];
    for (int j = 0; j < 4; j++) {
        headers[j] = *((uint64_t*)&bucket.cells[j]);
        printf("Entry %d header: 0x%lx\n", j, headers[j]);
    }
    
    __m256i mask_vec = _mm256_set1_epi64x(header_mask);
    __m256i values_vec = _mm256_set1_epi64x(header_values);
    __m256i headers_vec = _mm256_loadu_si256((__m256i*)headers);
    __m256i masked = _mm256_and_si256(headers_vec, mask_vec);
    __m256i cmp = _mm256_cmpeq_epi64(masked, values_vec);
    int mask = _mm256_movemask_epi8(cmp);
    
    printf("Comparison mask: 0x%x\n", mask);
    
    int i = mask ? (__builtin_ctz(mask) >> 3) : CUCKOO_BUCKET_SIZE;
    printf("Found at index: %d\n", i);
    
    // Verify manually
    for (int j = 0; j < 4; j++) {
        uint64_t header = headers[j];
        if ((header & header_mask) == header_values) {
            printf("Manual verification: found at index %d\n", j);
            return (i == j) ? 0 : 1;
        }
    }
    
    return (i == CUCKOO_BUCKET_SIZE) ? 0 : 1;
}

int main() {
    printf("Testing vectorized search implementation...\n");
    int result = test_vectorized_search();
    printf("Test %s\n", result == 0 ? "PASSED" : "FAILED");
    return result;
}
