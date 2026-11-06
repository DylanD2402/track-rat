/**
 * Track Rat - the DNS answer for the captive portal (no Arduino code, tested on a PC)
 *
 * Every name gets the board's address, so a phone that joins the network
 * reaches the page whatever it asks for. The core's DNSServer was dropped
 * because it ignores any query with an EDNS0 OPT record in it (most modern
 * phones add one) and answers AAAA and HTTPS queries with an A record.
 *
 * reply() looks at one query packet and writes the answer:
 *  - A or ANY, class IN: the address, TTL 30 s.
 *  - Any other type (AAAA, HTTPS, SVCB...): NOERROR with no answer, so the
 *    phone goes straight on to its A query.
 *  - Not a plain query, or not exactly one question: NOTIMP / FORMERR, header only.
 *  - Anything in the additional section (the OPT record) is read past and not echoed.
 *  - Damaged, truncated or too long: no reply at all.
 */

#ifndef CAPTIVE_DNS_H
#define CAPTIVE_DNS_H

#include <cstddef>
#include <cstdint>

namespace captivedns {

constexpr size_t HEADER_LEN = 12;
constexpr uint32_t TTL_S = 30;

// Writes the reply to `out` and returns its length, or 0 when there is nothing to send.
inline size_t reply(const uint8_t* query, size_t len, const uint8_t ip[4], uint8_t* out, size_t cap) {
  if (len < HEADER_LEN || cap < HEADER_LEN) return 0;
  const uint16_t flags = (uint16_t)((query[2] << 8) | query[3]);
  if (flags & 0x8000) return 0;  // a response, not a query
  const unsigned opcode = (flags >> 11) & 0x0F;
  const unsigned qdCount = (unsigned)((query[4] << 8) | query[5]);
  const uint16_t rd = flags & 0x0100;

  out[0] = query[0];
  out[1] = query[1];
  for (size_t i = 4; i < HEADER_LEN; i++) out[i] = 0;
  if (opcode != 0 || qdCount != 1) {
    const unsigned rcode = opcode != 0 ? 4 : 1;  // NOTIMP : FORMERR
    const uint16_t f = (uint16_t)(0x8000 | (opcode << 11) | rd | rcode);
    out[2] = (uint8_t)(f >> 8);
    out[3] = (uint8_t)f;
    return HEADER_LEN;
  }

  // The question: labels up to the zero byte. Compression pointers don't belong in a question.
  size_t pos = HEADER_LEN;
  for (;;) {
    if (pos >= len) return 0;
    const uint8_t l = query[pos];
    if (l == 0) break;
    if (l & 0xC0) return 0;
    pos += 1 + (size_t)l;
    if (pos - HEADER_LEN >= 255) return 0;  // a name is 255 bytes at most, with its length bytes
  }
  const size_t qEnd = pos + 1 + 4;  // zero byte, QTYPE, QCLASS
  if (qEnd > len) return 0;
  const uint16_t qtype = (uint16_t)((query[pos + 1] << 8) | query[pos + 2]);
  const uint16_t qclass = (uint16_t)((query[pos + 3] << 8) | query[pos + 4]);
  const bool answer = (qtype == 1 || qtype == 255) && qclass == 1;
  const size_t total = qEnd + (answer ? 16 : 0);
  if (total > cap) return 0;

  const uint16_t f = (uint16_t)(0x8000 | 0x0400 | 0x0080 | rd);  // QR, AA, RA, RD as asked, NOERROR
  out[2] = (uint8_t)(f >> 8);
  out[3] = (uint8_t)f;
  out[5] = 1;               // QDCOUNT
  out[7] = answer ? 1 : 0;  // ANCOUNT
  for (size_t i = HEADER_LEN; i < qEnd; i++) out[i] = query[i];
  if (answer) {
    uint8_t* a = out + qEnd;
    a[0] = 0xC0;  // name: pointer to the question at offset 12
    a[1] = 0x0C;
    a[2] = 0;  // type A
    a[3] = 1;
    a[4] = 0;  // class IN
    a[5] = 1;
    a[6] = (uint8_t)(TTL_S >> 24);
    a[7] = (uint8_t)(TTL_S >> 16);
    a[8] = (uint8_t)(TTL_S >> 8);
    a[9] = (uint8_t)TTL_S;
    a[10] = 0;  // RDLENGTH 4
    a[11] = 4;
    for (int i = 0; i < 4; i++) a[12 + i] = ip[i];
  }
  return total;
}

}  // namespace captivedns

#endif  // CAPTIVE_DNS_H
