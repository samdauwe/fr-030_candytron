/* The V2 synthesizer against the v2redux oracle.
 *
 *   suite_v2 <assets dir> <oracle fixtures dir>
 *
 * Renders josie.v2m (format version 5, with the speech synthesizer on channel 15) with v2.h as the player does and compares it with
 * oracle/josie_pcm.txt, the SHA-256 of every second of what the v2redux oracle renders, bit for bit: all 9,000,000 frames (204.08 s,
 * the song and its tail), in chunks of irregular size (the output must not depend on how it is asked for). Also: the song's format
 * version and length as the oracle has them, muting, seeking, damaged files (no crash, no leak: the suite runs under the address
 * sanitizer in the asan preset), and every allocation failure of v2_open().
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/check.h"
#include "support/suite.h"

static long g_live, g_count, g_fail_at = -1;
static void *t_malloc(size_t n)
{
  if (g_count++ == g_fail_at) return NULL;
  size_t *p = (size_t *)malloc(n + 16);
  if (!p) return NULL;
  g_live++;
  return (char *)p + 16;
}
static void t_free(void *q)
{
  if (!q) return;
  free((void *)((uintptr_t)q - 16));
  g_live--;
}
#define V2_MALLOC(n) t_malloc(n)
#define V2_FREE(p)   t_free(p)
#define V2_STATIC
#define V2_IMPLEMENTATION
#include <candytron/core/v2.h>
#define CT_SHA256_IMPLEMENTATION
#include <candytron/core/sha256.h>

enum { RATE = 44100, FRAME_BYTES = 8 };

static uint64_t g_rng = 0x9e3779b97f4a7c15ull;
static uint32_t rnd(void)
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 7;
  g_rng ^= g_rng << 17;
  return (uint32_t)(g_rng >> 32);
}

static unsigned char *read_file(const char *dir, const char *name, size_t *size)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *data = (unsigned char *)malloc((size_t)n + 1);
  if (data && fread(data, 1, (size_t)n, f) == (size_t)n) {
    data[n] = 0;
    *size = (size_t)n;
  } else {
    free(data);
    data = NULL;
  }
  fclose(f);
  return data;
}

typedef struct fixture {
  char song_sha[65], pcm_sha[65];
  int version;
  long long length_ms;
  uint64_t frames;
  uint32_t block;
  size_t n_blocks;
  char (*hash)[65];
} fixture;

static int load_fixture(const char *dir, fixture *fx)
{
  size_t size = 0;
  char *text = (char *)read_file(dir, "josie_pcm.txt", &size);
  if (!text) return 0;
  memset(fx, 0, sizeof *fx);
  fx->hash = (char (*)[65])calloc(1024, 65);
  for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
    unsigned long long v;
    char hex[80];
    unsigned index;
    if (line[0] == '#') continue;
    if (sscanf(line, "song_sha256 %64s", fx->song_sha) == 1) continue;
    if (sscanf(line, "pcm_sha256 %64s", fx->pcm_sha) == 1) continue;
    if (sscanf(line, "version %d", &fx->version) == 1) continue;
    if (sscanf(line, "length_ms %llu", &v) == 1) {
      fx->length_ms = (long long)v;
      continue;
    }
    if (sscanf(line, "frames %llu", &v) == 1) {
      fx->frames = v;
      continue;
    }
    if (sscanf(line, "block %llu", &v) == 1) {
      fx->block = (uint32_t)v;
      continue;
    }
    if (sscanf(line, "%u %64s", &index, hex) == 2 && index < 1024) {
      memcpy(fx->hash[index], hex, 65);
      if (index + 1 > fx->n_blocks) fx->n_blocks = index + 1;
    }
  }
  free(text);
  return fx->block == RATE && fx->frames && fx->n_blocks == (fx->frames + fx->block - 1) / fx->block;
}

static void sha_hex(const void *data, size_t size, char out[65])
{
  ct_sha256 c;
  uint8_t digest[32];
  ct_sha256_init(&c);
  ct_sha256_update(&c, data, size);
  ct_sha256_final(&c, digest);
  ct_sha256_hex(digest, out);
}

/* the whole song in irregular chunks, hashed per second as the fixture is */
static void check_oracle_pcm(const unsigned char *song, size_t song_size, const fixture *fx)
{
  v2_synth *s = v2_create(RATE);
  CHECK(s != NULL, "v2_create");
  if (!s) return;
  CHECK(v2_open(s, song, song_size) == V2_OK, "v2_open of josie.v2m");
  v2_play(s, 0);
  static float chunk[4096 * 2];
  ct_sha256 total, block;
  uint8_t digest[32];
  char hex[65];
  ct_sha256_init(&total);
  ct_sha256_init(&block);
  uint64_t done = 0, in_block = 0;
  size_t index = 0;
  size_t mismatches = 0;
  while (done < fx->frames) {
    uint64_t n = 1 + rnd() % 4096;
    if (n > fx->frames - done) n = fx->frames - done;
    v2_render(s, chunk, (uint32_t)n);
    ct_sha256_update(&total, chunk, (size_t)n * FRAME_BYTES);
    for (uint64_t off = 0; off < n;) {
      const uint64_t take = n - off < RATE - in_block ? n - off : RATE - in_block;
      ct_sha256_update(&block, chunk + off * 2, (size_t)take * FRAME_BYTES);
      off += take;
      in_block += take;
      if (in_block == RATE || done + off == fx->frames) {
        ct_sha256_final(&block, digest);
        ct_sha256_hex(digest, hex);
        if (index < fx->n_blocks && strcmp(hex, fx->hash[index]) != 0) {
          if (mismatches++ < 5) printf("second %zu differs from the oracle\n", index);
        }
        index++;
        in_block = 0;
        ct_sha256_init(&block);
      }
    }
    done += n;
  }
  ct_sha256_final(&total, digest);
  ct_sha256_hex(digest, hex);
  CHECK(mismatches == 0, "%zu of %zu seconds differ from the oracle", mismatches, fx->n_blocks);
  CHECK(index == fx->n_blocks, "%zu blocks, %zu expected", index, fx->n_blocks);
  CHECK(!strcmp(hex, fx->pcm_sha), "the SHA-256 of the whole render is %s, the oracle's is %s", hex, fx->pcm_sha);
  v2_destroy(s);
}

