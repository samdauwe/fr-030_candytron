/* The player's sound: the picture clock and the song.
 *
 *   suite_player_audio <assets dir>
 *
 * pclock: a simulated audio device (it reports whole periods, polled by a jittery frame loop) must never see the clock run
 * backwards, never lead the audio, and lag it by no more than one period plus one poll interval; pauses, seeks and an underrun are
 * modelled. paudio: silence before a song plays, the frame counter, a song asked for in pieces of any size renders what one call
 * renders, a second song replaces the first, refused songs leave nothing behind.
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
#define PLAYER_AUDIO_STATIC
#define PLAYER_AUDIO_IMPLEMENTATION
#include <candytron/player/player_audio.h>

static uint64_t g_rng = 0x9e3779b97f4a7c15ull;
static uint32_t rnd(void)
{
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 7;
  g_rng ^= g_rng << 17;
  return (uint32_t)(g_rng >> 32);
}
static double frand(double lo, double hi) { return lo + (hi - lo) * ((double)rnd() / 4294967296.0); }

/* ---- the simulated device ---- */

typedef struct sim {
  double rate, period; /* Hz, frames */
  double play_time;    /* seconds of audio the device has actually played */
  int device_running;
} sim;

/* what the device reports: whole periods of the true position */
static int64_t sim_reported(const sim *s) { return (int64_t)(s->period * (double)(int64_t)(s->rate * s->play_time / s->period)); }
static double sim_truth(const sim *s) { return s->rate * s->play_time; }

static int was_paused_now(int was_paused, double t, double paused_until) { return was_paused && t < paused_until + 0.2; }
static int sought_now(int sought, double t) { return sought && t > 15.0 && t < 15.2; }

static void run_clock(uint32_t rate, uint32_t period, double poll_lo, double poll_hi, int use_pause, int use_underrun, int use_seek)
{
  pclock c;
  sim s = { (double)rate, (double)period, 0, 1 };
  const uint32_t cap_ms = 100;
  pclock_init(&c, rate, cap_ms);
  uint64_t now = 1000000000ull; /* an arbitrary monotonic epoch */
  pclock_reset(&c, 0, now);
  pclock_set_running(&c, 1, now);
  int64_t prev = 0;
  double worst_lead = -1e9, worst_lag = 0, base = 0; /* base: the seek position */
  const double max_poll = poll_hi;
  int paused = 0, starved = 0, sought = 0, was_paused = 0, was_starved = 0;
  double paused_until = 0, starved_until = 0, t = 0, last_change_t = 0, prev_t = 0;
  int64_t prev_report = 0;
  while (t < 20.0) {
    double dt = frand(poll_lo, poll_hi);
    if ((rnd() & 255) == 0) dt = 0.09; /* a hiccup */
    t += dt;
    now += (uint64_t)(dt * 1e9);
    if (use_pause && !paused && !was_paused && t > 5.0) {
      was_paused = 1;
      paused = 1;
      paused_until = t + 1.0;
      pclock_set_running(&c, 0, now);
      s.device_running = 0;
    }
    if (paused && t >= paused_until) {
      paused = 0;
      pclock_set_running(&c, 1, now);
      s.device_running = 1;
    }
    if (use_underrun && !starved && !was_starved && t > 10.0) {
      was_starved = 1;
      starved = 1;
      starved_until = t + 1.0;
      s.device_running = 0;
    }
    if (starved && t >= starved_until) {
      starved = 0;
      s.device_running = 1;
    }
    const int recovering = was_starved && t < starved_until + (double)cap_ms / 1000.0 + 0.05;
    if (use_seek && !sought && t > 15.0) {
      sought = 1;
      s.play_time = 0;
      base = 90.0 * rate;
      pclock_reset(&c, (int64_t)base, now);
    }
    if (s.device_running) s.play_time += dt;
    const int64_t report = (int64_t)base + sim_reported(&s);
    if (report != prev_report) {
      last_change_t = t;
      prev_report = report;
    }
    pclock_report(&c, report, now);
    const int64_t v = pclock_now(&c, now);
    /* between changes of the reported value the clock keeps running with wall time, it does not step with the device */
    if (!paused && !starved && !was_paused_now(was_paused, t, paused_until) && t - last_change_t < (double)cap_ms / 1000.0 && t - prev_t > 0 &&
        prev_t > last_change_t && !sought_now(sought, t))
      CHECK(v - prev >= (int64_t)((t - prev_t) * rate) - 2, "clock stalled between reports: advanced %lld of %.0f frames (t=%.3f)", (long long)(v - prev),
            (t - prev_t) * rate, t);
    prev_t = t;
    const double truth = base + sim_truth(&s);
    CHECK(v >= prev, "clock went backwards: %lld -> %lld (t=%.3f)", (long long)prev, (long long)v, t);
    prev = v;
    const double lead = (double)v - truth;
    if (lead > worst_lead) worst_lead = lead;
    if (!starved) {
      const double lag = truth - (double)v;
      if (lag > worst_lag && !paused) worst_lag = lag;
    }
    /* an underrun lets the clock run on for at most the extrapolation cap */
    const double allowed_lead = (starved || recovering) ? (double)cap_ms * rate / 1000.0 + 2 : (double)rate / 1000.0;
    CHECK(lead <= allowed_lead, "clock leads the audio by %.1f frames (t=%.3f, paused %d, starved %d)", lead, t, paused, starved);
  }
  const double lag_bound = (double)period + max_poll * rate + 2;
  CHECK(worst_lag <= lag_bound, "clock lags by %.1f frames (bound %.1f) rate %u period %u", worst_lag, lag_bound, rate, period);
}

