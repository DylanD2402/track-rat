/**
 * Host-side tests for TextFold.h.
 *
 *   g++ -std=c++17 -Iinclude test/test_textfold.cpp -o /tmp/tfold && /tmp/tfold
 */

#include <cstring>
#include <string>
#include "TextFold.h"
#include "check.h"

static void is(const char* in, const char* want) {
  char b[128];
  foldText(in, b, sizeof(b));
  CHECK(strcmp(b, want) == 0, "fold(\"%s\") = \"%s\", want \"%s\"", in, b, want);
}

int main() {
  is("Autodrome La Chaudi\xC3\xA8re", "Autodrome La Chaudiere");   // French
  is("\xC3\x89\xC3\xA9 \xC3\xAA \xC3\xAB \xC3\xA0 \xC3\xA7 \xC3\xB4 \xC3\xBC", "Ee e e a c o u");
  is("Jos\xC3\xA9 Pe\xC3\xB1" "a, Ni\xC3\xB1o", "Jose Pena, Nino");   // Spanish
  is("M\xC3\xBCller Stra\xC3\x9F" "e", "Muller Strasse");   // German
  is("\xC5\x81\xC3\xB3" "d\xC5\xBA \xC5\x9A" "wi\xC4\x99" "tokrzyska \xC5\xBB", "Lodz Swietokrzyska Z");   // Polish
  is("\xC3\x85land \xC3\x86r\xC3\xB8 \xC3\x98rsted \xC3\xBE\xC3\xB3r", "Aland AEro Orsted thor");   // Nordic
  is("\xC5\x93uvre \xC5\x92 \xC4\x91 \xC4\xB1", "oeuvre OE d i");
  is("plain ASCII 123 -.,", "plain ASCII 123 -.,");
  is("", "");

  // the characters the fonts draw stay as they are
  const char* extra =
      "\xC2\xB1 \xC2\xB0 \xC2\xB7 \xE2\x80\x93 \xE2\x80\x94 \xE2\x80\xA6 \xE2\x80\xB9 \xE2\x80\xBA "
      "\xE2\x88\x92 \xC3\x97 \xC2\xBC \xC2\xBD \xE2\x85\x9B";
  is(extra, extra);

  // punctuation
  is("it\xE2\x80\x99s \xE2\x80\x98x\xE2\x80\x99", "it's 'x'");
  is("\xE2\x80\x9Cq\xE2\x80\x9D \xC2\xABq\xC2\xBB", "\"q\" \"q\"");
  is("a\xC2\xA0" "b", "a b");
  is("\xE2\x80\xA2 item", "\xC2\xB7 item");
  is("a\xE2\x80\x90" "b", "a-b");

  // other scripts and emoji: one "?" per run
  is("\xE6\x9D\xB1\xE4\xBA\xAC", "?");
  is("ab\xD0\x9C\xD0\xBE\xD1\x81\xD0\xBA\xD0\xB2\xD0\xB0" "cd", "ab?cd");
  is("fast \xF0\x9F\x8F\x8E x", "fast ? x");

  // broken UTF-8 neither crashes nor reads past the end
  is("a\xC3", "a?");
  is("a\x80" "b", "a?b");
  is("\xC0\xAF", "?");             // overlong
  is("\xED\xA0\x80", "?");         // surrogate
  is("\xF4\x90\x80\x80", "?");     // above U+10FFFF
  is("\xE2\x82", "?");             // truncated 3-byte

  // a tight buffer stays terminated
  char small[6];
  const size_t n = foldText("Cha\xC3\xAEne longue", small, sizeof(small));
  CHECK(n == 5 && strcmp(small, "Chain") == 0, "small buffer: %zu \"%s\"", n, small);
  CHECK(foldText("x", small, 0) == 0, "zero cap");

  CHECK(textIsAscii("abc"), "ascii");
  CHECK(!textIsAscii("ab\xC3\xA9"), "not ascii");

  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("test_textfold: all passed\n");
  return 0;
}