static void check_song_facts(const unsigned char *song, size_t song_size, const fixture *fx)
{
  char hex[65];
  sha_hex(song, song_size, hex);
  CHECK(!strcmp(hex, fx->song_sha), "the fixture is of another josie.v2m (%s)", hex);
  v2_synth *s = v2_create(RATE);
  CHECK(v2_open(s, song, song_size) == V2_OK, "open");
  CHECK(v2_file_version(s) == fx->version && fx->version == 5, "format version %d, the oracle's is %d", v2_file_version(s), fx->version);
  const uint64_t length = v2_length_frames(s);
  CHECK((long long)(length * 1000 / RATE) == fx->length_ms, "length %llu ms, the oracle's is %lld", (unsigned long long)(length * 1000 / RATE), fx->length_ms);
  v2_destroy(s);
}

/* muting feeds no bus: silence; the default mask is the reference; the polyphony of the 2003 build is 32 */
static void check_control(const unsigned char *song, size_t song_size)
{
  v2_synth *s = v2_create(RATE);
  static float a[RATE * 2], b[RATE * 2];
  v2_open(s, song, song_size);
  v2_set_channel_mask(s, 0);
  v2_play(s, 0);
  v2_render(s, a, RATE);
  double peak = 0;
  for (int i = 0; i < RATE * 2; i++) peak = a[i] > peak ? a[i] : a[i] < -peak ? -a[i] : peak;
  CHECK(peak == 0.0, "a muted song makes a sound (peak %g)", peak);

  v2_set_channel_mask(s, 0xffff);
  v2_play(s, 0);
  int most = 0;
  for (int t = 0; t < 120; t++) { /* two minutes of one-second blocks, reading the polyphony of each */
    int poly[16];
    v2_render(s, a, RATE);
    const int total = v2_get_channel_poly(s, poly);
    if (total > most) most = total;
  }
  CHECK(most > 0 && most <= 32, "the polyphony peaks at %d (the pool has 32 voices)", most);

  /* a seek restarts the sequencer and the synth: the same start renders the same, and it is audible */
  v2_play(s, 30000);
  v2_render(s, a, RATE);
  v2_play(s, 30000);
  v2_render(s, b, RATE);
  CHECK(memcmp(a, b, sizeof a) == 0, "two starts at 30 s render differently");
  peak = 0;
  for (int i = 0; i < RATE * 2; i++) peak = a[i] > peak ? a[i] : a[i] < -peak ? -a[i] : peak;
  CHECK(peak > 0.01, "nothing is heard at 30 s (peak %g)", peak);

  /* stopping silences */
  v2_stop(s, 0);
  v2_render(s, a, 1000);
  peak = 0;
  for (int i = 0; i < 2000; i++) peak = a[i] > peak ? a[i] : a[i] < -peak ? -a[i] : peak;
  CHECK(!v2_is_playing(s) && peak == 0.0, "stop does not silence");
  v2_destroy(s);
}

