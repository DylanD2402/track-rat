/**
 * Track Rat - allocating from PSRAM
 *
 * Internal RAM is what Bluetooth and Wi-Fi need, and small blocks of it
 * fragment. JSON documents and long lists go to the 8 MB of PSRAM instead,
 * falling back to internal RAM if PSRAM is full or missing. Firmware only.
 *
 *   PsramJsonDocument doc(4096);          // as DynamicJsonDocument
 *   std::vector<double, PsramAllocator<double>> points;
 */

#ifndef PSRAM_ALLOC_H
#define PSRAM_ALLOC_H

#include <ArduinoJson.h>
#include <stdlib.h>
#include "esp_heap_caps.h"

inline void* psramAlloc(size_t size) {
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(size);
}

// ArduinoJson's allocator interface
struct PsramJsonAllocator {
  void* allocate(size_t size) { return psramAlloc(size); }
  void deallocate(void* p) { heap_caps_free(p); }  // frees a block from either heap
  void* reallocate(void* p, size_t size) {
    void* q = heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return q ? q : heap_caps_realloc(p, size, MALLOC_CAP_8BIT);
  }
};
using PsramJsonDocument = BasicJsonDocument<PsramJsonAllocator>;

// The same for std containers
template <class T>
struct PsramAllocator {
  using value_type = T;
  PsramAllocator() = default;
  template <class U>
  PsramAllocator(const PsramAllocator<U>&) {}
  T* allocate(size_t n) {
    void* p = psramAlloc(n * sizeof(T));
    if (!p) abort();  // the firmware builds without exceptions; vector growth has nowhere to report this
    return (T*)p;
  }
  void deallocate(T* p, size_t) { heap_caps_free(p); }
  template <class U>
  bool operator==(const PsramAllocator<U>&) const { return true; }
  template <class U>
  bool operator!=(const PsramAllocator<U>&) const { return false; }
};

#endif  // PSRAM_ALLOC_H