static void test_clock(void)
{
  static const uint32_t rates[] = { 44100, 48000 };
  static const uint32_t periods[] = { 256, 512, 1024, 2048 };
  for (int r = 0; r < 2; r++)
    for (int p = 0; p < 4; p++) {
      run_clock(rates[r], periods[p], 0.004, 0.025, 0, 0, 0);
      run_clock(rates[r], periods[p], 0.010, 0.040, 1, 1, 1);
    }
  /* while stopped, the clock holds */
  pclock c;
  pclock_init(&c, 44100, 250);
  pclock_reset(&c, 1000, 0);
  CHECK(pclock_now(&c, 5000000000ull) == 1000, "a stopped clock moved");
  pclock_set_running(&c, 1, 5000000000ull);
  CHECK(pclock_now(&c, 5000000000ull + 100000000ull) == 1000 + 4410, "resume extrapolation");
}

/* ---- the song ---- */

static unsigned char *read_file(const char *dir, const char *name, size_t *size)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *d = (unsigned char *)malloc((size_t)n);
  if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) {
    free(d);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *size = (size_t)n;
  return d;
}

static void render_chunked(paudio *a, float *out, uint32_t frames)
{
  uint32_t done = 0;
  while (done < frames) {
    uint32_t n = 1 + rnd() % 3000;
    if (n > frames - done) n = frames - done;
    paudio_render(a, out + (size_t)done * 2, n);
    done += n;
  }
}

