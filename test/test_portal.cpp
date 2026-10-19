/**
 * Host-side tests for the Wi-Fi page's request rules (PortalLogic.h).
 *
 *   g++ -std=c++17 -Iinclude test/test_portal.cpp -o /tmp/wp && /tmp/wp
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "PortalLogic.h"
#include "check.h"

using namespace portal;

static bool jsonObj(const char* s) { return looksLikeJsonObject((const uint8_t*)s, strlen(s)); }

static int tz(const char* s, bool* ok) {
  int v = 12345;
  *ok = parseTimeZone(s, &v);
  return v;
}

// A directory entry, as the session list reads it
struct Entry {
  const char* name;
  uint32_t size;
  const char* fs;
  bool folder;
};

struct ListRun {
  std::string text;
  bool ok = true;  // no piece too big, every entry read, ended by a 0
  int pieces = 0;
  int mostReads = 0;       // in one piece
  int readsAfterText = 0;  // reads past the limit in a piece that already had something to send
};

// Sends the list as the web server does: a piece of up to `piece` bytes at a time until it gets 0
static ListRun sendList(const std::vector<Entry>& folder, int tzMin, size_t piece, int maxReads) {
  SessionListJson<std::string> json;
  ListRun r;
  size_t i = 0;
  int reads = 0;
  bool added = false;
  auto next = [&]() {
    if (++reads > maxReads && added) r.readsAfterText++;
    if (i == folder.size()) {
      json.finish(tzMin);
      added = true;
      return;
    }
    const Entry& e = folder[i++];
    if (!e.folder) {
      json.addFile(e.name, e.size, e.fs);
      added = true;
    }
  };
  std::vector<uint8_t> buf(piece + 1);
  for (;;) {
    reads = 0;
    added = false;
    buf[piece] = 0xEE;
    size_t n = json.fill(buf.data(), piece, maxReads, next);
    if (n > piece || buf[piece] != 0xEE) r.ok = false;
    if (reads > r.mostReads) r.mostReads = reads;
    if (n == 0) break;
    r.text.append((const char*)buf.data(), n);
    if (++r.pieces > 1000000) break;
  }
  if (i != folder.size() || r.pieces > 1000000) r.ok = false;
  return r;
}

int main() {
  printf("Wi-Fi page tests\n");

  // Download names: only plain files in /sessions
  const char* good[] = {"20261018_093012_calabogie.json", "20261018_093012_calabogie.vbo", "a",
                        "20261018_093012_my-track-2_perf.json", "session_bench.json", "name with spaces.txt"};
  for (const char* n : good) CHECK(isSafeFileName(n), "should allow '%s'", n);
  const char* bad[] = {"", "..", "../tracks.json", "a/../../b", "/etc/passwd", "sub/file.json", "a\\b",
                       "a..json", "x\"y", "c:boot", "tab\there", "new\nline"};
  for (const char* n : bad) CHECK(!isSafeFileName(n), "should refuse '%s'", n);
  std::string longName(121, 'a');
  CHECK(!isSafeFileName(longName.c_str()), "121-char name refused");
  CHECK(isSafeFileName(longName.substr(0, 120).c_str()), "120-char name allowed");
  printf("  file names: ok\n");

  CHECK(!strcmp(contentTypeFor("x.json"), "application/json"), "json");
  CHECK(!strcmp(contentTypeFor("x.vbo"), "text/plain"), "vbo");
  CHECK(!strcmp(contentTypeFor("x.csv"), "text/csv"), "csv");
  CHECK(!strcmp(contentTypeFor("x.gpx"), "application/gpx+xml"), "gpx");
  CHECK(!strcmp(contentTypeFor("x.bin"), "application/octet-stream"), "bin");
  CHECK(!strcmp(contentTypeFor("json"), "application/octet-stream"), "no dot");
  CHECK(!strcmp(contentTypeFor(""), "application/octet-stream"), "empty");
  CHECK(!strcmp(contentTypeFor("x.json.bak"), "application/octet-stream"), "bak");
  printf("  content types: ok\n");

  std::string j;
  appendJsonString(j, "plain");
  CHECK(j == "\"plain\"", "plain: %s", j.c_str());
  j.clear();
  appendJsonString(j, "a\"b\\c\nd");
  CHECK(j == "\"a\\\"b\\\\c d\"", "escaped: %s", j.c_str());
  j.clear();
  appendJsonString(j, "");
  CHECK(j == "\"\"", "empty: %s", j.c_str());
  j.clear();
  appendJsonString(j, "caf\xC3\xA9");  // UTF-8 passes through
  CHECK(j == "\"caf\xC3\xA9\"", "utf8: %s", j.c_str());
  printf("  JSON strings: ok\n");

  // Session list, sent in pieces of every size: always the same JSON
  std::vector<Entry> folder = {{"20261018_093012_calabogie.json", 1234, "sd", false},
                               {"System Volume Information", 0, "sd", true},
                               {"20261018_093012_calabogie.vbo", 3500000, "sd", false},
                               {"a\"b\\c", 0, "sd", false},
                               {"20261019_101500_calabogie.full_2.json.bak", 4294967295u, "flash", false}};
  const std::string want =
      "{\"files\":[{\"name\":\"20261018_093012_calabogie.json\",\"size\":1234,\"fs\":\"sd\"},"
      "{\"name\":\"20261018_093012_calabogie.vbo\",\"size\":3500000,\"fs\":\"sd\"},"
      "{\"name\":\"a\\\"b\\\\c\",\"size\":0,\"fs\":\"sd\"},"
      "{\"name\":\"20261019_101500_calabogie.full_2.json.bak\",\"size\":4294967295,\"fs\":\"flash\"}],\"tzMin\":-240}";
  for (size_t piece : {1, 2, 7, 64, 100, 2864}) {
    for (int reads : {1, 3, 32}) {
      ListRun r = sendList(folder, -240, piece, reads);
      CHECK(r.text == want, "piece %zu, %d reads: %s", piece, reads, r.text.c_str());
      CHECK(r.ok, "piece %zu, %d reads: a piece too big, or the list cut short", piece, reads);
    }
  }
  ListRun empty = sendList({}, 0, 1000, 32);
  CHECK(empty.text == "{\"files\":[],\"tzMin\":0}" && empty.ok, "no files: %s", empty.text.c_str());
  ListRun onlyFolder = sendList({{"x", 1, "sd", true}}, 330, 5, 1);
  CHECK(onlyFolder.text == "{\"files\":[],\"tzMin\":330}" && onlyFolder.ok, "only a folder: %s",
        onlyFolder.text.c_str());

  // A big folder: a piece stops reading once it has something to send and its reads are used up
  std::vector<std::string> names;
  for (int i = 0; i < 3000; i++) names.push_back("20261018_" + std::to_string(100000 + i) + "_track.vbo");
  std::vector<Entry> big;
  std::string bigWant = "{\"files\":[";
  for (int i = 0; i < 3000; i++) {
    big.push_back({names[i].c_str(), (uint32_t)i, "sd", false});
    bigWant += "{\"name\":\"" + names[i] + "\",\"size\":" + std::to_string(i) + ",\"fs\":\"sd\"},";
  }
  for (int i = 0; i < 200; i++) big.push_back({"subfolder", 0, "sd", true});  // nothing to send for a while
  big.push_back({"last.json", 7, "flash", false});
  bigWant += "{\"name\":\"last.json\",\"size\":7,\"fs\":\"flash\"}],\"tzMin\":60}";
  ListRun r = sendList(big, 60, 2864, 32);
  CHECK(r.ok && r.text == bigWant, "big folder (%zu bytes)", r.text.size());
  CHECK(r.readsAfterText == 0, "%d reads past the limit with text to send", r.readsAfterText);
  CHECK(r.mostReads <= 200 + 2, "most reads in one piece: %d", r.mostReads);
  CHECK(r.pieces >= 3000 / 32, "pieces: %d", r.pieces);
  printf("  session list: ok\n");

  bool ok;
  CHECK(tz("-240", &ok) == -240 && ok, "EDT");
  CHECK(tz("330", &ok) == 330 && ok, "India");
  CHECK(tz("+345", &ok) == 345 && ok, "Nepal with +");
  CHECK(tz("0", &ok) == 0 && ok, "UTC");
  CHECK(tz("-0", &ok) == 0 && ok, "-0");
  CHECK(tz("-720", &ok) == -720 && ok, "UTC-12 edge");
  CHECK(tz("840", &ok) == 840 && ok, "UTC+14 edge");
  const char* badTz[] = {"", "-", "+", "abc", "12a", "1.5", " 60", "60 ", "-721", "841", "99999999999", "--5"};
  for (const char* s : badTz) {
    int v = tz(s, &ok);
    CHECK(!ok && v == 12345, "should refuse tz '%s' (got ok=%d v=%d)", s, ok, v);
  }
  CHECK(!parseTimeZone(nullptr, nullptr), "null text");
  printf("  time zones: ok\n");

  CHECK(isPortalHost("192.168.4.1", "192.168.4.1"), "ip");
  CHECK(isPortalHost("192.168.4.1:80", "192.168.4.1"), "ip:80");
  CHECK(!isPortalHost("192.168.4.10", "192.168.4.1"), "longer ip");
  CHECK(!isPortalHost("192.168.4.1:8080", "192.168.4.1"), "other port");
  CHECK(!isPortalHost("captive.apple.com", "192.168.4.1"), "apple check");
  CHECK(!isPortalHost("connectivitycheck.gstatic.com", "192.168.4.1"), "android check");
  CHECK(!isPortalHost("", "192.168.4.1"), "no Host header");
  printf("  captive portal hosts: ok\n");

  char name[24];
  networkName(0x0000BEEF12345678ULL, name, sizeof(name));  // MAC 78:56:34:12:EF:BE
  CHECK(!strcmp(name, "TrackRat-EFBE"), "name %s", name);
  networkName(0, name, sizeof(name));
  CHECK(!strcmp(name, "TrackRat-0000"), "zero name %s", name);
  networkName(0xFFFFFFFFFFFFFFFFULL, name, sizeof(name));
  CHECK(!strcmp(name, "TrackRat-FFFF"), "ff name %s", name);
  char tiny[8];
  networkName(0x0000BEEF00000000ULL, tiny, sizeof(tiny));
  CHECK(strlen(tiny) == 7, "truncated safely: %s", tiny);
  printf("  network name: ok\n");

  CHECK(uploadPercent(0, 1000) == 0, "0%%");
  CHECK(uploadPercent(500, 1000) == 50, "50%%");
  CHECK(uploadPercent(1000, 1000) == 99, "complete is 99 until installed");
  CHECK(uploadPercent(2000, 1000) == 99, "over");
  CHECK(uploadPercent(5, 0) == 0, "unknown length");
  CHECK(uploadPercent(6000000, 6553600) == 91, "large %d", uploadPercent(6000000, 6553600));
  printf("  upload progress: ok\n");

  CHECK(jsonObj("{\"tracks\":[]}"), "object");
  CHECK(jsonObj("  \r\n\t{"), "leading blanks");
  CHECK(jsonObj("\xEF\xBB\xBF{\"tracks\":[]}"), "BOM");
  CHECK(!jsonObj("[1,2]"), "array");
  CHECK(!jsonObj(""), "empty");
  CHECK(!jsonObj("   "), "blank");
  CHECK(!jsonObj("\x7F" "ELF"), "binary");
  CHECK(!jsonObj("\xEF\xBB\xBF"), "BOM only");
  CHECK(!jsonObj("x{"), "text first");
  printf("  tracks.json check: ok\n");

  // Upload ownership, as the handlers use it
  UploadSlot slot;
  int a = 0, b = 0;  // two requests
  CHECK(!slot.busy() && !slot.owns(&a), "starts free");
  CHECK(slot.claim(&a) && slot.owns(&a) && slot.busy(), "A claims");
  CHECK(slot.claim(&a), "A claims again (same request)");
  CHECK(!slot.claim(&b) && !slot.owns(&b), "B refused while A uploads");
  slot.release(&b);  // B's disconnect must not free A's slot
  CHECK(slot.owns(&a), "B's release ignored");
  slot.release(&a);  // A done (or cut off)
  CHECK(!slot.busy(), "free after A");
  CHECK(slot.claim(&b) && slot.owns(&b), "B can go next");
  slot.release(&b);
  slot.release(&b);  // second release (done, then disconnect) is harmless
  CHECK(!slot.busy(), "double release");
  CHECK(!slot.claim(nullptr) || !slot.busy(), "null request never holds the slot");
  printf("  one upload at a time: ok\n");

  // Upload code shown on the screen
  UploadCode code;
  CHECK(!code.matches("0000") && !code.check("0000"), "no code before Wi-Fi starts");
  code.reset(1234567);  // -> 4567
  CHECK(code.value() == 4567, "value %d", code.value());
  CHECK(code.matches("4567") && code.check("4567"), "right code");
  CHECK(code.check("4567"), "right code again (second upload)");
  code.reset(42);
  CHECK(code.value() == 42 && code.check("0042"), "leading zeros");
  CHECK(!code.matches("42") && !code.matches("00042") && !code.matches("004a") && !code.matches("") &&
            !code.matches(nullptr) && !code.matches(" 042"),
        "only exactly 4 digits");
  code.reset(9999);
  for (int i = 0; i < UploadCode::MAX_TRIES - 1; i++) CHECK(!code.check("1111"), "wrong %d", i);
  CHECK(!code.locked() && code.check("9999"), "still open after %d wrong", UploadCode::MAX_TRIES - 1);
  CHECK(!code.check("1111") && code.locked(), "locked after %d wrong", UploadCode::MAX_TRIES);
  CHECK(!code.check("9999"), "right code refused once locked");
  CHECK(code.matches("9999"), "matches() still tells the reply which message to give");
  code.reset(5);
  CHECK(!code.locked() && code.check("0005"), "new code when Wi-Fi turns on again unlocks");
  printf("  upload code: ok\n");

  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
