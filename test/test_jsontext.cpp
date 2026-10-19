/**
 * Host-side tests for the JSON text helpers (JsonText.h).
 *
 *   g++ -std=c++17 -Iinclude test/test_jsontext.cpp -o /tmp/jt && /tmp/jt
 */

#include <cstdio>
#include <cstring>
#include <string>
#include "JsonText.h"
#include "check.h"

using namespace jsontext;

static size_t count(const char* s) { return countValues(s, strlen(s)); }

static std::string byLine(const std::string& compact, int depth) {
  std::string out;
  LineBreaker lines(depth);
  for (char c : compact) {
    if (lines.breakBefore(c)) out += '\n';
    out += c;
  }
  return out;
}

int main() {
  printf("JSON text tests\n");

  // One per array element or object member, not counting the outermost value
  CHECK(count("{}") == 0, "{}");
  CHECK(count("[]") == 0, "[]");
  CHECK(count("[ ]") == 0, "[ ]");
  CHECK(count("[1]") == 1, "[1]");
  CHECK(count("[1,2,3]") == 3, "[1,2,3]");
  CHECK(count("{\"a\":1,\"b\":[1,2],\"c\":{}}") == 5, "nested: %zu", count("{\"a\":1,\"b\":[1,2],\"c\":{}}"));
  CHECK(count("[[],{},[[]]]") == 4, "empty ones inside: %zu", count("[[],{},[[]]]"));
  CHECK(count("\"text\"") == 0 && count("42") == 0, "a lone value");
  // Commas, brackets and escaped quotes inside strings don't count
  const char* tricky = "[\"a,b]\",\"c\\\"],[d\",{\"x\\\\\":\"y,{\"}]";
  CHECK(count(tricky) == 4, "strings: %zu", count(tricky));
  // Whitespace and line breaks (pretty-printed by Python's json.dump) don't change it
  const char* pretty =
      "{\n \"tracks\": [\n  {\n   \"id\": \"a\",\n   \"map\": [\n    [\n     [\n      45.1,\n      -76.2\n     ]\n"
      "    ]\n   ]\n  },\n  {\r\n\t\"id\": \"b\", \"layouts\": [ ]\n  }\n ]\n}\n";
  const char* compact = "{\"tracks\":[{\"id\":\"a\",\"map\":[[[45.1,-76.2]]]},{\"id\":\"b\",\"layouts\":[]}]}";
  CHECK(count(pretty) == count(compact) && count(compact) == 11, "pretty %zu, compact %zu", count(pretty),
        count(compact));
  printf("  counting values: ok\n");

  // One track per line
  std::string lines = byLine(compact, 2);
  CHECK(lines ==
            "{\"tracks\":[\n{\"id\":\"a\",\"map\":[[[45.1,-76.2]]]},\n{\"id\":\"b\",\"layouts\":[]}\n]}",
        "by line: %s", lines.c_str());
  CHECK(count(lines.c_str()) == count(compact), "same values after breaking lines");
  CHECK(byLine("{\"tracks\":[]}", 2) == "{\"tracks\":[]}", "empty list stays []");
  CHECK(byLine("{\"tracks\":[{}],\"x\":1}", 2) == "{\"tracks\":[\n{}\n],\"x\":1}", "%s",
        byLine("{\"tracks\":[{}],\"x\":1}", 2).c_str());
  // Brackets, commas and quotes inside strings don't start lines
  std::string s = byLine("{\"t\":[\"a\\\"],[b\",\"c,d\\\\\",\"{\"]}", 2);
  CHECK(s == "{\"t\":[\n\"a\\\"],[b\",\n\"c,d\\\\\",\n\"{\"\n]}", "strings: %s", s.c_str());
  // Other top-level members that are objects get a line per member too
  s = byLine("{\"m\":{\"a\":1,\"b\":2},\"n\":3}", 2);
  CHECK(s == "{\"m\":{\n\"a\":1,\n\"b\":2\n},\"n\":3}", "object: %s", s.c_str());
  CHECK(byLine(compact, 0) == compact, "depth 0: unchanged");
  printf("  line per element: ok\n");

  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
