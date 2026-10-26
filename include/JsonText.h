/**
 * Track Rat - JSON text helpers
 *
 * Small scans of JSON text used when reading and saving tracks.json. No
 * Arduino code, so test/test_jsontext.cpp checks them on a PC.
 */

#ifndef JSON_TEXT_H
#define JSON_TEXT_H

#include <cstddef>
#include <string>

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

// Cuts the objects out of the first array of a {"tracks": [...]} file, fed a character at a time, so a big file
// (a world tile, ROADMAP Phase 10) is read in pieces and only one track is parsed at once. feed() returns true
// when an object has just ended: element() is its text, until the next feed(). Only objects count; the file's
// other members are skipped. done() once the array has closed.
class ElementSplitter {
public:
  bool feed(char c) {
    if (done_) return false;
    if (finished_) {  // the last call returned an element: it has been used
      elem_.clear();
      finished_ = false;
    }
    if (inString_) {
      if (collecting_) elem_ += c;
      if (escaped_) escaped_ = false;
      else if (c == '\\') escaped_ = true;
      else if (c == '"') inString_ = false;
      return false;
    }
    if (c == '"') {
      inString_ = true;
    } else if (c == '{' || c == '[') {
      depth_++;
      if (c == '[' && depth_ == 2) arrayOpen_ = true;
      if (c == '{' && depth_ == 3 && arrayOpen_) {
        collecting_ = true;
        elem_.clear();
      }
    } else if (c == '}' || c == ']') {
      if (c == ']' && depth_ == 2 && arrayOpen_) done_ = true;
      if (c == '}' && depth_ == 3 && collecting_) {
        elem_ += c;
        collecting_ = false;
        finished_ = true;
        depth_--;
        return true;
      }
      depth_--;
    }
    if (collecting_) elem_ += c;
    return false;
  }
  std::string& element() { return elem_; }  // (a caller may parse it in place, which changes it)
  bool done() const { return done_; }

private:
  std::string elem_;
  int depth_ = 0;
  bool inString_ = false, escaped_ = false, arrayOpen_ = false, collecting_ = false, finished_ = false, done_ = false;
};

}  // namespace jsontext

#endif  // JSON_TEXT_H
