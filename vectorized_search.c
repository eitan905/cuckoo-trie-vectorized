#include "vectorized_search.h"
#include "main.h"

#ifdef USE_VECTORIZED_SEARCH

// Vectorized search by color using AVX2
ct_entry_storage* find_entry_in_bucket_by_color_vectorized(ct_bucket* bucket,
                                                          ct_entry_local_copy* result, 
                                                          uint64_t is_secondary,
                                                          uint64_t tag, 
                                                          uint64_t color) {
    // Build the header mask and expected values (same as original)
    uint64_t header_mask = 0;
    uint64_t header_values = 0;

    header_mask |= ((1ULL << TAG_BITS) - 1) << (8*offsetof(ct_entry, color_and_tag));
    header_values |= tag << (8*offsetof(ct_entry, color_and_tag));

    header_mask |= ((uint64_t)((0xFF << TAG_BITS) & 0xFF)) << (8*offsetof(ct_entry, color_and_tag));
    header_values |= color << (8*offsetof(ct_entry, color_and_tag) + TAG_BITS);

    header_mask |= FLAG_SECONDARY_BUCKET << (8*offsetof(ct_entry, parent_color_and_flags));
    if (is_secondary)
        header_values |= FLAG_SECONDARY_BUCKET << (8*offsetof(ct_entry, parent_color_and_flags));

#ifdef MULTITHREADING
    uint32_t start_counter = read_int_atomic(&(bucket->write_lock_and_seq));
    if (start_counter & SEQ_INCREMENT)
        return NULL;
#else
    assert(bucket->write_lock_and_seq == 0);
#endif

    // Load all 4 entries as 64-bit values for SIMD comparison
    // Each ct_entry_storage is at least 8 bytes, we compare the first 8 bytes (header)
    uint64_t headers[CUCKOO_BUCKET_SIZE];
    
    // Read all headers
    for (int i = 0; i < CUCKOO_BUCKET_SIZE; i++) {
        ct_entry temp_entry;
        read_entry_non_atomic(&(bucket->cells[i]), &temp_entry);
        headers[i] = *((uint64_t*)&temp_entry);
    }

    // Use AVX2 for parallel comparison if available, otherwise fall back to SSE2
    #if defined(__AVX2__)
    // Load headers into AVX2 register (4 x 64-bit values)
    __m256i header_vec = _mm256_loadu_si256((__m256i*)headers);
    
    // Broadcast mask and expected values
    __m256i mask_vec = _mm256_set1_epi64x(header_mask);
    __m256i expected_vec = _mm256_set1_epi64x(header_values);
    
    // Apply mask and compare
    __m256i masked_headers = _mm256_and_si256(header_vec, mask_vec);
    __m256i cmp_result = _mm256_cmpeq_epi64(masked_headers, expected_vec);
    
    // Extract comparison mask
    int match_mask = _mm256_movemask_epi8(cmp_result);
    
    #elif defined(__SSE2__)
    // Use SSE2 for 2x64-bit comparisons
    __m128i header_vec1 = _mm_loadu_si128((__m128i*)&headers[0]);
    __m128i header_vec2 = _mm_loadu_si128((__m128i*)&headers[2]);
    
    __m128i mask_vec = _mm_set1_epi64x(header_mask);
    __m128i expected_vec = _mm_set1_epi64x(header_values);
    
    __m128i masked1 = _mm_and_si128(header_vec1, mask_vec);
    __m128i masked2 = _mm_and_si128(header_vec2, mask_vec);
    
    __m128i cmp1 = _mm_cmpeq_epi64(masked1, expected_vec);
    __m128i cmp2 = _mm_cmpeq_epi64(masked2, expected_vec);
    
    int match_mask1 = _mm_movemask_epi8(cmp1);
    int match_mask2 = _mm_movemask_epi8(cmp2);
    
    #else
    // Fallback to scalar comparison
    int match_mask1 = 0, match_mask2 = 0;
    for (int i = 0; i < 2; i++) {
        if ((headers[i] & header_mask) == header_values) {
            match_mask1 |= (0xFF << (i * 8));
        }
        if ((headers[i + 2] & header_mask) == header_values) {
            match_mask2 |= (0xFF << (i * 8));
        }
    }
    #endif

    // Find first match
    int match_index = -1;
    
    #if defined(__AVX2__)
    if (match_mask != 0) {
        // Find first set bit group (each 64-bit comparison sets 8 bits)
        for (int i = 0; i < 4; i++) {
            if (match_mask & (0xFF << (i * 8))) {
                match_index = i;
                break;
            }
        }
    }
    #else
    if (match_mask1 != 0) {
        match_index = (match_mask1 & 0xFF) ? 0 : 1;
    } else if (match_mask2 != 0) {
        match_index = (match_mask2 & 0xFF) ? 2 : 3;
    }
    #endif

    if (match_index == -1) {
        return NULL;
    }

    // Read the matching entry properly
    read_entry_non_atomic(&(bucket->cells[match_index]), &(result->value));

#ifdef MULTITHREADING
    if (read_int_atomic(&(bucket->write_lock_and_seq)) != start_counter) {
        return NULL;
    }
    result->last_seq = start_counter;
#endif

    result->last_pos = &(bucket->cells[match_index]);
    return &(bucket->cells[match_index]);
}

