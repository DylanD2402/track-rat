/**
 * Track Rat - the marker for sessions still open
 *
 * "/sessions/.open" lists the base names ("/sessions/20261025_101500_calabogie", no extension) of sessions
 * whose log was started and whose summary isn't finished yet. After a power cut, boot recovery looks at just
 * those instead of checking every .vbo on the card. One name per line. Pure text handling, no Arduino
 * dependencies, so it can be unit-tested on a PC (test/test_openmarker.cpp).
 */

#ifndef OPEN_MARKER_H
#define OPEN_MARKER_H

#include <string>
#include <vector>

namespace openmarker {

constexpr const char* FILE_NAME = ".open";  // inside the sessions folder

using Bases = std::vector<std::string>;

inline bool has(const Bases& b, const std::string& base) {
  for (const std::string& x : b)
    if (x == base) return true;
  return false;
}
inline void add(Bases& b, const std::string& base) {
  if (!base.empty() && !has(b, base)) b.push_back(base);
}
inline bool remove(Bases& b, const std::string& base) {
  for (size_t i = 0; i < b.size(); i++)
    if (b[i] == base) {
      b.erase(b.begin() + i);
      return true;
    }
  return false;
}

inline std::string format(const Bases& b) {
  std::string out;
  for (const std::string& x : b) out += x + "\n";
  return out;
}

// Lines, without \r, blanks or repeats. A cut-off last line is a name that never was finished: it is kept
// only if the text ends in a newline.
inline Bases parse(const std::string& text) {
  Bases out;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) break;
    std::string line = text.substr(pos, nl - pos);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    add(out, line);
    pos = nl + 1;
  }
  return out;
}

// "/sessions/a.json" or "/sessions/a.vbo" -> "/sessions/a"; "" for any other file
inline std::string baseOf(const std::string& path) {
  for (const char* ext : {".json", ".vbo"}) {
    const std::string e = ext;
    if (path.size() > e.size() && path.compare(path.size() - e.size(), e.size(), e) == 0)
      return path.substr(0, path.size() - e.size());
  }
  return std::string();
}

}  // namespace openmarker

#endif  // OPEN_MARKER_H
