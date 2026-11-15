/* csl_session.h - how the intro player drives the script (mainplayer.cpp, sAppHandler) - header-only C99 (stb style)

   The script has three entry points, user words 4, 1 and 2:
     4  OnGenerate    builds every bitmap, mesh, material and scene (996 native calls)
     1  timeline_OnInit  builds the splines and the effect chains of the timeline
     2  timeline_OnFrame  one picture, from the global `beat` (it resets every animated member itself: frames are independent of each other)
   The player runs 4 and 1 once, then 2 for 104 beats (the pre-roll, which fills the caches the first frames would otherwise pay for), then 2 per frame.

   The clock is the song's sample counter at 44.1 kHz; the beat is 16.16 fixed point: beat = samples * 65536 / timespeed, timespeed = 60 * 44100 * 65536 / bpm.

   gt3_session_trace_* write the marker lines of the oracle's calls.txt (PHASE, EXECUTE, GLOBALS, FRAME) around the machine's own call lines.

   #define GT3_SESSION_IMPLEMENTATION in the translation unit that has GT3_CSL_IMPLEMENTATION.
*/
#ifndef GT3_SESSION_H_INCLUDED
#define GT3_SESSION_H_INCLUDED

#include <candytron/core/csl.h>
#include <candytron/core/csl_natives.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GT3_SESSION_API
#ifdef GT3_SESSION_STATIC
#define GT3_SESSION_API static
#else
#define GT3_SESSION_API extern
#endif
#endif

enum { GT3_WORD_TIMELINE_INIT = 1, GT3_WORD_FRAME = 2, GT3_WORD_GENERATE = 4 };
enum { GT3_GLOBAL_TIME = 0, GT3_GLOBAL_BEAT = 1, GT3_GLOBAL_TICKS = 2, GT3_GLOBAL_BPM = 3, GT3_GLOBAL_BEATMAX = 4, GT3_GLOBAL_PLAYER = 5 };
#define GT3_SONG_RATE 44100

typedef struct gt3_session {
  gt3_vm *vm;
  int32_t timespeed;   /* samples per beat, 16.16 */
  int frames;          /* pictures made */
  FILE *state;         /* when set (zero the struct first, then set it), the session writes the state dumps at the oracle's points: after generation ("gen"),
                          after the timeline init ("tl"), after the pre-roll ("preroll"), after every frame ("f<n>") */
  int32_t *state_prev; /* the members written by the previous dump, per serial: 16 words */
  uint32_t state_cap;
  gt3_text_fn text;    /* set before gt3_session_init: the host's glyph coverage for Bitmap_Text (NULL: blank text) */
  void *text_user;
  gt3_pow_fn pow;      /* set before gt3_session_init: replaces gt3_fpow (the tests of the software version) */
  const uint8_t *babe; /* set before gt3_session_init: the mesh Mesh_Babe reads (NULL: an empty one) */
  size_t babe_size;
  /* set before gt3_session_init: called when a geometry has been filled / drawn (render.h gt3_geodev) */
  void (*on_geo_end)(void *user, gt3_geodev *dev, int32_t handle);
  void (*on_geo_draw)(void *user, gt3_geodev *dev, int32_t handle);
  void (*on_geo_viewport)(void *user, gt3_render *r, const gt3_fxview *view);
  gt3_trace_fn on_geo_trace; /* the oracle's draw.txt lines (needs the state of the device: costs time) */
  void *geo_user;
  int collect;               /* free the objects the script no longer reaches after every picture and pre-roll step (not while a trace or state dump is on) */
  gt3_gen gen;               /* what the generation natives share: vm->user points here from gt3_session_init on (textures: gen.texture) */
} gt3_session;

/* sAPPCODE_INIT: generation, timeline init and the pre-roll. Returns 1 when the three words ran without an error. */
GT3_SESSION_API int gt3_session_init(gt3_session *s, gt3_vm *vm);
/* sAPPCODE_PAINT at song time `seconds`. Returns 1 when the frame ran without an error. */
GT3_SESSION_API int gt3_session_frame(gt3_session *s, double seconds);

