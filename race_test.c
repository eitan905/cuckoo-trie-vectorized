#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "cuckoo_trie.h"
#include "random.h"
#include "dataset.h"

#define NUM_KEYS 1000
#define NUM_THREADS 16
#define TEST_DURATION_SEC 30
#define MAX_KEY_SIZE 16
#define DEFAULT_VALUE_SIZE 8

typedef struct {
    cuckoo_trie* trie;
    uint8_t* kvs_buf;
    uint64_t buf_size;
    int num_keys;
    volatile int* stop_flag;
    volatile long* lookup_count;
    volatile long* error_count;
    int thread_id;
} thread_ctx_t;

// Generate unique key-value pairs using the same pattern as test.c
void gen_test_kvs(uint8_t* buf, uint64_t num_kvs, uint64_t max_key_len) {
    uint64_t i;
    uint8_t* buf_pos;

    buf_pos = buf;
    i = 0;
    while (i < num_kvs) {
        ct_kv* kv = (ct_kv*) buf_pos;
        uint64_t len = (i % max_key_len) + 1; // Predictable lengths
        kv_init(kv, len, DEFAULT_VALUE_SIZE);
        
        // Make predictable but unique keys
        memset(kv_key_bytes(kv), 0, len);
        *(int*)kv_key_bytes(kv) = i;
        if (len > 4) {
            *(int*)(kv_key_bytes(kv) + 4) = i * 2;
        }
        
        memset(kv_value_bytes(kv), 0xAB, DEFAULT_VALUE_SIZE);
        
        buf_pos += kv_size(kv);
        i++;
    }
}

// Writer thread - continuously modifies existing entries
void* writer_thread(void* arg) {
    thread_ctx_t* ctx = (thread_ctx_t*)arg;
    uint8_t* buf_pos = ctx->kvs_buf;
    int key_idx = 0;
    
    while (!*(ctx->stop_flag)) {
        // Find the key to modify
        buf_pos = ctx->kvs_buf;
        for (int i = 0; i < key_idx; i++) {
            ct_kv* kv = (ct_kv*)buf_pos;
            buf_pos += kv_size(kv);
        }
        
        ct_kv* kv = (ct_kv*)buf_pos;
        
        // Modify the value (keep key same)
        uint8_t new_value = (uint8_t)(rand_uint64() & 0xFF);
        memset(kv_value_bytes(kv), new_value, DEFAULT_VALUE_SIZE);
        
        // Update in trie
        int created_new;
        int result = ct_upsert(ctx->trie, kv, &created_new);
        if (result != S_OK) {
            printf("Writer error: %d\n", result);
        }
        
        key_idx = (key_idx + 1) % ctx->num_keys;
        usleep(1000); // 1ms delay
    }
    return NULL;
}

// Reader thread - continuously looks up keys
void* reader_thread(void* arg) {
    thread_ctx_t* ctx = (thread_ctx_t*)arg;
    long lookups = 0;
    long errors = 0;
    
    while (!*(ctx->stop_flag)) {
        // Pick a random key to lookup
        int key_idx = rand_uint64() % ctx->num_keys;
        
        // Find the key in buffer
        uint8_t* buf_pos = ctx->kvs_buf;
        for (int i = 0; i < key_idx; i++) {
            ct_kv* kv = (ct_kv*)buf_pos;
            buf_pos += kv_size(kv);
        }
        
        ct_kv* expected_kv = (ct_kv*)buf_pos;
        
        // Perform lookup
        ct_kv* found_kv = ct_lookup(ctx->trie, kv_key_size(expected_kv), kv_key_bytes(expected_kv));
        
        lookups++;
        
        if (found_kv == NULL) {
            errors++;
            printf("Thread %d: Key %d not found (lookup %ld)\n", ctx->thread_id, key_idx, lookups);
        }
        
        // Tight loop to maximize race probability
        if (lookups % 100000 == 0) {
            usleep(100);
        }
    }
    
    __sync_fetch_and_add(ctx->lookup_count, lookups);
    __sync_fetch_and_add(ctx->error_count, errors);
    return NULL;
}

int main() {
    printf("Starting race condition stress test...\n");
    printf("Duration: %d seconds\n", TEST_DURATION_SEC);
    printf("Threads: %d readers + 2 writers\n", NUM_THREADS - 2);
    
    // Initialize random number generator
    seed_and_print();
    
    // Allocate trie
    cuckoo_trie* trie = ct_alloc(NUM_KEYS * 3);
    if (trie == NULL) {
        printf("Failed to allocate trie\n");
        return 1;
    }
    
    // Create key buffer
    uint64_t buf_size = NUM_KEYS * kv_required_size(MAX_KEY_SIZE, DEFAULT_VALUE_SIZE);
    uint8_t* kvs_buf = malloc(buf_size);
    if (kvs_buf == NULL) {
        printf("Failed to allocate key buffer\n");
        return 1;
    }
    
    // Generate keys
    gen_test_kvs(kvs_buf, NUM_KEYS, MAX_KEY_SIZE);
    
    // Insert all keys initially
    uint8_t* buf_pos = kvs_buf;
    for (int i = 0; i < NUM_KEYS; i++) {
        ct_kv* kv = (ct_kv*)buf_pos;
        int result = ct_insert(trie, kv);
        if (result != S_OK) {
            printf("Initial insert failed: %d\n", result);
            return 1;
        }
        buf_pos += kv_size(kv);
    }
    
    printf("Inserted %d keys successfully\n", NUM_KEYS);
    
    // Shared state
    volatile int stop_flag = 0;
    volatile long total_lookups = 0;
    volatile long total_errors = 0;
    
    // Create threads
    pthread_t threads[NUM_THREADS];
    thread_ctx_t contexts[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        contexts[i].trie = trie;
        contexts[i].kvs_buf = kvs_buf;
        contexts[i].buf_size = buf_size;
        contexts[i].num_keys = NUM_KEYS;
        contexts[i].stop_flag = &stop_flag;
        contexts[i].lookup_count = &total_lookups;
        contexts[i].error_count = &total_errors;
        contexts[i].thread_id = i;
        
        if (i < 2) {
            // First 2 threads are writers
            pthread_create(&threads[i], NULL, writer_thread, &contexts[i]);
        } else {
            // Rest are readers
            pthread_create(&threads[i], NULL, reader_thread, &contexts[i]);
        }
    }
    
    // Run test
    printf("Test running...\n");
    sleep(TEST_DURATION_SEC);
    
    // Stop all threads
    stop_flag = 1;
    
    // Wait for completion
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Report results
    long lookups = total_lookups;
    long errors = total_errors;
    
    printf("\n=== RESULTS ===\n");
    printf("Total lookups: %ld\n", lookups);
    printf("Total errors: %ld\n", errors);
    if (lookups > 0) {
        printf("Error rate: %.6f%%\n", errors * 100.0 / lookups);
    }
    
    // Cleanup
    ct_free(trie);
    free(kvs_buf);
    
    if (errors > 0) {
        printf("*** RACE CONDITION DETECTED ***\n");
        return 1;
    } else {
        printf("No race conditions detected in this run\n");
        return 0;
    }
}