static void test_paudio(const unsigned char *song, size_t song_size)
{
  enum { FRAMES = 60000 };
  float *a_out = (float *)malloc(FRAMES * 2 * sizeof(float));
  float *b_out = (float *)malloc(FRAMES * 2 * sizeof(float));
  const long base = g_live;

  paudio a;
  paudio_init(&a, 44100);
  memset(a_out, 0x55, 64);
  paudio_render(&a, a_out, 8);
  int silent = 1;
  for (int i = 0; i < 16; i++) silent &= a_out[i] == 0.0f;
  CHECK(silent, "no silence before a song plays");
  CHECK(a.frames == 0, "frames counted without a song");

  CHECK(paudio_play(&a, song, song_size) == V2_OK && a.synth, "play josie.v2m");
  CHECK(a.length_frames > 8000000 && a.length_frames < 9000000, "song length %llu frames", (unsigned long long)a.length_frames);
  render_chunked(&a, a_out, FRAMES);
  CHECK(a.frames == FRAMES, "frame counter %llu", (unsigned long long)a.frames);

  /* the same song in one call renders the same */
  CHECK(paudio_play(&a, song, song_size) == V2_OK, "play again");
  CHECK(a.frames == 0, "the counter restarts with the song");
  paudio_render(&a, b_out, FRAMES);
  CHECK(memcmp(a_out, b_out, (size_t)FRAMES * 2 * sizeof(float)) == 0, "chunked rendering differs from one call");
  double peak = 0;
  for (int i = 0; i < FRAMES * 2; i++) peak = a_out[i] > peak ? a_out[i] : a_out[i] < -peak ? -a_out[i] : peak;
  CHECK(peak > 0.05, "a second and a half of the song is silent (peak %g)", peak);
  CHECK(g_live - base <= 8, "%ld blocks alive for one song", g_live - base);

  /* the output is clipped to +-1 (the synthesizer peaks at 1.2 in the first minute) */
  {
    CHECK(paudio_play(&a, song, song_size) == V2_OK, "play for the clipping test");
    float block[4096 * 2];
    double peak_out = 0;
    uint64_t at_one = 0;
    for (uint64_t done = 0; done < 45 * 44100; done += 4096) {
      paudio_render(&a, block, 4096);
      for (int i = 0; i < 4096 * 2; i++) {
        const double v = block[i] < 0 ? -block[i] : block[i];
        if (v > peak_out) peak_out = v;
        at_one += v == 1.0;
      }
    }
    CHECK(peak_out == 1.0 && at_one > 0, "the peak of 45 s is %g with %llu samples at the limit", peak_out, (unsigned long long)at_one);
  }

  /* a seek is a fresh start at that time: the same bits as a synth that began there, and the counters say where the song is */
  {
    CHECK(paudio_play(&a, song, song_size) == V2_OK, "play for the seek test");
    float first[4096 * 2], second[4096 * 2];
    paudio_render(&a, first, 4096);
    CHECK(paudio_seek(&a, 30000) == 0 && a.frames == 0 && a.base == 30 * 44100, "counters after a seek: base %llu frames %llu", (unsigned long long)a.base,
          (unsigned long long)a.frames);
    paudio_render(&a, first, 4096);
    CHECK(a.base + a.frames == 30 * 44100 + 4096, "position after rendering %llu", (unsigned long long)(a.base + a.frames));
    paudio_seek(&a, 30000);
    paudio_render(&a, second, 4096);
    CHECK(memcmp(first, second, sizeof first) == 0, "two seeks to 30 s render differently");
    paudio_destroy(&a);
    CHECK(paudio_seek(&a, 1000) == -1, "a seek without a song");
  }

  /* a refused song leaves the player silent and without a synth */
  CHECK(paudio_play(&a, "not a song at all", 17) != V2_OK && !a.synth, "garbage played");
  memset(a_out, 0x55, 64);
  paudio_render(&a, a_out, 8);
  silent = 1;
  for (int i = 0; i < 16; i++) silent &= a_out[i] == 0.0f;
  CHECK(silent, "no silence after a refused song");
  paudio_destroy(&a);
  CHECK(g_live == base, "%ld blocks leaked", g_live - base);

  /* every allocation failure of the start */
  for (long at = 0; at < 40; at++) {
    g_count = 0;
    g_fail_at = at;
    paudio_init(&a, 44100);
    const v2_result r = paudio_play(&a, song, song_size);
    CHECK(r == V2_OK || !a.synth, "a failed start (allocation %ld) leaves a synth", at);
    paudio_destroy(&a);
    CHECK(g_live == base, "allocation %ld failing leaves %ld blocks", at, g_live - base);
  }
  g_fail_at = -1;
  free(a_out);
  free(b_out);
}

int suite_player_audio(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: player_audio <assets dir>\n");
    return 2;
  }
  size_t song_size = 0;
  unsigned char *song = read_file(argv[1], "josie.v2m", &song_size);
  if (!song) {
    printf("cannot read %s/josie.v2m\n", argv[1]);
    return 1;
  }
  test_clock();
  test_paudio(song, song_size);
  free(song);
  printf("player_audio: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
