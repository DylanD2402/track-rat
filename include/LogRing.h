/**
 * Track Rat - Log buffer
 *
 * A ring of records ([type][length, 2 bytes][bytes]) with exactly one
 * producer (the main loop) and one consumer (the log writer task). Each side
 * only moves its own counter, so neither ever waits for the other, and no
 * lock is needed across the two cores.
 *
 * No Arduino dependencies: tested in test/test_vbo.cpp with two threads.
 */

#ifndef LOG_RING_H
#define LOG_RING_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <atomic>

class LogRing {
public:
  static constexpr size_t MAX_RECORD = 4096;
  static constexpr size_t RECORD_HEADER = 3;

  // `size` must be a power of two.
  void attach(uint8_t* mem, size_t size) {
    buf = mem;
    cap = size;
    head.store(0);
    tail.store(0);
  }
  bool attached() const { return buf != nullptr; }

  // --- producer ---
  size_t freeSpace() const { return cap - (head.load(std::memory_order_relaxed) - tail.load(std::memory_order_acquire)); }

  bool push(uint8_t type, const void* data, size_t len) {
    if (!buf || len > MAX_RECORD || freeSpace() < len + RECORD_HEADER) return false;
    uint32_t h = head.load(std::memory_order_relaxed);
    const uint8_t hdr[RECORD_HEADER] = {type, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
    put(h, hdr, RECORD_HEADER);
    if (len) put(h + RECORD_HEADER, data, len);
    head.store(h + RECORD_HEADER + len, std::memory_order_release);  // publishes the record
    return true;
  }

  // --- consumer ---
  // The oldest record's type and length, if there is one.
  bool next(uint8_t& type, size_t& len) const {
    uint32_t t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) return false;
    uint8_t hdr[RECORD_HEADER];
    get(t, hdr, RECORD_HEADER);
    type = hdr[0];
    len = hdr[1] | hdr[2] << 8;
    return true;
  }
  // The payload of the record next() returned, or its first `max` bytes.
  void copyPayload(void* dst, size_t max = MAX_RECORD) const {
    uint32_t t = tail.load(std::memory_order_relaxed);
    uint8_t hdr[RECORD_HEADER];
    get(t, hdr, RECORD_HEADER);
    size_t len = hdr[1] | hdr[2] << 8;
    get(t + RECORD_HEADER, dst, len < max ? len : max);
  }
  void pop() {  // frees it for the producer
    uint32_t t = tail.load(std::memory_order_relaxed);
    uint8_t hdr[RECORD_HEADER];
    get(t, hdr, RECORD_HEADER);
    tail.store(t + RECORD_HEADER + (hdr[1] | hdr[2] << 8), std::memory_order_release);
  }

  size_t used() const { return head.load() - tail.load(); }

private:
  uint8_t* buf = nullptr;
  size_t cap = 0;
  std::atomic<uint32_t> head{0};  // bytes ever pushed; wraps harmlessly
  std::atomic<uint32_t> tail{0};  // bytes ever popped

  void put(uint32_t pos, const void* src, size_t n) {
    size_t off = pos & (cap - 1);
    size_t first = n < cap - off ? n : cap - off;
    memcpy(buf + off, src, first);
    memcpy(buf, (const uint8_t*)src + first, n - first);
  }
  void get(uint32_t pos, void* dst, size_t n) const {
    size_t off = pos & (cap - 1);
    size_t first = n < cap - off ? n : cap - off;
    memcpy(dst, buf + off, first);
    memcpy((uint8_t*)dst + first, buf, n - first);
  }
};

#endif  // LOG_RING_H
