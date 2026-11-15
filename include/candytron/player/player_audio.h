/* player_audio.h - the SDL-free halves of the player's sound (stb-style single header)

   pclock  The picture's master clock. It turns "frames the audio device has played" (put - queued - device latency, read from
           any audio API) into a song position that advances smoothly between polls, never runs backwards and never leads the
           audio that is audible. The original draws the picture at the audio frame counter; so does the player.
   paudio  The song: one v2.h synth, the count of frames rendered (the clock's "put"), silence while it does not play.

   Neither part touches SDL, a thread or a clock of its own: main.c supplies the time and the locking, the tests supply a
   simulated device.

     #define PLAYER_AUDIO_IMPLEMENTATION
     #include <candytron/player/player_audio.h>
*/
#ifndef PLAYER_AUDIO_H_INCLUDED
#define PLAYER_AUDIO_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#ifndef V2_H_INCLUDED
#include <candytron/core/v2.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PA_API
#ifdef PLAYER_AUDIO_STATIC
#define PA_API static
#else
#define PA_API extern
#endif
#endif

/* ---- clock ---- */

typedef struct pclock {
  double rate;           /* song sample rate in Hz */
  int64_t anchor_sample; /* the last played-frame count the device reported ... */
  uint64_t anchor_ns;    /* ... and the time at which the caller noticed it */
  int64_t reported;      /* last reported value (a change re-anchors) */
  int64_t last;          /* last value handed out: the clock never goes below it */
  uint64_t max_extrapolate_ns;
  int running;
} pclock;

/* max_extrapolate_ms bounds how far the clock may run past the last change of the reported value (an underrunning stream
   must not let the picture race ahead of the sound). */
PA_API void pclock_init(pclock *c, uint32_t sample_rate, uint32_t max_extrapolate_ms);
/* Start or seek: the song position is `sample` now. */
PA_API void pclock_reset(pclock *c, int64_t sample, uint64_t now_ns);
/* played = frames put to the device - frames still queued - device latency. */
PA_API void pclock_report(pclock *c, int64_t played, uint64_t now_ns);
PA_API void pclock_set_running(pclock *c, int running, uint64_t now_ns);
/* The song position in frames. Monotonic between resets; while stopped it returns the last value. */
PA_API int64_t pclock_now(pclock *c, uint64_t now_ns);

/* ---- song ---- */

typedef struct paudio {
  v2_synth *synth;
  uint32_t rate;
  uint64_t frames;        /* frames rendered since the start or the last seek */
  uint64_t base;          /* the song position of frame 0 of that count, in frames: base + frames is the clock's "put" */
  uint64_t length_frames; /* the song's length from its sequence timing (the tail rings on after it) */
} paudio;

PA_API void paudio_init(paudio *a, uint32_t sample_rate);
PA_API void paudio_destroy(paudio *a);
/* Parses the song (the caller's bytes may go) and starts it at the beginning. */
PA_API v2_result paudio_play(paudio *a, const void *v2m, size_t size);
/* Restarts the song at from_ms: the synth and the sequencer begin there (the reverb tails of what came before are gone). 0 on success, -1 with no song. */
PA_API int paudio_seek(paudio *a, uint32_t from_ms);
/* Interleaved stereo float, clipped to +-1 as the original's output is (the synthesizer itself peaks at about 1.25). Silence when no song plays. */
PA_API void paudio_render(paudio *a, float *out, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* PLAYER_AUDIO_H_INCLUDED */

#ifdef PLAYER_AUDIO_IMPLEMENTATION
#ifndef PLAYER_AUDIO_IMPLEMENTATION_DONE
#define PLAYER_AUDIO_IMPLEMENTATION_DONE

#include <string.h>

PA_API void pclock_init(pclock *c, uint32_t sample_rate, uint32_t max_extrapolate_ms)
{
  memset(c, 0, sizeof *c);
  c->rate = (double)sample_rate;
  c->max_extrapolate_ns = (uint64_t)max_extrapolate_ms * 1000000ull;
}

PA_API void pclock_reset(pclock *c, int64_t sample, uint64_t now_ns)
{
  c->anchor_sample = c->reported = c->last = sample;
  c->anchor_ns = now_ns;
}

PA_API void pclock_report(pclock *c, int64_t played, uint64_t now_ns)
{
  if (played == c->reported) return;
  c->reported = played;
  c->anchor_sample = played;
  c->anchor_ns = now_ns;
}

PA_API void pclock_set_running(pclock *c, int running, uint64_t now_ns)
{
  if (running && !c->running) { /* extrapolate from where the picture stopped */
    c->anchor_sample = c->last;
    c->anchor_ns = now_ns;
  }
  c->running = running != 0;
}

PA_API int64_t pclock_now(pclock *c, uint64_t now_ns)
{
  if (!c->running) return c->last;
  uint64_t elapsed = now_ns > c->anchor_ns ? now_ns - c->anchor_ns : 0;
  if (elapsed > c->max_extrapolate_ns) elapsed = c->max_extrapolate_ns;
  const int64_t estimate = c->anchor_sample + (int64_t)((double)elapsed * c->rate / 1e9);
  if (estimate > c->last) c->last = estimate;
  return c->last;
}

PA_API void paudio_init(paudio *a, uint32_t sample_rate)
{
  memset(a, 0, sizeof *a);
  a->rate = sample_rate;
}

PA_API void paudio_destroy(paudio *a)
{
  v2_destroy(a->synth);
  a->synth = NULL;
}

PA_API v2_result paudio_play(paudio *a, const void *v2m, size_t size)
{
  paudio_destroy(a);
  v2_synth *s = v2_create(a->rate);
  if (!s) return V2_ERR_OUT_OF_MEMORY;
  const v2_result res = v2_open(s, v2m, size);
  if (res != V2_OK) {
    v2_destroy(s);
    return res;
  }
  a->length_frames = v2_length_frames(s); /* implies a reset: before v2_play */
  v2_play(s, 0);
  a->synth = s;
  a->frames = a->base = 0;
  return V2_OK;
}

PA_API int paudio_seek(paudio *a, uint32_t from_ms)
{
  if (!a->synth) return -1;
  v2_play(a->synth, from_ms);
  a->frames = 0;
  a->base = (uint64_t)((double)from_ms * a->rate / 1000.0);
  return 0;
}

PA_API void paudio_render(paudio *a, float *out, uint32_t frames)
{
  if (!a->synth) {
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    return;
  }
  v2_render(a->synth, out, frames);
  for (size_t i = 0; i < (size_t)frames * 2; i++) {
    if (out[i] > 1.0f) out[i] = 1.0f;
    if (out[i] < -1.0f) out[i] = -1.0f;
  }
  a->frames += frames;
}

#endif /* PLAYER_AUDIO_IMPLEMENTATION_DONE */
#endif /* PLAYER_AUDIO_IMPLEMENTATION */
