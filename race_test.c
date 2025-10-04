// race_test.c
// Detects mixed-snapshot races in a multithreaded Cuckoo Trie lookup path.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <inttypes.h>   // <-- for PRIu64

#include "cuckoo_trie.h"
#include "random.h"
#include "dataset.h"

// ---------- Tunables ----------
#ifndef NUM_THREADS
#define NUM_THREADS 32          // 2 writers + (NUM_THREADS-2) readers
#endif

#ifndef TEST_DURATION_SEC
#define TEST_DURATION_SEC 60
#endif

#ifndef NUM_KEYS
#define NUM_KEYS 2              // concentrate contention
#endif

#ifndef MAX_KEY_SIZE
#define MAX_KEY_SIZE 16
#endif

#ifndef DEFAULT_VALUE_SIZE
#define DEFAULT_VALUE_SIZE 8    // 7-byte key signature + 1-byte flip
#endif
// -----------------------------

typedef struct {
    cuckoo_trie* trie;
    uint8_t*     kvs_buf;
    uint64_t     buf_size;
    int          num_keys;
    volatile int*   stop_flag;
    volatile long*  lookup_count;
    volatile long*  error_count;
    int          thread_id;
} thread_ctx_t;

// Map an index to its KV pointer inside the linear buffer
static inline ct_kv* kv_at(uint8_t* base, int idx) {
    uint8_t* p = base;
    for (int i = 0; i < idx; i++) {
        ct_kv* kv = (ct_kv*)p;
        p += kv_size(kv);
    }
    return (ct_kv*)p;
}

// 7-byte signature derived from the key’s 64-bit ID (low 56 bits, LE)
static inline void make_key_sig(uint64_t key_id, uint8_t sig7[7]) {
    memcpy(sig7, &key_id, 7);
}

// Generate deterministic, self-validating key-value pairs
void gen_test_kvs(uint8_t* buf, uint64_t num_kvs, uint64_t max_key_len) {
    (void)max_key_len; // we always use 8 here
    uint8_t* p = buf;
    for (uint64_t i = 0; i < num_kvs; i++) {
        ct_kv* kv = (ct_kv*)p;
        uint64_t klen = 8; // fixed 8-byte key (stores i)
        kv_init(kv, klen, DEFAULT_VALUE_SIZE);

        // key bytes = 64-bit i (little-endian)
        memset(kv_key_bytes(kv), 0, klen);
        *(uint64_t*)kv_key_bytes(kv) = i;

        // value[0..6] = signature(key), value[7] = flip (starts at 0)
        make_key_sig(i, kv_value_bytes(kv));
        kv_value_bytes(kv)[7] = 0;

        p += kv_size(kv);
    }
}

// Writer thread: flip only the last value byte; keep signature constant
void* writer_thread(void* arg) {
    thread_ctx_t* ctx = (thread_ctx_t*)arg;
    int key_idx = 0;

    while (!*(ctx->stop_flag)) {
        ct_kv* kv = kv_at(ctx->kvs_buf, key_idx);

        // Flip the last byte to ensure frequent updates without changing signature
        uint8_t* val = kv_value_bytes(kv);
        val[7] ^= 0xFF;

        int created_new = 0;
        int rc = ct_upsert(ctx->trie, kv, &created_new);
        if (rc != S_OK) {
            fprintf(stderr, "Writer error: rc=%d on key_idx=%d\n", rc, key_idx);
        }

        key_idx = (key_idx + 1) % ctx->num_keys;
    }

    return NULL;
}

// Reader thread: validate both key bytes and 7-byte signature on non-NULL results
void* reader_thread(void* arg) {
    thread_ctx_t* ctx = (thread_ctx_t*)arg;
    long local_lookups = 0;
    long local_errors  = 0;

    while (!*(ctx->stop_flag)) {
        int key_idx = (int)(rand_uint64() % (uint64_t)ctx->num_keys);

        ct_kv* expected = kv_at(ctx->kvs_buf, key_idx);
        const int exp_klen = kv_key_size(expected);
        uint8_t* exp_kbytes = kv_key_bytes(expected);  // non-const to satisfy ct_lookup signature

        // Precompute expected signature from key ID
        uint8_t expected_sig[7];
        uint64_t key_id = *(const uint64_t*)exp_kbytes;
        make_key_sig(key_id, expected_sig);

        // ct_lookup expects uint8_t*, not const void*
        ct_kv* found = ct_lookup(ctx->trie, exp_klen, exp_kbytes);
        local_lookups++;

        if (!found) {
            local_errors++;
        } else {
            // Validate key bytes (guards against wrong-entry return)
            int f_klen = kv_key_size(found);
            uint8_t* f_kbytes = kv_key_bytes(found);
            bool key_ok = (f_klen == exp_klen) &&
                          (memcmp(f_kbytes, exp_kbytes, exp_klen) == 0);

            // Validate 7-byte signature (guards against mixed-snapshot payload)
            const uint8_t* f_val = kv_value_bytes(found);
            bool sig_ok = (memcmp(f_val, expected_sig, 7) == 0);

            if (!(key_ok && sig_ok)) {
                local_errors++;
                // Optional debug:
                // fprintf(stderr, "T%d: mismatch key_idx=%d key_ok=%d sig_ok=%d\n",
                //         ctx->thread_id, key_idx, (int)key_ok, (int)sig_ok);
            }
        }

        if ((local_lookups & 0x1FFFF) == 0) {
            usleep(100);
        }
    }

    __sync_fetch_and_add(ctx->lookup_count, local_lookups);
    __sync_fetch_and_add(ctx->error_count,  local_errors);
    return NULL;
}

