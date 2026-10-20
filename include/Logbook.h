/**
 * Track Rat - the logbook: a note for each session
 *
 * A note (tyre pressures, weather, setup, an instructor's advice) is typed
 * on the Wi-Fi page and saved next to the session as <base>_note.json
 * ({"note": "..."}), where <base> is the session's file name without its
 * extension. The session list shows it, and a .vbo downloaded with a note
 * carries it in its [comments], so Circuit Tools shows it too.
 *
 * Voice notes (the board's microphone, an ES8311 codec) need bringing up on
 * the hardware first: VOICE_NOTES_ENABLED in Config.h stays 0 until then.
 *
 * No Arduino dependencies: tested in test/test_logbook.cpp.
 */

#ifndef LOGBOOK_H
#define LOGBOOK_H

#include <stddef.h>
#include <string.h>
#include <string>

namespace logbook {

constexpr size_t MAX_NOTE = 1000;  // characters (bytes of UTF-8) kept
constexpr const char* NOTE_SUFFIX = "_note.json";

// A session's base name as the page sends it ("20261018_093012_calabogie"):
// a plain file name with no folder, no "..", and no extension of its own.
inline bool validBase(const char* base) {
  const size_t n = strlen(base);
  if (n == 0 || n > 100 || strstr(base, "..")) return false;
  for (size_t i = 0; i < n; i++) {
    const unsigned char c = base[i];
    if (c < 0x20 || c == '/' || c == '\\' || c == '"' || c == ':' || c == '.' || c == 0x7F) return false;
  }
  return true;
}

inline std::string notePath(const char* sessionDir, const char* base) {
  return std::string(sessionDir) + "/" + base + NOTE_SUFFIX;
}

// The note as kept: CRLF and lone CRs become LF, other control characters
// spaces, trailing space and blank lines trimmed, at most MAX_NOTE bytes
// (never cutting a UTF-8 character in half).
inline std::string clean(const std::string& in) {
  std::string out;
  out.reserve(in.size() < MAX_NOTE ? in.size() : MAX_NOTE);
  for (size_t i = 0; i < in.size(); i++) {
    char c = in[i];
    if (c == '\r') {
      if (i + 1 < in.size() && in[i + 1] == '\n') continue;
      c = '\n';
    } else if (c != '\n' && (unsigned char)c < 0x20) {
      c = ' ';
    } else if (c == 0x7F) {
      c = ' ';
    }
    out += c;
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
  size_t start = 0;
  while (start < out.size() && (out[start] == ' ' || out[start] == '\n')) start++;
  out.erase(0, start);
  if (out.size() > MAX_NOTE) {
    size_t cut = MAX_NOTE;
    while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80) cut--;  // back to a character's first byte
    out.resize(cut);
  }
  return out;
}

// The note as [comments] lines of a .vbo: "Note: ..." for each line of it
// (a line starting with '[' would look like a section to a reader, so it
// gets a space in front). Empty for an empty note.
inline std::string commentLines(const std::string& note) {
  std::string out;
  size_t i = 0;
  while (i <= note.size()) {
    size_t j = note.find('\n', i);
    if (j == std::string::npos) j = note.size();
    std::string line = note.substr(i, j - i);
    if (!line.empty() || !out.empty()) {
      out += "Note: ";
      if (!line.empty() && line[0] == '[') out += ' ';
      out += line;
      out += "\r\n";
    }
    i = j + 1;
  }
  return out;
}

// Where the note's lines go in a .vbo: the byte just after the "[comments]"
// line in the start of the file (its first `len` bytes). 0 if not found
// there (then the note isn't added).
inline size_t commentsEnd(const char* head, size_t len) {
  static const char KEY[] = "[comments]\r\n";
  const size_t k = sizeof(KEY) - 1;
  for (size_t i = 0; i + k <= len; i++)
    if (memcmp(head + i, KEY, k) == 0 && (i == 0 || head[i - 1] == '\n')) return i + k;
  return 0;
}

}  // namespace logbook

#endif  // LOGBOOK_H
