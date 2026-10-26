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

// The session list: {"files":[{"name":"a.json","size":1234,"fs":"sd"},...],"tzMin":-240}
// A card can hold thousands of files, so the reply is built a few files at a
// time as it's sent (fill()), never all at once. Str: Arduino String or std::string.
template <class Str>
class SessionListJson {
public:
  SessionListJson() { text += "{\"files\":["; }

  // fsName: "sd" or "flash"
  void addFile(const char* name, uint32_t size, const char* fsName) {
    char num[12];
    snprintf(num, sizeof(num), "%lu", (unsigned long)size);
    if (count++) text += ',';
    text += "{\"name\":";
    appendJsonString(text, name);
    text += ",\"size\":";
    text += num;
    text += ",\"fs\":\"";
    text += fsName;
    text += "\"}";
  }

  // tzMin: the time zone the file names are written in
  void finish(int tzMin) {
    char num[12];
    snprintf(num, sizeof(num), "%d", tzMin);
    text += "],\"tzMin\":";
    text += num;
    text += '}';
    finished = true;
  }

  // The reply's next piece: up to maxLen (> 0) bytes into buf. next() reads
  // one directory entry and adds it (or nothing, for a folder), or calls
  // finish() when there are none left; it's called until buf is full or
  // maxReads times. 0 ends the reply, so it's returned only once everything
  // has been sent: until then this reads on until it has something.
  template <class Next>
  size_t fill(uint8_t* buf, size_t maxLen, int maxReads, Next next) {
    size_t n = take(buf, maxLen);
    for (int reads = 0; n < maxLen && !finished && (n == 0 || reads < maxReads); reads++) {
      next();
      n += take(buf + n, maxLen - n);
    }
    return n;
  }

private:
  // What's waiting to be sent, as much as fits
  size_t take(uint8_t* buf, size_t maxLen) {
    size_t n = text.length() - sent;
    if (n > maxLen) n = maxLen;
    memcpy(buf, text.c_str() + sent, n);
    sent += n;
    if (sent == text.length()) {  // all sent: reuse the space
      text = "";
      sent = 0;
    }
    return n;
  }

  Str text;
  size_t sent = 0;
  uint32_t count = 0;
  bool finished = false;
};

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

// The 4-digit code shown on Track Rat's screen, needed for every change from
// the page (settings, notes, tracks, firmware, restart...), so only someone
// who can see the screen can make one.
//
// The network is open, so a code that someone nearby sniffed must not stay good:
//  - It is good for one change. spend() draws a new one once a change has gone
//    through, and a check that only looks (matches()) doesn't use it up.
//  - After MAX_TRIES wrong codes, every code is refused for a while: 30 s, then
//    twice as long each time (up to 8 minutes), back to 30 s after a correct
//    code. A new code is drawn at each lockout, so guesses don't add up.
// Times are millis() from the caller, so this is tested on a PC.
class UploadCode {
public:
  static const int MAX_TRIES = 5;
  static const uint32_t FIRST_LOCK_MS = 30UL * 1000;
  static const uint32_t MAX_LOCK_MS = 8UL * 60 * 1000;

  void reset(uint32_t random) {
    draw(random);
    wrong = 0;
    strikes = 0;
    lockedOut = false;
  }
  int value() const { return code; }

  // A new code, because the last one was used for a change. A correct code also
  // forgives earlier wrong ones, which check() already did.
  void spend(uint32_t random) { draw(random); }

  bool locked(uint32_t now) const { return lockedOut && (int32_t)(until - now) > 0; }
  // How long until codes are accepted again (0 if they are)
  uint32_t waitMs(uint32_t now) const { return locked(now) ? until - now : 0; }

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

