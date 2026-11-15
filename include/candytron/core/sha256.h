/* sha256.h - SHA-256 (FIPS 180-4) - single-header C99 library (stb style)

   Checks the production data of the player (assets.h) and the files of the tests. Not for secrets: it is neither constant time nor
   hardened, it only has to agree with `sha256sum`.

   USAGE
     In exactly one translation unit:

         #define CT_SHA256_IMPLEMENTATION
         #include <candytron/core/sha256.h>

     ct_sha256 s;
     ct_sha256_init(&s);
     ct_sha256_update(&s, data, size);          (any number of times)
     ct_sha256_final(&s, digest);               (32 bytes)
     ct_sha256_hex(digest, text);               (64 characters and a terminator)

   CONFIGURATION (before including the implementation)
     CT_SHA256_STATIC    make every API function static
*/
#ifndef CT_SHA256_H_INCLUDED
#define CT_SHA256_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#ifndef CT_SHA256_API
#ifdef CT_SHA256_STATIC
#define CT_SHA256_API static
#else
#define CT_SHA256_API extern
#endif
#endif

typedef struct ct_sha256 {
  uint32_t h[8];
  uint64_t bytes; /* message length so far */
  uint8_t block[64];
  uint32_t used;  /* bytes in block */
} ct_sha256;

CT_SHA256_API void ct_sha256_init(ct_sha256 *s);
CT_SHA256_API void ct_sha256_update(ct_sha256 *s, const void *data, size_t size);
CT_SHA256_API void ct_sha256_final(ct_sha256 *s, uint8_t digest[32]);
CT_SHA256_API void ct_sha256_hex(const uint8_t digest[32], char text[65]);

#endif /* CT_SHA256_H_INCLUDED */

#ifdef CT_SHA256_IMPLEMENTATION
#ifndef CT_SHA256_IMPLEMENTATION_DONE
#define CT_SHA256_IMPLEMENTATION_DONE

static const uint32_t ct_sha256_k[64] = { 0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
                                          0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
                                          0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
                                          0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
                                          0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
                                          0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
                                          0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
                                          0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u };

static uint32_t ct_sha256_ror(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

static void ct_sha256_compress(uint32_t h[8], const uint8_t block[64])
{
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16 | (uint32_t)block[4 * i + 2] << 8 | (uint32_t)block[4 * i + 3];
  for (int i = 16; i < 64; i++) {
    const uint32_t s0 = ct_sha256_ror(w[i - 15], 7) ^ ct_sha256_ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = ct_sha256_ror(w[i - 2], 17) ^ ct_sha256_ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
  for (int i = 0; i < 64; i++) {
    const uint32_t t1 = hh + (ct_sha256_ror(e, 6) ^ ct_sha256_ror(e, 11) ^ ct_sha256_ror(e, 25)) + ((e & f) ^ (~e & g)) + ct_sha256_k[i] + w[i];
    const uint32_t t2 = (ct_sha256_ror(a, 2) ^ ct_sha256_ror(a, 13) ^ ct_sha256_ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    hh = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
  h[5] += f;
  h[6] += g;
  h[7] += hh;
}

CT_SHA256_API void ct_sha256_init(ct_sha256 *s)
{
  static const uint32_t iv[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
  for (int i = 0; i < 8; i++) s->h[i] = iv[i];
  s->bytes = 0;
  s->used = 0;
}

CT_SHA256_API void ct_sha256_update(ct_sha256 *s, const void *data, size_t size)
{
  const uint8_t *p = (const uint8_t *)data;
  s->bytes += size;
  while (size) {
    size_t n = 64u - s->used;
    if (n > size) n = size;
    for (size_t i = 0; i < n; i++) s->block[s->used + i] = p[i];
    s->used += (uint32_t)n;
    p += n;
    size -= n;
    if (s->used == 64u) {
      ct_sha256_compress(s->h, s->block);
      s->used = 0;
    }
  }
}

CT_SHA256_API void ct_sha256_final(ct_sha256 *s, uint8_t digest[32])
{
  const uint64_t bits = s->bytes * 8u;
  const uint8_t one = 0x80, zero = 0;
  ct_sha256_update(s, &one, 1); /* the padding changes s->bytes, which is why the length was taken above */
  while (s->used != 56u) ct_sha256_update(s, &zero, 1);
  uint8_t length[8];
  for (int i = 0; i < 8; i++) length[i] = (uint8_t)(bits >> (56 - 8 * i));
  ct_sha256_update(s, length, 8);
  for (int i = 0; i < 8; i++) {
    digest[4 * i] = (uint8_t)(s->h[i] >> 24);
    digest[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
    digest[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
    digest[4 * i + 3] = (uint8_t)s->h[i];
  }
}

CT_SHA256_API void ct_sha256_hex(const uint8_t digest[32], char text[65])
{
  static const char digits[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    text[2 * i] = digits[digest[i] >> 4];
    text[2 * i + 1] = digits[digest[i] & 15];
  }
  text[64] = 0;
}

#endif /* CT_SHA256_IMPLEMENTATION_DONE */
#endif /* CT_SHA256_IMPLEMENTATION */
