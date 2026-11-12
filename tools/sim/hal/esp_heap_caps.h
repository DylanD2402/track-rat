// Stand-in (tools/build_sim.py): one heap.
#pragma once
#include <stdint.h>
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
#define MALLOC_CAP_DMA 8

inline void* heap_caps_malloc(size_t n, uint32_t) { return malloc(n); }
inline void* heap_caps_calloc(size_t n, size_t s, uint32_t) { return calloc(n, s); }
inline void* heap_caps_realloc(void* p, size_t n, uint32_t) { return realloc(p, n); }
inline void heap_caps_free(void* p) { free(p); }
inline size_t heap_caps_get_free_size(uint32_t) { return 64u * 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 32u * 1024 * 1024; }