  // A change asking to start: counts wrong codes, refuses everything during a lockout.
  // random: for the new code a lockout draws.
  bool check(const char* given, uint32_t now, uint32_t random) {
    if (locked(now)) return false;
    if (lockedOut) {  // it has run out: a fresh set of tries
      lockedOut = false;
      wrong = 0;
    }
    if (matches(given)) {
      wrong = 0;
      strikes = 0;
      return true;
    }
    if (++wrong >= MAX_TRIES) {
      uint32_t ms = FIRST_LOCK_MS;
      for (int i = 0; i < strikes && ms < MAX_LOCK_MS; i++) ms *= 2;
      if (ms > MAX_LOCK_MS) ms = MAX_LOCK_MS;
      if (strikes < 30) strikes++;
      until = now + ms;
      lockedOut = true;
      draw(random);
    }
    return false;
  }

private:
  // Never the same code twice in a row, so "a new code" is always true
  void draw(uint32_t random) {
    int next = random % 10000;
    if (next == code) next = (next + 1) % 10000;
    code = next;
  }

  int code = -1;  // no code until reset(): nothing matches
  int wrong = 0;
  int strikes = 0;  // lockouts since the last correct code
  bool lockedOut = false;
  uint32_t until = 0;
};

// "Too many wrong codes. Wait 30 s, ..." for a lockout with `ms` left. Long waits are given in minutes.
inline void lockText(uint32_t ms, char* buf, size_t len) {
  uint32_t s = (ms + 999) / 1000;
  if (s < 1) s = 1;
  if (s >= 120) snprintf(buf, len, "Too many wrong codes. Wait %lu min, then enter the new code on Track Rat's screen.", (unsigned long)((s + 59) / 60));
  else snprintf(buf, len, "Too many wrong codes. Wait %lu s, then enter the new code on Track Rat's screen.", (unsigned long)s);
}

// --- What a POST may be before its body is read -------------------------------
//
// The page is on http://192.168.4.1 and sends this as its Origin. Another web page open
// on the phone could post to the board too, so a POST that names any other origin is refused.
// A request with no Origin (not from a browser) is left to the code.
inline bool isPortalOrigin(const char* origin, const char* ip) {
  static const char scheme[] = "http://";
  if (strncmp(origin, scheme, sizeof(scheme) - 1) != 0) return false;
  return isPortalHost(origin + sizeof(scheme) - 1, ip);
}

static const size_t TRACKS_MAX_BYTES = 2 * 1024 * 1024;  // a tracks.json upload
static const size_t FORM_MAX_BYTES = 4096;                // every other POST (a note is up to 1000 bytes, +30% escaped)
static const size_t MULTIPART_SLACK = 1024;               // a file's body is a little more than the file

// The most a POST to `url` may be in Content-Length, before reading any of it.
// firmwareMax: the app slot.
inline size_t bodyLimit(const char* url, size_t firmwareMax) {
  if (strcmp(url, "/api/tracks") == 0) return TRACKS_MAX_BYTES + MULTIPART_SLACK;
  if (strcmp(url, "/api/firmware") == 0) return firmwareMax + MULTIPART_SLACK;
  return FORM_MAX_BYTES;
}

// Is there room for an upload of `bytes` on a card or flash with `freeBytes` free? The new file sits next to the old
// one until it replaces it, and the file system needs a little for itself.
inline bool roomFor(uint64_t freeBytes, uint64_t bytes) {
  static const uint64_t MARGIN = 64 * 1024;
  return freeBytes >= bytes + MARGIN;
}

// 0 to let a POST go on to its handler, else the status to refuse it with: 403 from another
// site, 411 with no length (chunked bodies aren't read), 413 too big. origin: null if there was none.
inline int postRefusal(const char* url, size_t contentLength, bool chunked, const char* origin, const char* ip,
                       size_t firmwareMax) {
  if (origin && !isPortalOrigin(origin, ip)) return 403;
  if (chunked) return 411;
  if (contentLength > bodyLimit(url, firmwareMax)) return 413;
  return 0;
}

// Session downloads at once: more would keep the logger waiting for the card.
class DownloadGate {
public:
  static const int MAX_AT_ONCE = 2;
  bool enter() {
    if (active >= MAX_AT_ONCE) return false;
    active++;
    return true;
  }
  void leave() {
    if (active > 0) active--;
  }
  int count() const { return active; }

private:
  int active = 0;
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
