/**
 * Host-side tests for the logbook's note helpers (Logbook.h).
 *
 *   g++ -std=c++17 -Iinclude test/test_logbook.cpp -o /tmp/lb && /tmp/lb
 */

#include <cstdio>
#include <cstring>
#include <string>
#include "Logbook.h"
#include "check.h"

using namespace logbook;

int main() {
  printf("Logbook tests\n");

  // Session base names from the page
  CHECK(validBase("20261018_093012_calabogie"), "plain base");
  CHECK(validBase("SIM_12_bench"), "simulated base");
  CHECK(!validBase(""), "empty");
  CHECK(!validBase("../tracks"), "parent folder");
  CHECK(!validBase("a/b"), "folder");
  CHECK(!validBase("a\\b"), "backslash folder");
  CHECK(!validBase("20261018_093012_calabogie.vbo"), "has an extension");
  CHECK(!validBase("x\"y"), "quote");
  CHECK(!validBase("x\ny"), "newline");
  CHECK(!validBase(std::string(101, 'a').c_str()), "too long");
  CHECK(validBase(std::string(100, 'a').c_str()), "100 is fine");
  CHECK(notePath("/sessions", "20261018_093012_calabogie") == "/sessions/20261018_093012_calabogie_note.json",
        "path %s", notePath("/sessions", "20261018_093012_calabogie").c_str());

  // Cleaning what was typed
  CHECK(clean("Fronts 32, rears 30") == "Fronts 32, rears 30", "unchanged");
  CHECK(clean("a\r\nb\rc\n") == "a\nb\nc", "line ends: %s", clean("a\r\nb\rc\n").c_str());
  CHECK(clean("  \n\n hi \n\n ") == "hi", "trimmed: [%s]", clean("  \n\n hi \n\n ").c_str());
  CHECK(clean("tab\there\x01") == "tab here", "control characters: [%s]", clean("tab\there\x01").c_str());
  CHECK(clean("   \r\n  ").empty(), "blank note is empty");
  CHECK(clean(std::string(MAX_NOTE + 50, 'x')).size() == MAX_NOTE, "cut to MAX_NOTE");
  {
    // "é" is two bytes; a cut through it backs off to before it
    std::string s(MAX_NOTE - 1, 'x');
    s += "\xC3\xA9tail";
    std::string c = clean(s);
    CHECK(c.size() == MAX_NOTE - 1, "UTF-8 not split: %zu", c.size());
    std::string t(MAX_NOTE - 2, 'x');
    t += "\xC3\xA9tail";
    CHECK(clean(t).size() == MAX_NOTE, "whole character kept: %zu", clean(t).size());
  }

  // [comments] lines
  CHECK(commentLines("") == "", "no note, no lines");
  CHECK(commentLines("Wet track") == "Note: Wet track\r\n", "one line");
  CHECK(commentLines("Fronts 32\nRears 30") == "Note: Fronts 32\r\nNote: Rears 30\r\n", "two lines");
  CHECK(commentLines("A\n\nB") == "Note: A\r\nNote: \r\nNote: B\r\n", "blank line inside kept: %s",
        commentLines("A\n\nB").c_str());
  CHECK(commentLines("[header]") == "Note:  [header]\r\n", "section-like line: %s", commentLines("[header]").c_str());
  CHECK(commentLines(clean("x\r\n\r\n")) == "Note: x\r\n", "cleaned note");

  // Where they go in a .vbo
  const char vbo[] =
      "File created on 18/10/2026 at 09:30:12\r\n\r\n[header]\r\nsatellites\r\ntime\r\n\r\n"
      "[comments]\r\nTrack: Calabogie\r\n\r\n[column names]\r\nsats time\r\n";
  size_t at = commentsEnd(vbo, strlen(vbo));
  CHECK(at && memcmp(vbo + at, "Track: Calabogie", 16) == 0, "after [comments]: %zu", at);
  CHECK(commentsEnd(vbo, at - 1) == 0, "not all of [comments] in the head");
  const char none[] = "[header]\r\nsats\r\n\r\n[data]\r\n";
  CHECK(commentsEnd(none, strlen(none)) == 0, "no [comments]");
  const char inside[] = "x[comments]\r\n";
  CHECK(commentsEnd(inside, strlen(inside)) == 0, "only at the start of a line");
  {
    std::string out(vbo, at);
    out += commentLines("Wet");
    out += vbo + at;
    CHECK(out.find("[comments]\r\nNote: Wet\r\nTrack: Calabogie\r\n") != std::string::npos, "spliced: %s",
          out.c_str());
  }

  // The note file
  CHECK(noteJson("Wet") == "{\"note\":\"Wet\"}\n", "plain: %s", noteJson("Wet").c_str());
  CHECK(noteJson("a \"b\"\nc\\d") == "{\"note\":\"a \\\"b\\\"\\nc\\\\d\"}\n", "escaped: %s",
        noteJson("a \"b\"\nc\\d").c_str());
  CHECK(noteJson("caf\xC3\xA9") == "{\"note\":\"caf\xC3\xA9\"}\n", "UTF-8 as it is");

  // A download put together a piece at a time, in every chunk size, matches the whole
  {
    const std::string file = vbo;
    const size_t at2 = commentsEnd(file.data(), file.size());
    const std::string lines = commentLines("Fronts 32\nRears 30");
    const std::string want = file.substr(0, at2) + lines + file.substr(at2);
    auto read = [&](size_t off, unsigned char* dst, size_t n) {
      if (off >= file.size()) return (size_t)0;
      if (n > file.size() - off) n = file.size() - off;
      memcpy(dst, file.data() + off, n);
      return n;
    };
    for (size_t chunk = 1; chunk <= want.size() + 3; chunk++) {
      std::string got;
      unsigned char buf[512];
      for (;;) {
        size_t n = splicedChunk(buf, chunk, got.size(), file.size(), at2, lines, read);
        if (!n) break;
        got.append((const char*)buf, n);
      }
      if (got != want) {
        CHECK(false, "chunks of %zu: got %zu bytes", chunk, got.size());
        break;
      }
    }
    // A file that stops reading early ends the download short, never loops
    auto broken = [&](size_t off, unsigned char* dst, size_t n) { return off < 10 ? read(off, dst, 10 - off < n ? 10 - off : n) : (size_t)0; };
    unsigned char buf[512];
    CHECK(splicedChunk(buf, sizeof(buf), 0, file.size(), at2, lines, broken) == 10, "short read");
  }

  if (failures) {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("All logbook tests passed\n");
  return 0;
}
