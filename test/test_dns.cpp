/**
 * Host-side tests for the captive portal's DNS answer (include/CaptiveDns.h): A and ANY get the
 * board's address, other types get an empty NOERROR, an EDNS0 OPT record doesn't stop a reply,
 * and damaged packets get none.
 *
 *   g++ -std=c++17 -Iinclude test/test_dns.cpp -o /tmp/dns && /tmp/dns
 */

#include <cstdio>
#include <cstring>
#include <vector>
#include "CaptiveDns.h"
#include "check.h"

using Bytes = std::vector<uint8_t>;

static const uint8_t IP[4] = {192, 168, 4, 1};
// "connectivitycheck.gstatic.com"
static const Bytes NAME = {17, 'c', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'v', 'i', 't', 'y', 'c', 'h', 'e', 'c', 'k',
                           7,  'g', 's', 't', 'a', 't', 'i', 'c', 3,   'c', 'o', 'm', 0};

static void put16(Bytes& b, unsigned v) {
  b.push_back((uint8_t)(v >> 8));
  b.push_back((uint8_t)v);
}

// A query: header, one question, and an EDNS0 OPT record in the additional section if asked
static Bytes query(unsigned type, bool opt, unsigned flags = 0x0100, unsigned cls = 1) {
  Bytes q;
  put16(q, 0xBEEF);
  put16(q, flags);
  put16(q, 1);
  put16(q, 0);
  put16(q, 0);
  put16(q, opt ? 1 : 0);
  q.insert(q.end(), NAME.begin(), NAME.end());
  put16(q, type);
  put16(q, cls);
  if (opt) {
    q.push_back(0);  // root name
    put16(q, 41);    // OPT
    put16(q, 1232);  // UDP size
    put16(q, 0);     // extended rcode and flags
    put16(q, 0);
    put16(q, 0);  // no options
  }
  return q;
}

static size_t run(const Bytes& q, Bytes& out, size_t cap = 512) {
  out.assign(cap, 0xAA);
  size_t n = captivedns::reply(q.data(), q.size(), IP, out.data(), cap);
  out.resize(n);
  return n;
}

static unsigned be16(const Bytes& b, size_t i) { return b.size() > i + 1 ? (unsigned)((b[i] << 8) | b[i + 1]) : 0; }

static void testA() {
  for (int opt = 0; opt < 2; opt++) {
    Bytes out;
    const Bytes q = query(1, opt);
    const size_t n = run(q, out);
    const size_t qEnd = 12 + NAME.size() + 4;
    CHECK(n == qEnd + 16, "A (opt %d): length %zu", opt, n);
    if (n != qEnd + 16) return;
    CHECK(be16(out, 0) == 0xBEEF, "ID echoed");
    CHECK(be16(out, 2) == 0x8580, "flags QR AA RD RA, NOERROR: %04X", be16(out, 2));
    CHECK(be16(out, 4) == 1 && be16(out, 6) == 1 && be16(out, 8) == 0 && be16(out, 10) == 0,
          "counts 1 question, 1 answer, nothing else (the OPT is not echoed)");
    CHECK(memcmp(&out[12], &q[12], NAME.size() + 4) == 0, "question echoed");
    const uint8_t want[16] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, 192, 168, 4, 1};
    CHECK(memcmp(&out[qEnd], want, 16) == 0, "answer bytes (opt %d)", opt);
  }
  printf("  A: answered with and without an OPT record\n");
}

static void testAny() {
  Bytes out;
  const size_t n = run(query(255, true), out);
  CHECK(n == 12 + NAME.size() + 4 + 16 && be16(out, 6) == 1, "ANY gets the address");
  Bytes nord;
  run(query(1, false, 0x0000), nord);
  CHECK(be16(nord, 2) == 0x8480, "RD not set in the query: not set in the reply: %04X", be16(nord, 2));
  printf("  ANY and RD handling\n");
}