int main(void) {
    printf("Starting Cuckoo Trie race-condition stress test\n");
    printf("Config: duration=%ds, threads=%d (readers=%d, writers=2), keys=%d\n",
           TEST_DURATION_SEC, NUM_THREADS, NUM_THREADS - 2, NUM_KEYS);

    // RNG init
    seed_and_print();

    // Allocate trie with extra capacity
    cuckoo_trie* trie = ct_alloc(NUM_KEYS * 3);
    if (!trie) {
        fprintf(stderr, "Failed to allocate trie\n");
        return 1;
    }

    // Allocate a buffer big enough for NUM_KEYS entries (worst-case per-key size)
    uint64_t one_kv_size = kv_required_size(MAX_KEY_SIZE, DEFAULT_VALUE_SIZE);
    uint64_t buf_size = (uint64_t)NUM_KEYS * one_kv_size;
    uint8_t* kvs_buf = (uint8_t*)malloc(buf_size);
    if (!kvs_buf) {
        fprintf(stderr, "Failed to allocate key buffer (%" PRIu64 " bytes)\n", buf_size);
        ct_free(trie);
        return 1;
    }

    // Initialize KVs (8-byte keys, 8-byte values with signature+flip)
    gen_test_kvs(kvs_buf, NUM_KEYS, MAX_KEY_SIZE);

    // Insert all keys initially
    uint8_t* p = kvs_buf;
    for (int i = 0; i < NUM_KEYS; i++) {
        ct_kv* kv = (ct_kv*)p;
        int rc = ct_insert(trie, kv);
        if (rc != S_OK) {
            fprintf(stderr, "Initial insert failed for key %d: rc=%d\n", i, rc);
            free(kvs_buf);
            ct_free(trie);
            return 1;
        }
        p += kv_size(kv);
    }
    printf("Inserted %d keys successfully\n", NUM_KEYS);

    // Shared state
    volatile int  stop_flag = 0;
    volatile long total_lookups = 0;
    volatile long total_errors  = 0;

    // Create threads
    pthread_t    threads[NUM_THREADS];
    thread_ctx_t ctxs[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        ctxs[i].trie         = trie;
        ctxs[i].kvs_buf      = kvs_buf;
        ctxs[i].buf_size     = buf_size;
        ctxs[i].num_keys     = NUM_KEYS;
        ctxs[i].stop_flag    = &stop_flag;
        ctxs[i].lookup_count = &total_lookups;
        ctxs[i].error_count  = &total_errors;
        ctxs[i].thread_id    = i;

        if (i < 2) {
            if (pthread_create(&threads[i], NULL, writer_thread, &ctxs[i]) != 0) {
                fprintf(stderr, "Failed to create writer thread %d\n", i);
                stop_flag = 1;
                for (int j = 0; j < i; j++) pthread_join(threads[j], NULL);
                free(kvs_buf);
                ct_free(trie);
                return 1;
            }
        } else {
            if (pthread_create(&threads[i], NULL, reader_thread, &ctxs[i]) != 0) {
                fprintf(stderr, "Failed to create reader thread %d\n", i);
                stop_flag = 1;
                for (int j = 0; j < i; j++) pthread_join(threads[j], NULL);
                free(kvs_buf);
                ct_free(trie);
                return 1;
            }
        }
    }

    // Run test
    printf("Test running...\n");
    sleep(TEST_DURATION_SEC);

    // Signal stop and join
    stop_flag = 1;
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Report
    long lookups = total_lookups;
    long errors  = total_errors;

    printf("\n=== RESULTS ===\n");
    printf("Total lookups: %ld\n", lookups);
    printf("Total errors : %ld\n", errors);
    if (lookups > 0) {
        double rate = (errors * 100.0) / (double)lookups;
        printf("Error rate   : %.9f%%\n", rate);
    }

    // Cleanup
    ct_free(trie);
    free(kvs_buf);

    if (errors > 0) {
        printf("\n*** INCOHERENT SNAPSHOT DETECTED (NULL or wrong entry/value) ***\n");
        return 1;
    } else {
        printf("\nNo incoherent snapshots detected in this run\n");
        return 0;
    }
}