/* The members the script has written, as the oracle's state.txt has them: for the objects the trace has shown (they have a serial), the members the
   script can reach, and only what changed since the previous dump:   S <tag> o<serial> <class> <member words in hex>
   Needs the trace to be on (the serials come from it). */
GT3_SESSION_API void gt3_session_dump_state(gt3_session *s, FILE *f, const char *tag);
GT3_SESSION_API void gt3_session_exit(gt3_session *s);

#ifdef __cplusplus
}
#endif

#endif /* GT3_SESSION_H_INCLUDED */

#ifdef GT3_SESSION_IMPLEMENTATION
#ifndef GT3_SESSION_IMPLEMENTATION_DONE
#define GT3_SESSION_IMPLEMENTATION_DONE

#include <stdlib.h>
#include <string.h>

/* the members as the effect chain is rendered: the script sets them for the call of RenderChain and puts them back after it, so only this moment shows
   what the picture is made of (the oracle dumps at the same moment, tag rc<number of the call>) */
static void gt3_session_on_native(gt3_vm *vm, int id, void *user)
{
  gt3_session *s = (gt3_session *)user;
  if (id == 0x102 && s->state) {
    char tag[32];
    snprintf(tag, sizeof tag, "rc%d", vm->trace_seq - 1);
    gt3_session_dump_state(s, s->state, tag);
  }
}

GT3_SESSION_API int gt3_session_init(gt3_session *s, gt3_vm *vm)
{
  vm->on_native = gt3_session_on_native;
  vm->on_native_user = s;
  s->vm = vm;
  s->frames = 0;
  gt3_gen_init(&s->gen); /* sInitPerlin and the render targets come before the script */
  s->gen.text = s->text;
  s->gen.text_user = s->text_user;
  s->gen.pow = s->pow;
  s->gen.babe = s->babe;
  s->gen.babe_size = s->babe_size;
  if (s->gen.render) {
    s->gen.render->on_end = s->on_geo_end;
    s->gen.render->on_draw = s->on_geo_draw;
    s->gen.render->on_viewport = s->on_geo_viewport;
    s->gen.render->trace = s->on_geo_trace;
    s->gen.render->user = s->geo_user;
  }
  vm->user = &s->gen;
  vm->phase = "init";
  if (vm->trace) fprintf(vm->trace, "PHASE init\n");
  gt3_set_global(vm, GT3_GLOBAL_BPM, 140 * 0x10);
  gt3_set_global(vm, GT3_GLOBAL_PLAYER, 0);
  vm->show_progress = 1;
  int ok = 1;
  if (vm->trace) fprintf(vm->trace, "EXECUTE 4\n");
  ok &= gt3_vm_execute(vm, GT3_WORD_GENERATE);
  if (s->state) gt3_session_dump_state(s, s->state, "gen");
  if (vm->trace) fprintf(vm->trace, "EXECUTE 1\n");
  ok &= gt3_vm_execute(vm, GT3_WORD_TIMELINE_INIT);
  if (s->state) gt3_session_dump_state(s, s->state, "tl");
  s->timespeed = gt3_mul_div(60 * GT3_SONG_RATE, 0x10000, gt3_global(vm, GT3_GLOBAL_BPM));
  if (vm->trace)
    fprintf(vm->trace, "GLOBALS bpm=%08x beatmax=%08x timespeed=%d\n", (unsigned)gt3_global(vm, GT3_GLOBAL_BPM), (unsigned)gt3_global(vm, GT3_GLOBAL_BEATMAX),
            (int)s->timespeed);
  vm->phase = "preroll";
  for (int32_t beat = 0x00000000; beat < 0x01a00000; beat += 0x00040000) {
    gt3_set_global(vm, GT3_GLOBAL_BEAT, beat);
    if (vm->trace) fprintf(vm->trace, "EXECUTE 2 preroll beat=%08x\n", (unsigned)beat);
    if (s->gen.render) gt3_render_frame_begin(s->gen.render);
    ok &= gt3_vm_execute(vm, GT3_WORD_FRAME);
    if (s->gen.render) gt3_render_frame_end(s->gen.render);
    if (s->collect) gt3_vm_collect(vm);
  }
  if (s->state) gt3_session_dump_state(s, s->state, "preroll");
  vm->progress++; /* ProgressBar(1) at the end of sAPPCODE_INIT */
  vm->show_progress = 0;
  return ok;
}