/* damaged files: refused or played, never a crash or a leak (the address sanitizer judges) */
static void check_damaged(const unsigned char *song, size_t song_size)
{
  unsigned char *copy = (unsigned char *)malloc(song_size);
  static float out[2048 * 2];
  int refused = 0, played = 0;

  CHECK(v2_open(NULL, song, song_size) == V2_ERR_BAD_ARGUMENT, "a NULL synth");
  v2_synth *s = v2_create(RATE);
  CHECK(v2_open(s, NULL, 0) == V2_ERR_BAD_FILE, "no data");
  CHECK(v2_open(s, song, 15) == V2_ERR_BAD_FILE, "15 bytes");
  CHECK(v2_create(44099) == NULL && v2_create(96001) == NULL, "sample rates outside 44.1 to 96 kHz");
  for (int i = 0; i < 400; i++) {
    size_t n = song_size;
    memcpy(copy, song, song_size);
    if (i % 4 == 0) n = rnd() % song_size;                                                                /* truncated */
    for (int k = 0; k < 1 + (int)(rnd() % 4); k++) copy[rnd() % n] ^= (unsigned char)(1u << (rnd() % 8)); /* flipped bits */
    const v2_result r = v2_open(s, copy, n);
    if (r == V2_OK) {
      v2_play(s, 0);
      v2_render(s, out, 2048);
      played++;
    } else
      refused++;
  }
  CHECK(refused > 0 && played > 0, "of 400 damaged files %d were refused and %d played: the test should see both", refused, played);
  v2_destroy(s);
  free(copy);
}

/* v2_open fails cleanly at every allocation: nothing stays allocated */
static void check_allocation_failures(const unsigned char *song, size_t song_size)
{
  g_fail_at = -1;
  g_count = 0;
  v2_synth *s = v2_create(RATE);
  const long created = g_count;
  v2_open(s, song, song_size);
  const long total = g_count - created;
  v2_destroy(s);
  CHECK(g_live == 0, "%ld blocks leaked by a normal open and destroy", g_live);
  for (long at = 0; at < total + 2; at++) {
    g_count = 0;
    g_fail_at = created + at;
    s = v2_create(RATE);
    const v2_result r = s ? v2_open(s, song, song_size) : V2_ERR_OUT_OF_MEMORY;
    (void)r;
    v2_destroy(s);
    CHECK(g_live == 0, "allocation %ld failing leaves %ld blocks", at, g_live);
  }
  g_fail_at = -1;
}

int suite_v2(int argc, char **argv)
{
  if (argc < 3) {
    printf("usage: v2 <assets dir> <oracle fixtures dir>\n");
    return 2;
  }
  size_t song_size = 0;
  unsigned char *song = read_file(argv[1], "josie.v2m", &song_size);
  fixture fx;
  if (!song || !load_fixture(argv[2], &fx)) {
    printf("cannot read josie.v2m or oracle/josie_pcm.txt\n");
    return 1;
  }
  CHECK(v2_fp_env_ok(), "this build flushes subnormals");
  check_song_facts(song, song_size, &fx);
  check_oracle_pcm(song, song_size, &fx);
  check_control(song, song_size);
  check_damaged(song, song_size);
  check_allocation_failures(song, song_size);
  free(song);
  free(fx.hash);
  printf("v2: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
