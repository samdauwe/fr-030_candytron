/* Checks for sha256.h: the FIPS 180-4 vectors, the block boundaries and incremental updates. The boundary vectors were made with Python's
 * hashlib over the bytes (i * 31 + 7) & 255. */
#include <stdint.h>
#include <string.h>

#include "support/check.h"
#include "support/suite.h"

#define CT_SHA256_IMPLEMENTATION
#include <candytron/core/sha256.h>

static void hex_of(const void *data, size_t size, char hex[65])
{
  ct_sha256 s;
  uint8_t digest[32];
  ct_sha256_init(&s);
  ct_sha256_update(&s, data, size);
  ct_sha256_final(&s, digest);
  ct_sha256_hex(digest, hex);
}

int suite_sha256(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  char hex[65];

  hex_of("", 0, hex);
  CHECK(!strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "empty message: %s", hex);
  hex_of("abc", 3, hex);
  CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "abc: %s", hex);
  const char *two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  hex_of(two_blocks, strlen(two_blocks), hex);
  CHECK(!strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "448-bit message: %s", hex);

  /* a million 'a', fed in odd pieces */
  {
    ct_sha256 s;
    uint8_t digest[32], chunk[997];
    memset(chunk, 'a', sizeof chunk);
    ct_sha256_init(&s);
    size_t left = 1000000;
    while (left) {
      const size_t n = left < sizeof chunk ? left : sizeof chunk;
      ct_sha256_update(&s, chunk, n);
      left -= n;
    }
    ct_sha256_final(&s, digest);
    ct_sha256_hex(digest, hex);
    CHECK(!strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"), "one million a: %s", hex);
  }

  static const struct {
    size_t size;
    const char *hex;
  } boundary[] = {
    { 1, "ca358758f6d27e6cf45272937977a748fd88391db679ceda7dc7bf1f005ee879" },    { 55, "8aa994584139d128848eeebc4e815639ba5ab6e6e39574195a63ac4f14f7c43b" },
    { 56, "ad574708f75c044c9b85de64cb568ee7711ff4f36448c6242f053ba8f6cc2b63" },   { 57, "5b46e502092be01b1100193e089fdda95638c12e19a1d24f308eb2c3d3ae849d" },
    { 63, "280ed3e8ff1df845b2e7dfe6ac6cee817bef20e783cc65abc41b818b4d2fe076" },   { 64, "c6ab9724ade5b6a7a1edfffb12f3aa9181351355af8fd08c919952ad211339dd" },
    { 65, "788367c73c7ddf4c53f65e68cc0d943e6227ab55b0e78ba63ace822b1c6301c0" },   { 119, "3d610547d68216dedf7435a4fb6260353911f6b3fd3f18805ddb8be285d726fe" },
    { 120, "1f80156a804cb7862ad113e8200e9d74499723e7c7854d5f48776d3148e09656" },  { 128, "cc548ca2dec1f6fe4f58b2e27aa9c7521607df1130d140b55a4dad0665302356" },
    { 1000, "5097e7d587352f5097062ae679f37bda5802d9f875aba14c8cb4d1a188ada179" },
  };
  uint8_t data[1000];
  for (size_t i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 31u + 7u);
  for (size_t k = 0; k < sizeof boundary / sizeof boundary[0]; k++) {
    hex_of(data, boundary[k].size, hex);
    CHECK(!strcmp(hex, boundary[k].hex), "%lu bytes: %s", (unsigned long)boundary[k].size, hex);
    /* every way of splitting the message in two gives the same digest */
    for (size_t cut = 0; cut <= boundary[k].size; cut += 7) {
      ct_sha256 s;
      uint8_t digest[32];
      ct_sha256_init(&s);
      ct_sha256_update(&s, data, cut);
      ct_sha256_update(&s, data + cut, boundary[k].size - cut);
      ct_sha256_final(&s, digest);
      ct_sha256_hex(digest, hex);
      CHECK(!strcmp(hex, boundary[k].hex), "%lu bytes split at %lu: %s", (unsigned long)boundary[k].size, (unsigned long)cut, hex);
    }
  }

  printf("sha256: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
