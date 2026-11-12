// Just enough of the Arduino API to build the firmware's code on a PC or as WebAssembly
// (tools/render_screens.py for the screens, tools/build_sim.py for the whole firmware).
// Not used by the firmware.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdarg.h>
#include <string>
#include <time.h>

using std::max;
using std::min;

#define PROGMEM
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define F(x) (x)
#define PSTR(x) (x)
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define TWO_PI 6.283185307179586476925286766559
#define HALF_PI 1.5707963267948966192313216916398
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))
typedef uint8_t byte;
typedef bool boolean;

extern uint32_t mockMillis;  // set by the screen renderer (the simulator sets it on every tick)
inline uint32_t millis() { return mockMillis; }
inline uint32_t micros() { return mockMillis * 1000u; }
#ifdef TRACKRAT_SIM
inline void delay(uint32_t ms) { mockMillis += ms; }  // virtual time: a wait is the clock moving on
#else
inline void delay(uint32_t) {}
#endif
inline void delayMicroseconds(uint32_t) {}
inline void yield() {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
int digitalRead(int pin);  // the simulator answers for the BOOT button
inline void* ps_malloc(size_t n) { return malloc(n); }

class String {
public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(unsigned char v, unsigned char base = 10) : s_(num((unsigned long long)v, base)) {}
  String(int v, unsigned char base = 10) : s_(snum((long long)v, base)) {}
  String(unsigned v, unsigned char base = 10) : s_(num((unsigned long long)v, base)) {}
  String(long v, unsigned char base = 10) : s_(snum((long long)v, base)) {}
  String(unsigned long v, unsigned char base = 10) : s_(num((unsigned long long)v, base)) {}
  String(long long v, unsigned char base = 10) : s_(snum(v, base)) {}
  String(unsigned long long v, unsigned char base = 10) : s_(num(v, base)) {}
  String(float v, int decimals = 2) : String((double)v, decimals) {}
  String(double v, int decimals = 2) {
    char b[48];
    snprintf(b, sizeof(b), "%.*f", decimals, v);
    s_ = b;
  }

  unsigned length() const { return s_.size(); }
  const char* c_str() const { return s_.c_str(); }
  bool isEmpty() const { return s_.empty(); }
  void reserve(unsigned n) { s_.reserve(n); }
  char operator[](unsigned i) const { return i < s_.size() ? s_[i] : 0; }
  char& operator[](unsigned i) { return s_[i]; }
  char charAt(unsigned i) const { return (*this)[i]; }
  void setCharAt(unsigned i, char c) { if (i < s_.size()) s_[i] = c; }
  void remove(unsigned i) { if (i < s_.size()) s_.erase(i); }
  void remove(unsigned i, unsigned n) { if (i < s_.size()) s_.erase(i, n); }
  bool startsWith(const String& p) const { return s_.rfind(p.s_, 0) == 0; }
  bool endsWith(const String& p) const {
    return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
  }
  String substring(unsigned from) const { return from < s_.size() ? String(s_.substr(from)) : String(); }
  String substring(unsigned from, unsigned to) const {
    if (from > to) std::swap(from, to);
    return from < s_.size() ? String(s_.substr(from, to - from)) : String();
  }
  int indexOf(char c, unsigned from = 0) const { size_t p = s_.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String& t, unsigned from = 0) const { size_t p = s_.find(t.s_, from); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { size_t p = s_.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(const String& t) const { size_t p = s_.rfind(t.s_); return p == std::string::npos ? -1 : (int)p; }
  long toInt() const { return atol(s_.c_str()); }
  float toFloat() const { return (float)atof(s_.c_str()); }
  double toDouble() const { return atof(s_.c_str()); }
  void trim() {
    size_t b = s_.find_first_not_of(" \t\r\n\f\v");
    if (b == std::string::npos) { s_.clear(); return; }
    s_ = s_.substr(b, s_.find_last_not_of(" \t\r\n\f\v") - b + 1);
  }
  void toLowerCase() { for (char& c : s_) c = (char)tolower((unsigned char)c); }
  void toUpperCase() { for (char& c : s_) c = (char)toupper((unsigned char)c); }
  void replace(char a, char b) { for (char& c : s_) if (c == a) c = b; }
  void replace(const String& a, const String& b) {
    if (a.s_.empty()) return;
    for (size_t p = 0; (p = s_.find(a.s_, p)) != std::string::npos; p += b.s_.size()) s_.replace(p, a.s_.size(), b.s_);
  }
  bool equals(const String& o) const { return s_ == o.s_; }
  bool equals(const char* o) const { return s_ == o; }
  bool equalsIgnoreCase(const String& o) const {
    if (s_.size() != o.s_.size()) return false;
    for (size_t i = 0; i < s_.size(); i++) if (tolower((unsigned char)s_[i]) != tolower((unsigned char)o.s_[i])) return false;
    return true;
  }
  int compareTo(const String& o) const { return s_.compare(o.s_); }
  bool concat(const String& o) { s_ += o.s_; return true; }
  bool concat(const char* o) { s_ += o ? o : ""; return true; }
  bool concat(char c) { s_ += c; return true; }

  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* o) { s_ += o; return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator==(const char* o) const { return s_ == o; }
  bool operator!=(const String& o) const { return s_ != o.s_; }
  bool operator!=(const char* o) const { return s_ != o; }
  bool operator<(const String& o) const { return s_ < o.s_; }
  bool operator>(const String& o) const { return s_ > o.s_; }
  bool operator<=(const String& o) const { return s_ <= o.s_; }
  bool operator>=(const String& o) const { return s_ >= o.s_; }

  friend String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
  friend String operator+(const String& a, const char* b) { return String(a.s_ + b); }
  friend String operator+(const char* a, const String& b) { return String(std::string(a) + b.s_); }
  friend String operator+(const String& a, char b) { return String(a.s_ + b); }
  friend String operator+(char a, const String& b) { return String(std::string(1, a) + b.s_); }

private:
  static std::string num(unsigned long long v, unsigned char base) {
    if (base == 10) return std::to_string(v);
    std::string r;
    do { r.insert(r.begin(), "0123456789abcdef"[v % base]); v /= base; } while (v);
    return r;
  }
  static std::string snum(long long v, unsigned char base) {
    if (base == 10) return std::to_string(v);
    return v < 0 ? "-" + num((unsigned long long)-v, base) : num((unsigned long long)v, base);
  }
  std::string s_;
};

class __FlashStringHelper;  // (ArduinoJson names it)
#define strlen_P strlen
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strcasecmp_P strcasecmp
#define memcmp_P memcmp
#define memcpy_P memcpy
#define pgm_read_dword(p) (*(const uint32_t*)(p))
#define pgm_read_float(p) (*(const float*)(p))
#define pgm_read_ptr(p) (*(void* const*)(p))

// Print and Stream, for ArduinoJson and the files
class Print;
class Printable {
public:
  virtual ~Printable() {}
  virtual size_t printTo(Print& p) const = 0;
};

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) {
    size_t k = 0;
    while (k < n && write(b[k])) k++;
    return k;
  }
  size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
  size_t write(const char* s, size_t n) { return write((const uint8_t*)s, n); }
  virtual void flush() {}
  size_t print(const String& s) { return write(s.c_str()); }
  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v) { return print(String(v)); }
  size_t print(unsigned v) { return print(String(v)); }
  size_t print(long v) { return print(String(v)); }
  size_t print(unsigned long v) { return print(String(v)); }
  size_t print(double v, int d = 2) { return print(String(v, d)); }
  size_t println() { return write("\r\n"); }
  size_t println(const String& s) { return print(s) + println(); }
  size_t println(const char* s) { return print(s) + println(); }
  size_t println(char c) { return print(c) + println(); }
  size_t println(int v) { return print(v) + println(); }
  size_t println(unsigned v) { return print(v) + println(); }
  size_t println(long v) { return print(v) + println(); }
  size_t println(unsigned long v) { return print(v) + println(); }
  size_t println(double v, int d = 2) { return print(v, d) + println(); }
  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char b[256];
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, a);
    va_end(a);
    if (n < 0) return 0;
    if ((size_t)n < sizeof(b)) return write((const uint8_t*)b, n);
    std::string big(n + 1, 0);
    va_start(a, fmt);
    vsnprintf(&big[0], big.size(), fmt, a);
    va_end(a);
    return write((const uint8_t*)big.data(), n);
  }
};

