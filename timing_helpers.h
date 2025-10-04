#ifndef TIMING_HELPERS_H
#define TIMING_HELPERS_H

#include <stdint.h>

// Comment out this line to disable all timing
#define ENABLE_TIMING

#ifdef ENABLE_TIMING
    #define UPDATE_TIMING_STATS(start_cycles, total, count, min, max, hist) do { \
        uint64_t cycles = rdtsc_stop() - start_cycles; \
        total += cycles; \
        count++; \
        if (cycles < min) min = cycles; \
        if (cycles > max) max = cycles; \
        int bin = (cycles < 50) ? 0 : (cycles < 550) ? 1 + (int)((cycles - 50) / 50) : (cycles >= 5000) ? 12 : -1; \
        if (bin >= 0) hist[bin]++; \
    } while(0)

    #define UPDATE_FIND_BY_PARENT_STATS(start_cycles, total, count, min, max, hist, is_secondary, cell_idx) do { \
        uint64_t cycles = rdtsc_stop() - start_cycles; \
        total += cycles; \
        count++; \
        if (cycles < min) min = cycles; \
        if (cycles > max) max = cycles; \
        int bin = (cycles < 50) ? 0 : (cycles < 550) ? 1 + (int)((cycles - 50) / 50) : (cycles >= 5000) ? 12 : -1; \
        if (bin >= 0) hist[bin]++; \
        if (is_secondary) { \
            find_by_parent_secondary_bucket++; \
            find_by_parent_secondary_cell_counts[cell_idx]++; \
        } else { \
            find_by_parent_primary_bucket++; \
            find_by_parent_primary_cell_counts[cell_idx]++; \
        } \
    } while(0)
#else
    #define UPDATE_TIMING_STATS(start_cycles, total, count, min, max, hist) do { } while(0)
    #define UPDATE_FIND_BY_PARENT_STATS(start_cycles, total, count, min, max, hist, is_secondary, cell_idx) do { } while(0)
#endif

#endif // TIMING_HELPERS_H