static void testOtherTypes() {
  const unsigned types[] = {28, 65, 64, 15, 16};
  for (unsigned t : types) {
    Bytes out;
    const size_t n = run(query(t, true), out);
    CHECK(n == 12 + NAME.size() + 4, "type %u: question only, got %zu", t, n);
    if (n != 12 + NAME.size() + 4) continue;
    CHECK(be16(out, 2) == 0x8580, "type %u: NOERROR flags %04X", t, be16(out, 2));
    CHECK(be16(out, 4) == 1 && be16(out, 6) == 0 && be16(out, 10) == 0, "type %u: no answer", t);
  }
  Bytes out;
  const size_t n = run(query(1, false, 0x0100, 3), out);  // class CH
  CHECK(n == 12 + NAME.size() + 4 && be16(out, 6) == 0, "class other than IN: no answer");
  printf("  AAAA, HTTPS, SVCB...: empty NOERROR\n");
}

static void testHeaderOnly() {
  Bytes out;
  Bytes q = query(1, false, 0x1100);  // opcode 2 (STATUS)
  CHECK(run(q, out) == 12 && be16(out, 2) == 0x9104 && be16(out, 4) == 0 && be16(out, 0) == 0xBEEF,
        "other opcode: NOTIMP, header only: %04X", be16(out, 2));
  q = query(1, false);
  q[5] = 2;  // two questions
  CHECK(run(q, out) == 12 && be16(out, 2) == 0x8101 && be16(out, 4) == 0, "QDCOUNT 2: FORMERR, header only");
  q[5] = 0;
  CHECK(run(q, out) == 12 && (be16(out, 2) & 15) == 1, "QDCOUNT 0: FORMERR");
  printf("  NOTIMP and FORMERR\n");
}

// A question with a name made of labels of the given lengths
static Bytes queryWithLabels(const std::vector<int>& labels) {
  Bytes q = query(1, false);
  q.resize(12);
  for (int l : labels) {
    q.push_back((uint8_t)l);
    q.insert(q.end(), (size_t)l, 'a');
  }
  q.push_back(0);
  put16(q, 1);
  put16(q, 1);
  return q;
}

static void testMalformed() {
  Bytes out;
  Bytes q = query(1, false);
  CHECK(run(Bytes(q.begin(), q.begin() + 11), out) == 0, "shorter than a header: ignored");
  q[2] |= 0x80;
  CHECK(run(q, out) == 0, "a response: ignored");

  q = query(1, false);
  q.resize(q.size() - 1);
  CHECK(run(q, out) == 0, "QCLASS cut off: ignored");
  q.resize(12 + 5);
  CHECK(run(q, out) == 0, "label runs past the packet: ignored");
  q.resize(12);
  CHECK(run(q, out) == 0, "no question at all: ignored");

  q = query(1, false);
  q[12] = 0xC0;  // compression pointer in the QNAME
  CHECK(run(q, out) == 0, "pointer in the name: ignored");
  q = query(1, false);
  q[12] = 0x80;
  CHECK(run(q, out) == 0, "reserved label type: ignored");

  CHECK(run(queryWithLabels({63, 63, 63, 63, 63}), out) == 0, "name over 255 bytes: ignored");
  CHECK(run(queryWithLabels({63, 63, 63, 61}), out) > 0, "a 255-byte name is fine");
  printf("  damaged packets: no reply\n");
}

static void testSmallOut() {
  Bytes out;
  const Bytes q = query(1, true);
  const size_t need = 12 + NAME.size() + 4 + 16;
  CHECK(run(q, out, need) == need, "exactly enough room");
  CHECK(run(q, out, need - 1) == 0, "one byte short: no reply");
  CHECK(run(q, out, 11) == 0, "less than a header: no reply");
  const size_t qOnly = 12 + NAME.size() + 4;
  CHECK(run(query(28, false), out, qOnly) == qOnly, "empty answer needs only the question");
  CHECK(run(query(28, false), out, qOnly - 1) == 0, "empty answer one byte short: no reply");
  printf("  output buffer too small: no reply\n");
}

int main() {
  printf("DNS tests\n");
  testA();
  testAny();
  testOtherTypes();
  testHeaderOnly();
  testMalformed();
  testSmallOut();
  printf(failures ? "\n%d FAILURE(S)\n" : "\nAll DNS tests passed\n", failures);
  return failures ? 1 : 0;
}