// Vectorized search by parent using similar approach
ct_entry_storage* find_entry_in_bucket_by_parent_vectorized(ct_bucket* bucket,
                                                           ct_entry_local_copy* result, 
                                                           uint64_t is_secondary,
                                                           uint64_t tag, 
                                                           uint64_t last_symbol, 
                                                           uint64_t parent_color) {
    // Build header mask and values
    uint64_t header_mask = 0;
    uint64_t header_values = 0;

    header_mask |= ((1ULL << TAG_BITS) - 1) << (8*offsetof(ct_entry, color_and_tag));
    header_values |= tag << (8*offsetof(ct_entry, color_and_tag));

    header_mask |= 0xFFULL << (8*offsetof(ct_entry, last_symbol));
    header_values |= last_symbol << (8*offsetof(ct_entry, last_symbol));

    const uint64_t parent_color_mask = (0xFFULL << PARENT_COLOR_SHIFT) & 0xFF;
    header_mask |= parent_color_mask << (8*offsetof(ct_entry, parent_color_and_flags));
    header_values |= parent_color << (8*offsetof(ct_entry, parent_color_and_flags) + PARENT_COLOR_SHIFT);

    header_mask |= FLAG_SECONDARY_BUCKET << (8*offsetof(ct_entry, parent_color_and_flags));
    if (is_secondary)
        header_values |= FLAG_SECONDARY_BUCKET << (8*offsetof(ct_entry, parent_color_and_flags));

#ifdef MULTITHREADING
    uint32_t start_counter = read_int_atomic(&(bucket->write_lock_and_seq));
    if (start_counter & SEQ_INCREMENT)
        return NULL;
#else
    assert(bucket->write_lock_and_seq == 0);
#endif

    // Load headers for vectorized comparison
    uint64_t headers[CUCKOO_BUCKET_SIZE];
    for (int i = 0; i < CUCKOO_BUCKET_SIZE; i++) {
        ct_entry temp_entry;
        read_entry_non_atomic(&(bucket->cells[i]), &temp_entry);
        headers[i] = *((uint64_t*)&temp_entry);
    }

    // Vectorized comparison (same pattern as color search)
    int match_index = -1;
    
    #if defined(__AVX2__)
    __m256i header_vec = _mm256_loadu_si256((__m256i*)headers);
    __m256i mask_vec = _mm256_set1_epi64x(header_mask);
    __m256i expected_vec = _mm256_set1_epi64x(header_values);
    
    __m256i masked_headers = _mm256_and_si256(header_vec, mask_vec);
    __m256i cmp_result = _mm256_cmpeq_epi64(masked_headers, expected_vec);
    int match_mask = _mm256_movemask_epi8(cmp_result);
    
    if (match_mask != 0) {
        for (int i = 0; i < 4; i++) {
            if (match_mask & (0xFF << (i * 8))) {
                match_index = i;
                break;
            }
        }
    }
    #else
    // SSE2 or scalar fallback
    for (int i = 0; i < CUCKOO_BUCKET_SIZE; i++) {
        if ((headers[i] & header_mask) == header_values) {
            match_index = i;
            break;
        }
    }
    #endif

    if (match_index == -1) {
        return NULL;
    }

    // Verify the entry is not unused
    read_entry_non_atomic(&(bucket->cells[match_index]), &(result->value));
    if (entry_type(&(result->value)) == TYPE_UNUSED) {
        return NULL;
    }

#ifdef MULTITHREADING
    if (read_int_atomic(&(bucket->write_lock_and_seq)) != start_counter) {
        return NULL;
    }
    result->last_seq = start_counter;
#endif

    result->last_pos = &(bucket->cells[match_index]);
    return &(bucket->cells[match_index]);
}

#endif // USE_VECTORIZED_SEARCH
