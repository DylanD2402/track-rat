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
  CHECK(!code.matches("0000") && !code.check("0000", 0, 1), "no code before Wi-Fi starts");
  code.reset(1234567);  // -> 4567
  CHECK(code.value() == 4567, "value %d", code.value());
  CHECK(code.matches("4567") && code.check("4567", 0, 1), "right code");
  CHECK(code.check("4567", 0, 1) && code.value() == 4567, "checking doesn't use the code up");
  code.reset(42);
  CHECK(code.value() == 42 && code.check("0042", 0, 1), "leading zeros");
  CHECK(!code.matches("42") && !code.matches("00042") && !code.matches("004a") && !code.matches("") &&
            !code.matches(nullptr) && !code.matches(" 042"),
        "only exactly 4 digits");

  // One use: after a change goes through the code is a new one, and the old one is wrong
  code.reset(1111);
  code.spend(2222);
  CHECK(code.value() == 2222 && !code.matches("1111") && code.matches("2222"), "spend: new code %d", code.value());
  code.spend(2222);  // the draw is the same number: still a different code
  CHECK(code.value() != 2222 && code.value() == 2223, "spend never repeats the code: %d", code.value());
  code.spend(9999 + 1);  // 10000 % 10000
  CHECK(code.value() == 0, "draw wraps to 4 digits: %d", code.value());
  CHECK(!code.check("2222", 0, 5), "a spent code is refused (and counted)");

  // Lockout: 5 wrong codes, then everything is refused for 30 s
  const uint32_t T = 100000;
  code.reset(9999);
  for (int i = 0; i < UploadCode::MAX_TRIES - 1; i++) CHECK(!code.check("1111", T, 7), "wrong %d", i);
  CHECK(!code.locked(T) && code.check("9999", T, 7), "still open after %d wrong", UploadCode::MAX_TRIES - 1);
  for (int i = 0; i < UploadCode::MAX_TRIES - 1; i++) code.check("1111", T, 7);
  CHECK(!code.locked(T) && code.value() == 9999, "four wrong after a right code: still open, code unchanged");
  CHECK(!code.check("1111", T, 7) && code.locked(T), "locked after %d wrong", UploadCode::MAX_TRIES);
  CHECK(code.value() == 7, "a new code is drawn at the lockout: %d", code.value());
  CHECK(!code.check("0007", T + 1, 8), "right code refused while locked");
  CHECK(code.matches("0007"), "matches() still tells the reply which message to give");
  CHECK(code.waitMs(T) == 30000 && code.waitMs(T + 10000) == 20000, "wait %u", (unsigned)code.waitMs(T));
  CHECK(code.locked(T + 29999) && !code.locked(T + 30000), "30 s, then it's open");
  CHECK(code.waitMs(T + 30000) == 0, "nothing to wait for");
  CHECK(code.value() == 7, "refused tries during the lockout didn't draw codes");

  // Doubling: each lockout twice the last (after a right code the next is 30 s again)
  CHECK(!code.check("1111", T + 30000, 11) && !code.locked(T + 30000), "tries start over after the wait");
  uint32_t t = T + 30000;
  const uint32_t waits[] = {60000, 120000, 240000, 480000, 480000, 480000};
  for (uint32_t ms : waits) {
    for (int i = 0; i < UploadCode::MAX_TRIES; i++) code.check("1111", t, 3 + i);
    CHECK(code.locked(t) && code.waitMs(t) == ms, "lockout %u ms, was %u", (unsigned)ms, (unsigned)code.waitMs(t));
    t += ms;
    CHECK(!code.locked(t), "open again after %u ms", (unsigned)ms);
  }
  // A correct code resets the doubling
  code.reset(5000);
  for (int i = 0; i < UploadCode::MAX_TRIES; i++) code.check("1111", t, 5000 + i);  // 30 s
  t += 30000;
  for (int i = 0; i < UploadCode::MAX_TRIES; i++) code.check("1111", t, 6000 + i);  // 60 s
  CHECK(code.waitMs(t) == 60000, "second lockout %u", (unsigned)code.waitMs(t));
  t += 60000;
  char right[8];
  snprintf(right, sizeof(right), "%04d", code.value());
  CHECK(code.check(right, t, 1), "right code after the second lockout");
  for (int i = 0; i < UploadCode::MAX_TRIES; i++) code.check("0000", t, 8000 + i);
  CHECK(code.waitMs(t) == 30000, "a right code started the lockouts from 30 s again: %u", (unsigned)code.waitMs(t));

  // Rotation: each lockout draws from what it's given, so what was guessed before is no use
  code.reset(1234);
  for (int i = 0; i < UploadCode::MAX_TRIES; i++) code.check("0000", 0, 4321);
  CHECK(code.value() == 4321, "lockout drew %d", code.value());

  // millis() wrapping round after 49 days
  code.reset(321);
  const uint32_t nearEnd = 0xFFFFFFFFu - 10000;
  for (int i = 0; i < UploadCode::MAX_TRIES; i++) code.check("1111", nearEnd, 99);
  CHECK(code.locked(nearEnd) && code.locked(0) && code.waitMs(0) == 19999 && !code.locked(20000),
        "lockout across the wrap: %u left at 0", (unsigned)code.waitMs(0));

  code.reset(5);
  CHECK(!code.locked(0) && code.check("0005", 0, 1), "new code when Wi-Fi turns on again unlocks");

  // The words for a lockout
  char msg[120];
  lockText(30000, msg, sizeof(msg));
  CHECK(strstr(msg, "Wait 30 s,") && strstr(msg, "new code on Track Rat's screen"), "30 s text: %s", msg);
  lockText(29001, msg, sizeof(msg));
  CHECK(strstr(msg, "Wait 30 s,"), "rounds up: %s", msg);
  lockText(1, msg, sizeof(msg));
  CHECK(strstr(msg, "Wait 1 s,"), "at least a second: %s", msg);
  lockText(120000, msg, sizeof(msg));
  CHECK(strstr(msg, "Wait 2 min,"), "minutes: %s", msg);
  lockText(480000, msg, sizeof(msg));
  CHECK(strstr(msg, "Wait 8 min,"), "8 minutes: %s", msg);
  printf("  upload code: one use, lockout 30 s doubling to 8 min, new code at each lockout, millis wrap\n");

  // POSTs refused before the body is read
  const char* IP = "192.168.4.1";
  const size_t SLOT = 6553600;
  CHECK(isPortalOrigin("http://192.168.4.1", IP) && isPortalOrigin("http://192.168.4.1:80", IP), "own origin");
  CHECK(!isPortalOrigin("http://evil.example", IP) && !isPortalOrigin("https://192.168.4.1", IP) &&
            !isPortalOrigin("http://192.168.4.1.evil.example", IP) && !isPortalOrigin("http://192.168.4.10", IP) &&
            !isPortalOrigin("http://192.168.4.1:8080", IP) && !isPortalOrigin("null", IP) && !isPortalOrigin("", IP),
        "other origins");
  CHECK(postRefusal("/api/settings", 50, false, nullptr, IP, SLOT) == 0, "no Origin: left to the code");
  CHECK(postRefusal("/api/settings", 50, false, "http://192.168.4.1", IP, SLOT) == 0, "own page");
  CHECK(postRefusal("/api/settings", 50, false, "http://example.com", IP, SLOT) == 403, "other site");
  CHECK(postRefusal("/api/settings", 50, false, "null", IP, SLOT) == 403, "Origin: null");
  CHECK(postRefusal("/api/restart", 0, false, "http://example.com", IP, SLOT) == 403, "restart from another site");
  CHECK(postRefusal("/api/settings", FORM_MAX_BYTES, false, nullptr, IP, SLOT) == 0, "form at the limit");
  CHECK(postRefusal("/api/settings", FORM_MAX_BYTES + 1, false, nullptr, IP, SLOT) == 413, "form over the limit");
  CHECK(postRefusal("/api/note", 3000, false, nullptr, IP, SLOT) == 0, "a 1000-byte note, escaped, fits");
  CHECK(postRefusal("/api/tracks", TRACKS_MAX_BYTES + 100, false, nullptr, IP, SLOT) == 0, "tracks.json of 2 MB, with its wrapping");
  CHECK(postRefusal("/api/tracks", TRACKS_MAX_BYTES + MULTIPART_SLACK + 1, false, nullptr, IP, SLOT) == 413, "tracks.json over 2 MB");
  CHECK(postRefusal("/api/firmware", SLOT, false, nullptr, IP, SLOT) == 0, "slot-sized firmware");
  CHECK(postRefusal("/api/firmware", SLOT + MULTIPART_SLACK + 1, false, nullptr, IP, SLOT) == 413, "firmware over the slot");
  CHECK(postRefusal("/api/firmware", 100, true, nullptr, IP, SLOT) == 411, "chunked bodies aren't read");
  CHECK(postRefusal("/api/tracks", TRACKS_MAX_BYTES * 4, false, "http://example.com", IP, SLOT) == 403, "Origin first");
  CHECK(postRefusal("/anything", 100000, false, nullptr, IP, SLOT) == 413, "unknown URL gets the small limit");
  printf("  POST rules: other origins 403, chunked 411, over the limit 413 (tracks 2 MB, firmware the slot, others 4 KB)\n");

  CHECK(roomFor(3 * 1024 * 1024, TRACKS_MAX_BYTES) && !roomFor(2 * 1024 * 1024, TRACKS_MAX_BYTES) &&
            !roomFor(0, 1) && roomFor(100000, 1000) && !roomFor(65536, 1),
        "room for an upload");

  // Paths for the Files tab
  CHECK(isSafePath("/") && isRootPath("/") && !isRootPath("/a") && !isRootPath("") && !isRootPath("//"), "root");
  CHECK(isSafePath("/sessions") && isSafePath("/sessions/20261018_093012_calabogie.vbo") && isSafePath("/a/b/c/d.txt"),
        "plain paths");
  CHECK(isSafePath("/My Folder/lap times (1).csv") && isSafePath("/caf\xC3\xA9/\xE6\x97\xA5\xE6\x9C\xAC.txt") &&
            isSafePath("/a b") && isSafePath("/.hidden") && isSafePath("/a.b.c") && isSafePath("/a..b") &&
            isSafePath("/..a") && isSafePath("/a.."),
        "spaces, UTF-8 and dots inside names");
  CHECK(isSafePath("/#1 [best] {x} 'y' ~ ! @ $ % ^ & + = , ;"), "other punctuation FAT takes");
  CHECK(!isSafePath("") && !isSafePath("a") && !isSafePath("a/b") && !isSafePath("./a") && !isSafePath("../a") &&
            !isSafePath(nullptr),
        "must start with /");
  CHECK(!isSafePath("/..") && !isSafePath("/../a") && !isSafePath("/a/..") && !isSafePath("/a/../b") &&
            !isSafePath("/a/../../b") && !isSafePath("/.") && !isSafePath("/a/./b") && !isSafePath("/...") &&
            !isSafePath("/a/..../b"),
        "no .. segment, nor . or names of dots");
  CHECK(!isSafePath("//") && !isSafePath("//a") && !isSafePath("/a//b") && !isSafePath("/a/") && !isSafePath("/a/b/"),
        "no empty segment, no trailing /");
  CHECK(!isSafePath("/a\\b") && !isSafePath("/\\") && !isSafePath("/a\\..\\b"), "no backslash");
  {
    const char bad[] = ":*?\"<>|";
    bool refused = true;
    for (const char* b = bad; *b; b++) {
      std::string head = std::string("/a") + *b, mid = std::string("/a/b") + *b + "c", tail = std::string("/a/") + *b;
      refused = refused && !isSafePath(head.c_str()) && !isSafePath(mid.c_str()) && !isSafePath(tail.c_str());
    }
    CHECK(refused, "none of : * ? \" < > |, anywhere in a name");
    bool control = true;
    for (int c = 1; c < 0x20; c++) {
      std::string p = std::string("/a") + (char)c + "b";
      control = control && !isSafePath(p.c_str());
    }
    std::string del = std::string("/a") + (char)0x7F;
    CHECK(control && !isSafePath(del.c_str()) && !isSafePath("/a\nb") && !isSafePath("/a\tb") && !isSafePath("/a\rb"),
          "no control characters");
  }
  {
    std::string at200 = "/" + std::string(199, 'x'), at201 = "/" + std::string(200, 'x');
    CHECK(isSafePath(at200.c_str()) && !isSafePath(at201.c_str()), "200 bytes, not 201");
    std::string utf = "/";
    for (int i = 0; i < 99; i++) utf += "\xC3\xA9";  // 198 bytes of letters
    CHECK(isSafePath(utf.c_str()) && !isSafePath((utf + "\xC3\xA9\xC3\xA9").c_str()), "the limit is in bytes, not letters");
  }
  CHECK(isSameOrInside("/", "/a") && isSameOrInside("/", "/") && isSameOrInside("/a", "/a") && isSameOrInside("/a", "/a/b") &&
            isSameOrInside("/A", "/a/b/c") && isSameOrInside("/a/b", "/A/B"),
        "inside, whatever the case");
  CHECK(!isSameOrInside("/a", "/ab") && !isSameOrInside("/a", "/") && !isSameOrInside("/a/b", "/a") &&
            !isSameOrInside("/a", "/b/a") && !isSameOrInside("/abc", "/ab"),
        "not inside: a longer name, the parent, somewhere else");
  CHECK(parentLength("/a") == 0 && parentLength("/a/b") == 2 && parentLength("/a/b/c.txt") == 4, "where a path's folder ends");
  printf("  paths: safe, unsafe, inside\n");

  // The Files tab's token
  {
    FsUnlock u;
    const uint32_t rnd[4] = {0x0123abcd, 0xFFFFFFFF, 0, 0x89abcdef};
    char token[FsUnlock::TOKEN_CHARS + 1];
    CHECK(!u.unlocked() && !u.check("", 1, 0) && !u.check("00000000000000000000000000000000", 1, 0), "nothing before an unlock");
    u.issue(rnd, 0xC0A80402, 1000, token);
    CHECK(strcmp(token, "0123abcdffffffff0000000089abcdef") == 0 && strlen(token) == 32, "128 bits of hex");
    CHECK(u.unlocked() && u.check(token, 0xC0A80402, 1000), "the token opens it");
    CHECK(!u.check("0123abcdffffffff0000000089abcdee", 0xC0A80402, 1001), "one digit wrong");
    CHECK(!u.check("1123abcdffffffff0000000089abcdef", 0xC0A80402, 1001), "first digit wrong");
    CHECK(!u.check("0123abcdffffffff0000000089abcde", 0xC0A80402, 1001) && !u.check("", 0xC0A80402, 1001) &&
              !u.check("0123abcdffffffff0000000089abcdefX", 0xC0A80402, 1001) && !u.check(nullptr, 0xC0A80402, 1001),
          "too short, empty, too long");
    CHECK(!u.check("0123ABCDFFFFFFFF0000000089ABCDEF", 0xC0A80402, 1001), "hex is lower case");
    CHECK(!u.check(token, 0xC0A80403, 1001), "another phone");
    CHECK(u.check(token, 0xC0A80402, 1002), "wrong tries don't spoil it");

    // 15 minutes from the last use
    const uint32_t T = FsUnlock::IDLE_MS;
    CHECK(T == 15u * 60 * 1000, "15 minutes");
    CHECK(u.check(token, 0xC0A80402, 1002 + T), "good until 15 minutes");
    CHECK(u.check(token, 0xC0A80402, 1002 + T + T), "each use renews it");
    CHECK(u.check(token, 0xC0A80402, 1002 + 2 * T + T / 2) && u.check(token, 0xC0A80402, 1002 + 2 * T + T), "and again");
    CHECK(!u.check(token, 0xC0A80402, 1002 + 2 * T + T + T + 1), "idle for more than 15 minutes");
    CHECK(!u.unlocked() && !u.check(token, 0xC0A80402, 1002 + 2 * T + T + T + 2), "and it's forgotten, not just late");

    // millis() wraps after 49 days: the clock passing 2^32 mustn't end it, or revive an old one
    u.issue(rnd, 7, 0xFFFFFFFFu - 1000, token);
    CHECK(u.check(token, 7, 0xFFFFFFFFu - 500), "just before the wrap");
    CHECK(u.check(token, 7, 500), "just after it (1000 ms later)");
    CHECK(u.check(token, 7, 500 + T), "a use after the wrap renews it");
    CHECK(!u.check(token, 7, 500 + 2 * T + 1), "and it runs out across the wrap too");
    u.issue(rnd, 7, 0xFFFFFFFFu - 100, token);
    CHECK(!u.check(token, 7, T + 1000), "idle across the wrap");

    // One at a time; forgotten when asked
    const uint32_t rnd2[4] = {1, 2, 3, 4};
    char t1[FsUnlock::TOKEN_CHARS + 1], t2[FsUnlock::TOKEN_CHARS + 1];
    u.issue(rnd, 7, 0, t1);
    u.issue(rnd2, 8, 10, t2);
    CHECK(strcmp(t1, t2) != 0 && !u.check(t1, 7, 20) && !u.check(t1, 8, 20) && u.check(t2, 8, 20), "a new unlock replaces the old");
    u.forget();
    CHECK(!u.unlocked() && !u.check(t2, 8, 30), "forgotten when Wi-Fi stops");
    u.issue(rnd2, 8, 40, t2);
    CHECK(u.check(t2, 8, 41), "and a later unlock works again");
    printf("  files token: wrong token, wrong phone, 15 minute sliding expiry across the wrap, one at a time, forgotten\n");
  }

  // The list of a folder, sent in pieces like the session list
  {
    FsListJson<std::string> json;
    json.addEntry("sessions", 0, true);
    json.addEntry("tracks \"x\".json", 2048, false);
    json.addEntry("caf\xC3\xA9", 4294967295u, false);
    json.finish(1234, 56789);
    std::vector<uint8_t> buf(64);
    std::string all;
    for (;;) {
      size_t n = json.fill(buf.data(), buf.size(), 4, [] {});
      if (!n) break;
      all.append((const char*)buf.data(), n);
    }
    CHECK(all == "{\"entries\":[{\"name\":\"sessions\",\"size\":0,\"isDir\":true},{\"name\":\"tracks \\\"x\\\".json\",\"size\":2048,"
                 "\"isDir\":false},{\"name\":\"caf\xC3\xA9\",\"size\":4294967295,\"isDir\":false}],\"freeKB\":1234,\"totalKB\":56789}",
          "folder list as JSON: %s", all.c_str());
    FsListJson<std::string> empty;
    empty.finish(0, 0);
    std::string e;
    for (size_t n; (n = empty.fill(buf.data(), buf.size(), 4, [] {})) > 0;) e.append((const char*)buf.data(), n);
    CHECK(e == "{\"entries\":[],\"freeKB\":0,\"totalKB\":0}", "an empty folder");
  }

  // 1 GB uploads to the Files tab
  CHECK(postRefusal("/api/fs/upload", FS_UPLOAD_MAX_BYTES, false, nullptr, IP, SLOT) == 0, "a 1 GB file");
  CHECK(postRefusal("/api/fs/upload", FS_UPLOAD_MAX_BYTES + MULTIPART_SLACK, false, nullptr, IP, SLOT) == 0, "1 GB with its wrapping");
  CHECK(postRefusal("/api/fs/upload", FS_UPLOAD_MAX_BYTES + MULTIPART_SLACK + 1, false, nullptr, IP, SLOT) == 413, "over 1 GB");
  CHECK(postRefusal("/api/fs/upload", 5000, false, "http://example.com", IP, SLOT) == 403, "not from another site");
  CHECK(postRefusal("/api/fs/upload", 5000, true, nullptr, IP, SLOT) == 411, "not chunked");
  CHECK(postRefusal("/api/fs/delete", FORM_MAX_BYTES + 1, false, nullptr, IP, SLOT) == 413 &&
            postRefusal("/api/fs/rename", 2000, false, nullptr, IP, SLOT) == 0 &&
            postRefusal("/api/fs/unlock", 40, false, nullptr, IP, SLOT) == 0,
        "the other file routes take a form");
  printf("  files uploads: up to 1 GB, other routes 4 KB\n");

  // A session's files, for Delete: the log, summary, note and a cut-off save of each, nothing else
  CHECK(SESSION_FILE_COUNT == 6, "six file kinds");
  bool suffixesOk = true;
  for (int i = 0; i < SESSION_FILE_COUNT; i++) {
    const char* x = SESSION_FILE_SUFFIXES[i];
    suffixesOk = suffixesOk && (x[0] == '.' || x[0] == '_') && !strchr(x, '/');
    for (int j = 0; j < i; j++) suffixesOk = suffixesOk && strcmp(x, SESSION_FILE_SUFFIXES[j]) != 0;
  }
  CHECK(suffixesOk, "suffixes are distinct and add to the base name only");
  bool hasVbo = false, hasNote = false, hasTmp = false;
  for (int i = 0; i < SESSION_FILE_COUNT; i++) {
    hasVbo = hasVbo || strcmp(SESSION_FILE_SUFFIXES[i], ".vbo") == 0;
    hasNote = hasNote || strcmp(SESSION_FILE_SUFFIXES[i], "_note.json") == 0;
    hasTmp = hasTmp || strcmp(SESSION_FILE_SUFFIXES[i], "_note.json.tmp") == 0;
  }
  CHECK(hasVbo && hasNote && hasTmp, "log, note and a cut-off note save");
  printf("  session delete: %d file kinds\n", SESSION_FILE_COUNT);

  // Downloads at once
  DownloadGate dl;
  CHECK(dl.enter() && dl.enter() && !dl.enter() && dl.count() == 2, "third download refused");
  dl.leave();
  CHECK(dl.enter() && !dl.enter(), "a finished download frees a place");
  dl.leave();
  dl.leave();
  dl.leave();  // more leaves than enters: harmless
  CHECK(dl.count() == 0 && dl.enter(), "back to none");
  printf("  downloads: two at once\n");

  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
