/**
 * Host-side tests for OpenMarker.h.
 *
 *   g++ -std=c++17 -Iinclude test/test_openmarker.cpp -o /tmp/om && /tmp/om
 */

#include "OpenMarker.h"
#include "check.h"

int main() {
  openmarker::Bases b;
  openmarker::add(b, "/sessions/a");
  openmarker::add(b, "/sessions/b");
  openmarker::add(b, "/sessions/a");  // no repeats
  openmarker::add(b, "");
  CHECK(b.size() == 2, "two names, got %d", (int)b.size());
  CHECK(openmarker::format(b) == "/sessions/a\n/sessions/b\n", "format");

  openmarker::Bases back = openmarker::parse(openmarker::format(b));
  CHECK(back == b, "round trip");

  CHECK(openmarker::parse("").empty(), "empty text");
  CHECK(openmarker::parse("\n\n \r\n").empty(), "blank lines");
  CHECK(openmarker::parse("/sessions/a\r\n/sessions/a\r\n/sessions/c\n").size() == 2, "CRLF and repeats");
  CHECK(openmarker::parse("/sessions/a\n/sessions/cut").size() == 1, "a cut-off last line is dropped");

  CHECK(openmarker::remove(b, "/sessions/a"), "removed");
  CHECK(!openmarker::remove(b, "/sessions/a"), "not there twice");
  CHECK(b.size() == 1 && b[0] == "/sessions/b", "b is left");
  CHECK(openmarker::format(openmarker::Bases()).empty(), "nothing open: an empty file (the marker is deleted)");

  CHECK(openmarker::baseOf("/sessions/x_y.json") == "/sessions/x_y", "json base");
  CHECK(openmarker::baseOf("/sessions/x_y.vbo") == "/sessions/x_y", "vbo base");
  CHECK(openmarker::baseOf("/sessions/x_y.json.tmp").empty(), "tmp is not a base");
  CHECK(openmarker::baseOf(".json").empty(), "only an extension");
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("test_openmarker: all passed\n");
  return 0;
}