GT3_SESSION_API int gt3_session_frame(gt3_session *s, double seconds)
{
  gt3_vm *vm = s->vm;
  const int32_t sample = (int32_t)(seconds * (double)GT3_SONG_RATE);
  const int32_t beat = gt3_mul_div(sample, 0x10000, s->timespeed);
  vm->phase = "frame";
  s->frames++;
  if (vm->trace) { fprintf(vm->trace, "FRAME %d t=%g sample=%d beat=%08x\n", s->frames, seconds, (int)sample, (unsigned)beat); }
  if (s->gen.render) gt3_render_frame_begin(s->gen.render);
  gt3_set_global(vm, GT3_GLOBAL_BEAT, beat);
  if (s->gen.render) gt3_fxm_reset(s->gen.render); /* FXMaster->ResetAlloc() of the player's paint handler */
  const int ok = gt3_vm_execute(vm, GT3_WORD_FRAME);
  if (s->gen.render) gt3_render_frame_end(s->gen.render);
  if (s->collect) gt3_vm_collect(vm);
  if (s->state) {
    char tag[32];
    snprintf(tag, sizeof tag, "f%d", s->frames);
    gt3_session_dump_state(s, s->state, tag);
  }
  return ok;
}

static int gt3_state_members(uint32_t cls)
{
  switch (cls) {
  case GT3_CID_SCENE: return 10;
  case GT3_CID_FXCHAIN: return 15;
  case GT3_CID_MATRIX: return 9;
  case GT3_CID_MESHANIM: return 13;
  case GT3_CID_MATPASS: return 8;
  default: return 0;
  }
}

GT3_SESSION_API void gt3_session_dump_state(gt3_session *s, FILE *f, const char *tag)
{
  gt3_vm *vm = s->vm;
  if ((uint32_t)vm->trace_next_serial > s->state_cap) {
    const uint32_t cap = (uint32_t)vm->trace_next_serial + 1024u;
    int32_t *bigger = (int32_t *)realloc(s->state_prev, (size_t)cap * 16u * sizeof *bigger);
    if (!bigger) return;
    memset(bigger + (size_t)s->state_cap * 16u, 0, (size_t)(cap - s->state_cap) * 16u * sizeof *bigger);
    s->state_prev = bigger;
    s->state_cap = cap;
  }
  for (int serial = 1; serial <= vm->trace_next_serial; serial++) {
    const gt3_object *o = gt3_object_get(vm, vm->trace_handle[serial - 1]);
    const int n = o ? gt3_state_members(o->cls) : 0;
    if (!n) continue;
    int32_t *prev = s->state_prev + (size_t)(serial - 1) * 16u;
    int same = prev[15] == 1;
    for (int i = 0; i < n && same; i++) same = prev[i] == o->m[i];
    if (same) continue;
    memcpy(prev, o->m, sizeof o->m);
    prev[15] = 1; /* m[15] of the object is its render handler: the slot of the cache is used as a flag */
    fprintf(f, "S %s o%d %08x", tag, serial, (unsigned)o->cls);
    for (int i = 0; i < n; i++) fprintf(f, " %08x", (unsigned)o->m[i]);
    fprintf(f, "\n");
  }
}

GT3_SESSION_API void gt3_session_exit(gt3_session *s)
{
  if (s->vm && s->vm->user == &s->gen) s->vm->user = NULL;
  gt3_gen_exit(&s->gen);
  free(s->state_prev);
  s->state_prev = NULL;
  s->state_cap = 0;
}

#endif /* GT3_SESSION_IMPLEMENTATION_DONE */
#endif /* GT3_SESSION_IMPLEMENTATION */
