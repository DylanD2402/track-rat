/**
 * Track Rat - JSON text helpers
 *
 * Small scans of JSON text used when reading and saving tracks.json. No
 * Arduino code, so test/test_jsontext.cpp checks them on a PC.
 */

#ifndef JSON_TEXT_H
#define JSON_TEXT_H

#include <cstddef>

namespace jsontext {

// Values in the text, not counting the outermost one: one per array element
// or object member, which is one ArduinoJson slot each. Brackets and commas
// inside strings don't count.
inline size_t countValues(const char* s, size_t n) {
  size_t count = 0;
  bool inString = false, escaped = false, opened = false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') inString = false;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
    if (opened && c != ']' && c != '}') count++;  // first value in an array or object that isn't empty
    opened = c == '[' || c == '{';
    if (c == ',') count++;
    else if (c == '"') inString = true;
  }
  return count;
}

// Fed compact JSON a character at a time, says where to start a new line so
// that each element of the arrays and objects `depth` levels down gets one
// (depth 2: each track of {"tracks":[...]}). Empty ones stay as [] or {}.
class LineBreaker {
public:
  explicit LineBreaker(int depth) : depth_(depth) {}

  // True if a '\n' goes before c.
  bool breakBefore(char c) {
    bool closing = !inString_ && (c == ']' || c == '}');
    bool brk = pending_ ? !closing : closing && level_ == depth_;
    pending_ = false;
    if (inString_) {
      if (escaped_) escaped_ = false;
      else if (c == '\\') escaped_ = true;
      else if (c == '"') inString_ = false;
    } else if (c == '"') {
      inString_ = true;
    } else if (c == '[' || c == '{') {
      pending_ = ++level_ == depth_;
    } else if (closing) {
      level_--;
    } else if (c == ',') {
      pending_ = level_ == depth_;
    }
    return brk;
  }

private:
  int depth_;
  int level_ = 0;
  bool inString_ = false, escaped_ = false;
  bool pending_ = false;  // a line starts at the next character, unless it closes an empty array or object
};

}  // namespace jsontext

#endif  // JSON_TEXT_H