class Stream : public Print {
public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual size_t readBytes(char* buf, size_t n) {
    size_t k = 0;
    while (k < n) {
      int c = read();
      if (c < 0) break;
      buf[k++] = (char)c;
    }
    return k;
  }
  void setTimeout(unsigned long) {}
};

// The USB serial log (and the GPS's UART): the simulator prints lines to JavaScript
class HardwareSerial : public Stream {
public:
  void begin(unsigned long = 115200, uint32_t = 0, int = -1, int = -1) {}
  void end() {}
  void setTxBufferSize(size_t) {}
  void setRxBufferSize(size_t) {}
  void setTxTimeoutMs(uint32_t) {}
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* b, size_t n) override;
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};
#define SERIAL_8N1 0
extern HardwareSerial Serial;
extern HardwareSerial Serial1;

// The chip: free memory is plentiful in a browser, and a restart is JavaScript's to do
class EspClass {
public:
  uint32_t getFreePsram() const { return 8u * 1024 * 1024; }
  uint32_t getMaxAllocPsram() const { return 8u * 1024 * 1024; }
  uint32_t getFreeHeap() const { return 200u * 1024; }
  uint32_t getMaxAllocHeap() const { return 100u * 1024; }
  [[noreturn]] void restart();
};
extern EspClass ESP;

#ifdef TRACKRAT_SIM
#include <freertos/FreeRTOS.h>
#endif
