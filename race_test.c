#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdatomic.h>

#include "cuckoo_trie.h"
#include "random.h"

#define NUM_KEYS 1000
#define NUM_THREADS 16
#define TEST_DURATION_SEC 30
#define MAX_KEY_SIZE 16

typedef struct {
    cuckoo_trie* trie;
    ct_kv** keys;
    int num_keys;
    atomic_int* stop_flag;
    atomic_long* lookup_count;
    atomic_long* error_count;
    int thread_id;
} thread_ctx_t;

// Generate a key-value pair
ct_kv* make_kv(int key_id, int key_size) {
    ct_kv* kv = malloc(sizeof(ct_kv) + key_size + 8);
    kv->key_size = key_size;
    kv->value_size = 8;
    
    // Make key predictable but unique
    memset(kv->bytes, 0, key_size);
    *(int*)kv->bytes = key_id;
    
    // Set value
    memset(kv->bytes + key_size, 0xAB, 8);
    return kv;
}

// Writer thread - continuously modifies existing entries
void* writer_thread(void* arg) {
    thread_ctx_t* ctx = (thread_ctx_t*)arg;
    int key_idx = 0;
    
    while (!atomic_load(ctx->stop_flag)) {
        // Pick a key to modify
        ct_kv* old_kv = ctx->keys[key_idx];
        ct_kv* new_kv = make_kv(key_idx + 10000, old_kv->key_size); // Different key content
        
        // Update the entry (this should trigger the race condition)
        int result = ct_upsert(ctx->trie, new_kv, NULL);
        if (result != S_OK) {
            printf("Writer error: %d\n", result);
        }
        
        free(ctx->keys[key_idx]);
        ctx->keys[key_idx] = new_kv;
        
        key_idx = (key_idx + 1) % ctx->num_keys;
        
        // Small delay to let readers get in between
        usleep(1);
    }
    return NULL;
}

// Reader thread - continuously looks up keys
void* reader_thread(void* arg) {
    thread_ctx_t* ctx = (thread_ctx_t*)arg;
    long lookups = 0;
    long errors = 0;
    
    while (!atomic_load(ctx->stop_flag)) {
        // Pick a random key to lookup
        int key_idx = rand() % ctx->num_keys;
        ct_kv* expected_kv = ctx->keys[key_idx];
        
        // Perform lookup
        ct_kv* found_kv = ct_lookup(ctx->trie, expected_kv->key_size, expected_kv->bytes);
        
        lookups++;
        
        if (found_kv == NULL) {
            // Key not found - this could indicate the race condition
            errors++;
            printf("Thread %d: Key %d not found (lookup %ld)\n", ctx->thread_id, key_idx, lookups);
        } else {
            // Verify the key content matches what we looked up
            if (memcmp(found_kv->bytes, expected_kv->bytes, expected_kv->key_size) != 0) {
                errors++;
                printf("Thread %d: Key mismatch! Expected key %d, got different content (lookup %ld)\n", 
                       ctx->thread_id, key_idx, lookups);
            }
        }
        
        // Tight loop to maximize race probability
        if (lookups % 100000 == 0) {
            usleep(1); // Tiny break to prevent total CPU saturation
        }
    }
    
    atomic_fetch_add(ctx->lookup_count, lookups);
    atomic_fetch_add(ctx->error_count, errors);
    return NULL;
}

int main() {
    printf("Starting race condition stress test...\n");
    printf("Duration: %d seconds\n", TEST_DURATION_SEC);
    printf("Threads: %d readers + 2 writers\n", NUM_THREADS - 2);
    
    // Initialize
    srand(time(NULL));
    cuckoo_trie* trie = ct_alloc(NUM_KEYS * 3);
    
    // Create initial keys
    ct_kv** keys = malloc(NUM_KEYS * sizeof(ct_kv*));
    for (int i = 0; i < NUM_KEYS; i++) {
        keys[i] = make_kv(i, 8 + (rand() % 8)); // Variable key sizes
        int result = ct_insert(trie, keys[i]);
        if (result != S_OK) {
            printf("Initial insert failed: %d\n", result);
            return 1;
        }
    }
    
    // Shared state
    atomic_int stop_flag = 0;
    atomic_long total_lookups = 0;
    atomic_long total_errors = 0;
    
    // Create threads
    pthread_t threads[NUM_THREADS];
    thread_ctx_t contexts[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        contexts[i].trie = trie;
        contexts[i].keys = keys;
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
    atomic_store(&stop_flag, 1);
    
    // Wait for completion
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Report results
    long lookups = atomic_load(&total_lookups);
    long errors = atomic_load(&total_errors);
    
    printf("\n=== RESULTS ===\n");
    printf("Total lookups: %ld\n", lookups);
    printf("Total errors: %ld\n", errors);
    printf("Error rate: %.6f%%\n", errors * 100.0 / lookups);
    
    if (errors > 0) {
        printf("*** RACE CONDITION DETECTED ***\n");
        return 1;
    } else {
        printf("No race conditions detected in this run\n");
        return 0;
    }
}
