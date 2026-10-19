/**
 * Track Rat - Wi-Fi page request rules
 *
 * The parts of WifiPortal that decide things (which file names are allowed,
 * what a time zone looks like, who owns an upload...). No Arduino code, so
 * test/test_portal.cpp checks them on a PC.
 */

#ifndef PORTAL_LOGIC_H
#define PORTAL_LOGIC_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace portal {

// A file in /sessions asked for by name: no folders, no "..", nothing odd.
inline bool isSafeFileName(const char* name) {
  size_t n = strlen(name);
  if (n == 0 || n > 120 || strstr(name, "..")) return false;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = name[i];
    if (c < 0x20 || c == '/' || c == '\\' || c == '"' || c == ':' || c == 0x7F) return false;
  }
  return true;
}

inline bool endsWith(const char* s, const char* suffix) {
  size_t n = strlen(s), m = strlen(suffix);
  return n >= m && strcmp(s + n - m, suffix) == 0;
}

inline const char* contentTypeFor(const char* name) {
  if (endsWith(name, ".json")) return "application/json";
  if (endsWith(name, ".csv")) return "text/csv";
  if (endsWith(name, ".gpx")) return "application/gpx+xml";
  if (endsWith(name, ".vbo") || endsWith(name, ".txt")) return "text/plain";
  return "application/octet-stream";
}

// "..." with quotes and backslashes escaped; control characters become spaces.
// Str: anything with += char (Arduino String, std::string).
template <class Str>
void appendJsonString(Str& out, const char* s) {
  out += '"';
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((unsigned char)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  out += '"';
}

// The phone's time zone, in minutes east of UTC ("-240", "+330", "0").
// Real zones run from UTC-12 to UTC+14. Anything else is refused.
inline bool parseTimeZone(const char* text, int* minutes) {
  if (!text || !*text) return false;
  const char* p = text;
  bool neg = false;
  if (*p == '+' || *p == '-') neg = (*p++ == '-');
  if (!*p) return false;
  long v = 0;
  for (; *p; p++) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + (*p - '0');
    if (v > 100000) return false;
  }
  if (neg) v = -v;
  if (v < -720 || v > 840) return false;
  *minutes = (int)v;
  return true;
}

// Requests for any other host are phones checking for internet: send them to us.
inline bool isPortalHost(const char* host, const char* ip) {
  size_t n = strlen(ip);
  if (strncmp(host, ip, n) != 0) return false;
  return host[n] == '\0' || strcmp(host + n, ":80") == 0;
}

// "TrackRat-XXXX" from the last two bytes of the MAC (byte 0 is the lowest byte of `mac`).
inline void networkName(uint64_t mac, char* buf, size_t len) {
  snprintf(buf, len, "TrackRat-%02X%02X", (unsigned)(uint8_t)(mac >> 32), (unsigned)(uint8_t)(mac >> 40));
}

// Percent of an upload received. Never 100 until the file is complete and installed.
inline int uploadPercent(size_t received, size_t total) {
  if (total == 0) return 0;
  uint64_t p = (uint64_t)received * 100 / total;
  return p > 99 ? 99 : (int)p;
}

// First non-blank byte of an upload is '{': a JSON object, worth saving.
inline bool looksLikeJsonObject(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    uint8_t c = data[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    if (i == 0 && len >= 3 && c == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {  // UTF-8 BOM
      i += 2;
      continue;
    }
    return c == '{';
  }
  return false;
}

// The 4-digit code shown on Track Rat's screen, needed to install firmware
// or replace tracks.json, so only someone looking at the screen can. After
// MAX_TRIES wrong codes, uploads stay locked until Wi-Fi is turned on again.
class UploadCode {
public:
  static const int MAX_TRIES = 5;

  void reset(uint32_t random) {
    code = random % 10000;
    wrong = 0;
  }
  int value() const { return code; }
  bool locked() const { return wrong >= MAX_TRIES; }

  // given: exactly 4 digits, e.g. "0042". Doesn't count tries.
  bool matches(const char* given) const {
    if (!given || strlen(given) != 4) return false;
    int v = 0;
    for (int i = 0; i < 4; i++) {
      if (given[i] < '0' || given[i] > '9') return false;
      v = v * 10 + (given[i] - '0');
    }
    return v == code;
  }

  // An upload asking to start: counts wrong codes, refuses everything once locked.
  bool check(const char* given) {
    if (locked()) return false;
    if (matches(given)) return true;
    wrong++;
    return false;
  }

private:
  int code = -1;  // no code until reset(): nothing matches
  int wrong = 0;
};

// One upload of each kind at a time. The request that sent the first piece
// owns it until it finishes or its connection drops; any other is refused.
class UploadSlot {
public:
  bool claim(const void* who) {
    if (owner && owner != who) return false;
    owner = who;
    return true;
  }
  bool owns(const void* who) const { return owner && owner == who; }
  bool busy() const { return owner != nullptr; }
  void release(const void* who) {
    if (owner == who) owner = nullptr;
  }

private:
  const void* owner = nullptr;
};

}  // namespace portal

#endif  // PORTAL_LOGIC_H
