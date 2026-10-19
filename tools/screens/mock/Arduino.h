// Just enough of the Arduino API to build the display code on a PC
// (tools/render_screens.py). Not used by the firmware.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using std::max;
using std::min;

#define PROGMEM
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))

extern uint32_t mockMillis;  // set by the screen renderer
inline uint32_t millis() { return mockMillis; }
inline void delay(uint32_t) {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline void* ps_malloc(size_t n) { return malloc(n); }

class String {
public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(double v, int decimals = 2) {
    char b[48];
    snprintf(b, sizeof(b), "%.*f", decimals, v);
    s_ = b;
  }

  unsigned length() const { return s_.size(); }
  const char* c_str() const { return s_.c_str(); }
  char operator[](unsigned i) const { return i < s_.size() ? s_[i] : 0; }
  void remove(unsigned i) { if (i < s_.size()) s_.erase(i); }
  void remove(unsigned i, unsigned n) { if (i < s_.size()) s_.erase(i, n); }
  bool startsWith(const String& p) const { return s_.rfind(p.s_, 0) == 0; }
  bool endsWith(const String& p) const {
    return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
  }
  String substring(unsigned from) const { return from < s_.size() ? String(s_.substr(from)) : String(); }
  String substring(unsigned from, unsigned to) const {
    return from < s_.size() ? String(s_.substr(from, to - from)) : String();
  }
  int lastIndexOf(char c) const { size_t p = s_.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  long toInt() const { return atol(s_.c_str()); }

  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* o) { s_ += o; return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator==(const char* o) const { return s_ == o; }
  bool operator!=(const String& o) const { return s_ != o.s_; }

  friend String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
  friend String operator+(const String& a, const char* b) { return String(a.s_ + b); }
  friend String operator+(const char* a, const String& b) { return String(std::string(a) + b.s_); }

private:
  std::string s_;
};
