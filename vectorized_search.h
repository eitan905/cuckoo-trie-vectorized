#ifndef VECTORIZED_SEARCH_H
#define VECTORIZED_SEARCH_H

#include <immintrin.h>
#include "cuckoo_trie_internal.h"

// Enable vectorized search functions
#define USE_VECTORIZED_SEARCH

#ifdef USE_VECTORIZED_SEARCH

// Vectorized version of find_entry_in_bucket_by_color
ct_entry_storage* find_entry_in_bucket_by_color_vectorized(ct_bucket* bucket,
                                                          ct_entry_local_copy* result, 
                                                          uint64_t is_secondary,
                                                          uint64_t tag, 
                                                          uint64_t color);

// Vectorized version of find_entry_in_bucket_by_parent  
ct_entry_storage* find_entry_in_bucket_by_parent_vectorized(ct_bucket* bucket,
                                                           ct_entry_local_copy* result, 
                                                           uint64_t is_secondary,
                                                           uint64_t tag, 
                                                           uint64_t last_symbol, 
                                                           uint64_t parent_color);

#endif // USE_VECTORIZED_SEARCH

#endif // VECTORIZED_SEARCH_H
