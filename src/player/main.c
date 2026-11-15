#define _POSIX_C_SOURCE 200809L
/* candytron: the player. SDL3 window and OpenGL ES 3 (or GL 3.3 core) context for fr-030: Candytron.
 *
 *   candytron [--assets DIR] [--size WxH] [--aspect W:H] [--fullscreen] [--no-vsync] [--exit-after SECONDS] [--gl-check]
 *             [--start SECONDS] [--capture DIR [--times T,T,...]] [--soak SECONDS [--soak-rate R]] [--selftest] [--verify-assets] [--version] [--licenses]
 *
 * The player finds the production's data (assets/default.pac, josie.v2m, babe.mesh.old: next to the executable, in ./assets, or in --assets),
 * refuses to start unless every file is the production's (size, SHA-256 and the outer structure, assets.h), runs the script (generation,
 * timeline init, pre-roll: the original's sAPPCODE_INIT) and plays it: every picture is made by the script's virtual machine at the song's time
 * and drawn by the OpenGL ES 3 backend (gles.h) into an 800 x 600 target that is shown in the window with the picture's 4:3 shape. The
 * soundtrack is the song rendered by the V2 synthesizer (v2.h) in the audio callback; the picture follows the audio's frame counter (player_audio.h),
 * as the original does. Without an audio device the picture runs on the wall clock.
 *
 * Keys: F fullscreen, Space pause, Left and Right 10 seconds back and forward (the sound restarts there), Home the beginning, Esc quit. Every picture
 * is made for a song time alone (nothing carries over from the one before), which is what makes seeking free.
 *
 * Options for tests and measurements (the test runner starts the player with them; see tests/suites/player_cli.c):
 *   --verify-assets  check the data and exit; no window
 *   --exit-after S   quit S seconds after the process started
 *   --gl-check       poll glGetError after every frame; errors are failures
 *   --capture DIR    make the pictures at the song times of --times (default: 0, 5, ... 195 s; no clock), write DIR/frame_<NNN>.ppm (numbered from 001) and the SHA-256
 *                    of each on stdout, and exit
 *   --start S        begin at song time S
 *   --soak S         make S seconds of pictures at --soak-rate (30) a second, as fast as possible, the song's time wrapping at its end, and say whether the memory grew
 *   --selftest       draw the renderer's unit scenes and compare them with the Direct3D formulas (selftest.h); no data needed
 *   --version, --licenses
 */
#define CT_SHA256_IMPLEMENTATION
#define CT_ASSETS_IMPLEMENTATION
#include <candytron/core/assets.h>
#define CT_GL_IMPLEMENTATION
#include <candytron/render/gl.h>
#define PLAYER_ARGS_IMPLEMENTATION
#include <candytron/player/player_args.h>
#define PLAYER_VIEW_IMPLEMENTATION
#include <candytron/player/player_view.h>
#define PLAYER_CREDITS_IMPLEMENTATION
#include <candytron/player/player_credits.h>
#include "licenses_data.h"
#define PLAYER_GLCHECK_IMPLEMENTATION
#include <candytron/player/player_glcheck.h>
#define GT3_CSL_STATIC
#define GT3_CSL_IMPLEMENTATION
#include <candytron/core/csl.h>
#define GT3_BITMAP_STATIC
#define GT3_BITMAP_IMPLEMENTATION
#define GT3_MESH_STATIC
#define GT3_MESH_IMPLEMENTATION
#define GT3_RENDER_STATIC
#define GT3_RENDER_IMPLEMENTATION
#define GT3_BABE_STATIC
#define GT3_BABE_IMPLEMENTATION
#define GT3_NATIVES_STATIC
#define GT3_NATIVES_IMPLEMENTATION
#include <candytron/core/csl_natives.h>
#define GT3_SESSION_STATIC
#define GT3_SESSION_IMPLEMENTATION
#include <candytron/core/csl_session.h>
#define GT3_TEXT_STATIC
#define GT3_TEXT_IMPLEMENTATION
#include <candytron/core/text.h>
#define CT_GLES_STATIC
#define CT_GLES_IMPLEMENTATION
#include <candytron/render/gles.h>
#define V2_IMPLEMENTATION
#include <candytron/core/v2.h>
#define PLAYER_AUDIO_IMPLEMENTATION
#include <candytron/player/player_audio.h>
#include "selftest.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/heap.h>
#endif

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <unistd.h>
#endif

#define CANDYTRON_VERSION "0.1.0"
#define GL_ERROR_QUEUE    16 /* errors read per poll */

typedef struct app {
  poptions opt;
  ct_assets assets;
  SDL_Window *window;
  SDL_GLContext context;
  ct_gl gl;
  pglcheck glcheck;
  uint64_t start_ns, frames;
  int rc; /* exit status of the live loop, valid once live_frame() returns 0 */
  int es; /* the context is OpenGL ES 3.0 (else 3.3 core) */
  /* the demo */
  gt3_program program;
  gt3_vm *vm;
  gt3_session session;
  ct_gles gles;
  int demo, gles_ok;
  uint64_t demo_start_ns;
  /* the sound: the song's synth runs in the audio callback, the picture follows its frame counter */
  paudio audio;
  pclock clock;
  SDL_AudioStream *stream;
  uint32_t device_latency_frames;
  double last_song;   /* the song time of the last picture, seconds */
  int paused;
  double paused_song; /* without audio: the song time the pause stopped at */
  /* what the pictures cost: CPU time of the script, the scene and the draw calls, and of the presentation; the heap */
  uint64_t perf_n, perf_slow;
  double perf_pic_sum, perf_pic_worst, perf_present_sum;
  uint64_t perf_first_ns, perf_last_ns; /* the clock at the first and the last picture of a live run */
  size_t heap_peak;
} app;

static int fail(const char *what, const char *detail)
{
  fprintf(stderr, "candytron: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
  return 1;
}

/* ---- the data ---- */

#if !defined(__EMSCRIPTEN__)
static int file_exists(const char *dir, const char *name)
{
  char path[2048];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "rb");
  if (f) fclose(f);
  return f != NULL;
}
#endif

/* --assets, else the web's /assets, else assets/ next to the executable, else ./assets */
static const char *find_assets_dir(const app *a, char *buffer, size_t size)
{
  if (a->opt.assets_dir) return a->opt.assets_dir;
#if defined(__EMSCRIPTEN__)
  (void)buffer;
  (void)size;
  return "/assets";
#else
  const char *base = SDL_GetBasePath();
  if (base && snprintf(buffer, size, "%sassets", base) < (int)size && file_exists(buffer, ct_asset_describe(CT_ASSET_SCRIPT)->file)) return buffer;
  return "assets";
#endif
}

static int load_assets(app *a)
{
  char buffer[1024], error[512] = "";
  const char *dir = find_assets_dir(a, buffer, sizeof buffer);
  if (!ct_assets_load(&a->assets, dir, error, sizeof error)) return fail("the production data", error);
  return 0;
}

static void print_assets(const app *a)
{
  for (int i = 0; i < CT_ASSET_COUNT; i++) {
    const ct_asset_info *info = ct_asset_describe((ct_asset_id)i);
    printf("assets: %-14s %6lu bytes  sha256 %.16s...  %s\n", info->file, (unsigned long)a->assets.size[i], info->sha256, info->what);
  }
  ct_pac_header pac;
  ct_v2m_header v2m;
  char error[256];
  if (ct_pac_parse_header(a->assets.data[CT_ASSET_SCRIPT], a->assets.size[CT_ASSET_SCRIPT], &pac, error, sizeof error))
    printf("assets: script: %u code bytes, %u immediates, %u functions, %u variable references, %u string bytes\n", pac.code_bytes, pac.imm_words,
           pac.func_count, pac.var_refs, pac.string_bytes);
  if (ct_v2m_parse_header(a->assets.data[CT_ASSET_SONG], a->assets.size[CT_ASSET_SONG], &v2m, error, sizeof error))
    printf("assets: song: %u ticks per beat, %u ticks, %u global events\n", v2m.timediv, v2m.maxtime, v2m.globals);
  printf("assets: ok\n");
}

/* ---- OpenGL ---- */

static void *proc_address(const char *name)
{
#if defined(__EMSCRIPTEN__)
  /* The GL functions are linked directly: no glGetProcAddress table, and nothing that is not called is linked. */
#define PGL_WEB_FN(ret, fn, args) \
  if (!strcmp(name, "gl" #fn)) return (void *)gl##fn;
  CT_GL_FUNCTIONS(PGL_WEB_FN)
#undef PGL_WEB_FN
  return NULL;
#else
  const SDL_FunctionPointer f = SDL_GL_GetProcAddress(name);
  void *p = NULL;
  memcpy(&p, &f, sizeof p < sizeof f ? sizeof p : sizeof f);
  return p;
#endif
}

static int create_window(app *a)
{
  SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (a->opt.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
  static const struct {
    int profile, major, minor;
  } contexts[] = { { SDL_GL_CONTEXT_PROFILE_ES, 3, 0 }, { SDL_GL_CONTEXT_PROFILE_CORE, 3, 3 } };
  for (size_t i = 0; i < sizeof contexts / sizeof contexts[0] && !a->context; i++) {
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, contexts[i].profile);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, contexts[i].major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, contexts[i].minor);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
#if defined(__EMSCRIPTEN__)
    /* A canvas with alpha is composited with the page, one without ignores it as a native window does. */
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
#endif
    a->window = SDL_CreateWindow("fr-030: Candytron", a->opt.width, a->opt.height, flags);
    if (!a->window) return fail("window", SDL_GetError());
    a->context = SDL_GL_CreateContext(a->window);
    a->es = contexts[i].profile == SDL_GL_CONTEXT_PROFILE_ES;
    if (!a->context) {
      SDL_DestroyWindow(a->window);
      a->window = NULL;
    }
  }
  if (!a->context) return fail("OpenGL context (ES 3.0 and 3.3 core failed)", SDL_GetError());
  SDL_GL_SetSwapInterval(a->opt.vsync ? 1 : 0);
  const char *missing = ct_gl_load(&a->gl, proc_address);
  if (missing) return fail("the driver lacks", missing);
  pglcheck_init(&a->glcheck);
  return 0;
}

/* --gl-check counts what the player asks of the driver: the wrappers sit between the renderer and the table the loader filled */
static ct_gl g_gl_real;
static pglcheck *g_gl_count;

#define COUNT_GEN(fn, kind)                     \
  static void count_##fn(int n, unsigned *ids)  \
  {                                             \
    g_gl_real.fn(n, ids);                       \
    pglcheck_created(g_gl_count, kind, n, ids); \
  }
#define COUNT_DEL(fn, kind)                          \
  static void count_##fn(int n, const unsigned *ids) \
  {                                                  \
    pglcheck_deleted(g_gl_count, kind, n, ids);      \
    g_gl_real.fn(n, ids);                            \
  }
COUNT_GEN(GenTextures, PGL_TEXTURE)
COUNT_DEL(DeleteTextures, PGL_TEXTURE)
COUNT_GEN(GenBuffers, PGL_BUFFER)
COUNT_DEL(DeleteBuffers, PGL_BUFFER)
COUNT_GEN(GenVertexArrays, PGL_VERTEX_ARRAY)
COUNT_DEL(DeleteVertexArrays, PGL_VERTEX_ARRAY)
COUNT_GEN(GenFramebuffers, PGL_FRAMEBUFFER)
COUNT_DEL(DeleteFramebuffers, PGL_FRAMEBUFFER)
COUNT_GEN(GenRenderbuffers, PGL_RENDERBUFFER)
COUNT_DEL(DeleteRenderbuffers, PGL_RENDERBUFFER)
static unsigned count_CreateProgram(void)
{
  const unsigned id = g_gl_real.CreateProgram();
  pglcheck_created(g_gl_count, PGL_PROGRAM, 1, &id);
  return id;
}
static void count_DeleteProgram(unsigned id)
{
  pglcheck_deleted(g_gl_count, PGL_PROGRAM, 1, &id);
  g_gl_real.DeleteProgram(id);
}
static unsigned count_CreateShader(unsigned type)
{
  const unsigned id = g_gl_real.CreateShader(type);
  pglcheck_created(g_gl_count, PGL_SHADER, 1, &id);
  return id;
}
static void count_DeleteShader(unsigned id)
{
  pglcheck_deleted(g_gl_count, PGL_SHADER, 1, &id);
  g_gl_real.DeleteShader(id);
}

static void install_counters(app *a)
{
  g_gl_real = a->gl;
  g_gl_count = &a->glcheck;
  a->gl.GenTextures = count_GenTextures;
  a->gl.DeleteTextures = count_DeleteTextures;
  a->gl.GenBuffers = count_GenBuffers;
  a->gl.DeleteBuffers = count_DeleteBuffers;
  a->gl.GenVertexArrays = count_GenVertexArrays;
  a->gl.DeleteVertexArrays = count_DeleteVertexArrays;
  a->gl.GenFramebuffers = count_GenFramebuffers;
  a->gl.DeleteFramebuffers = count_DeleteFramebuffers;
  a->gl.GenRenderbuffers = count_GenRenderbuffers;
  a->gl.DeleteRenderbuffers = count_DeleteRenderbuffers;
  a->gl.CreateProgram = count_CreateProgram;
  a->gl.DeleteProgram = count_DeleteProgram;
  a->gl.CreateShader = count_CreateShader;
  a->gl.DeleteShader = count_DeleteShader;
}

/* ---- the demo: the script's session and its renderer ---- */

#define DEMO_SECONDS         196.0
#define SONG_RATE            44100
#define AUDIO_CHUNK_FRAMES   1024
#define CLOCK_EXTRAPOLATE_MS 100

/* ---- the sound ---- */

static void SDLCALL audio_callback(void *user, SDL_AudioStream *stream, int additional, int total)
{
  app *a = (app *)user;
  float buf[AUDIO_CHUNK_FRAMES * 2];
  (void)total;
  while (additional >= (int)(2 * sizeof(float))) {
    int frames = additional / (int)(2 * sizeof(float));
    if (frames > AUDIO_CHUNK_FRAMES) frames = AUDIO_CHUNK_FRAMES;
    paudio_render(&a->audio, buf, (uint32_t)frames);
    if (!SDL_PutAudioStreamData(stream, buf, frames * (int)(2 * sizeof(float)))) return;
    additional -= frames * (int)(2 * sizeof(float));
  }
}

/* the soundtrack, started at the song's beginning; without an audio device the picture runs on the wall clock and says so */
static void audio_start(app *a)
{
  paudio_init(&a->audio, SONG_RATE);
  const v2_result res = paudio_play(&a->audio, a->assets.data[CT_ASSET_SONG], a->assets.size[CT_ASSET_SONG]);
  if (res != V2_OK) {
    fprintf(stderr, "candytron: the soundtrack: %s; playing without sound\n", v2_result_string(res));
    return;
  }
  SDL_AudioSpec spec;
  SDL_zero(spec);
  spec.format = SDL_AUDIO_F32;
  spec.channels = 2;
  spec.freq = SONG_RATE;
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO) || !(a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, a))) {
    fprintf(stderr, "candytron: no audio device (%s); playing without sound\n", SDL_GetError());
    a->stream = NULL;
    return;
  }
  SDL_AudioSpec device;
  int device_frames = 0;
  if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(a->stream), &device, &device_frames) && device.freq > 0 && device_frames > 0)
    a->device_latency_frames = (uint32_t)((double)device_frames * SONG_RATE / device.freq);
  pclock_init(&a->clock, SONG_RATE, CLOCK_EXTRAPOLATE_MS);
  const uint64_t now = SDL_GetTicksNS();
  pclock_reset(&a->clock, 0, now);
  pclock_set_running(&a->clock, 1, now);
  a->demo_start_ns = now;
  SDL_ResumeAudioStreamDevice(a->stream);
}

static void audio_stop(app *a)
{
  if (a->stream) SDL_DestroyAudioStream(a->stream); /* ends the callback before the synth goes */
  a->stream = NULL;
  paudio_destroy(&a->audio);
}

/* ---- what it costs ---- */

static size_t heap_in_use(void)
{
#if defined(__EMSCRIPTEN__)
  return (size_t)mallinfo().uordblks;
#elif defined(__GLIBC__)
  const struct mallinfo2 m = mallinfo2();
  return m.uordblks + m.hblkhd;
#else
  return 0;
#endif
}

/* the resident set (Linux) or the size of the WebAssembly memory */
static size_t resident_bytes(void)
{
#if defined(__EMSCRIPTEN__)
  return emscripten_get_heap_size();
#elif defined(__linux__)
  long pages = 0, resident = 0;
  FILE *f = fopen("/proc/self/statm", "r");
  if (f) {
    if (fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
    fclose(f);
  }
  return (size_t)resident * (size_t)sysconf(_SC_PAGESIZE);
#else
  return 0;
#endif
}

static void note_heap(app *a)
{
  const size_t h = heap_in_use();
  if (h > a->heap_peak) a->heap_peak = h;
}

static void report_cost(app *a)
{
  note_heap(a);
  const double mb = 1.0 / (1024.0 * 1024.0);
  if (a->perf_n && a->perf_last_ns > a->perf_first_ns)
    printf("perf: %.1f pictures a second over %.1f s\n", (double)(a->perf_n - 1) * 1e9 / (double)(a->perf_last_ns - a->perf_first_ns),
           (double)(a->perf_last_ns - a->perf_first_ns) * 1e-9);
  if (a->perf_n)
    printf("perf: %llu pictures, %.2f ms each (script, scene, draw calls), worst %.2f ms, presenting %.2f ms, %llu over 20 ms\n", (unsigned long long)a->perf_n,
           a->perf_pic_sum / (double)a->perf_n, a->perf_pic_worst, a->perf_present_sum / (double)a->perf_n, (unsigned long long)a->perf_slow);
  printf("memory: heap in use %.1f MB (peak %.1f), %s %.1f MB\n", (double)heap_in_use() * mb, (double)a->heap_peak * mb,
#if defined(__EMSCRIPTEN__)
         "wasm memory",
#else
         "resident",
#endif
         (double)resident_bytes() * mb);
}

/* the song position the picture is made for: what the device has played (frames put - queued - the device's buffer), smoothed by the clock */
static double song_seconds(app *a)
{
  if (!a->stream) return a->paused ? a->paused_song : (double)(SDL_GetTicksNS() - a->demo_start_ns) * 1e-9;
  SDL_LockAudioStream(a->stream);
  const int64_t put = (int64_t)(a->audio.base + a->audio.frames);
  const int64_t queued = SDL_GetAudioStreamQueued(a->stream) / (int)(2 * sizeof(float));
  SDL_UnlockAudioStream(a->stream);
  int64_t played = put - queued - (int64_t)a->device_latency_frames;
  if (played < 0) played = 0;
  const uint64_t sampled = SDL_GetTicksNS();
  pclock_report(&a->clock, played, sampled);
  return (double)pclock_now(&a->clock, sampled) / SONG_RATE;
}

/* the pause and the seek: the sound is restarted at the time (the synth begins there), what was queued is dropped, the clock begins there */
static void transport_seek(app *a, double seconds)
{
  seconds = seconds < 0 ? 0 : seconds > 195 ? 195 : seconds;
  const uint64_t now = SDL_GetTicksNS();
  if (a->stream) {
    SDL_LockAudioStream(a->stream);
    paudio_seek(&a->audio, (uint32_t)(seconds * 1000.0));
    SDL_ClearAudioStream(a->stream);
    SDL_UnlockAudioStream(a->stream);
    pclock_reset(&a->clock, (int64_t)(seconds * SONG_RATE), now);
  }
  a->demo_start_ns = now - (uint64_t)(seconds * 1e9);
  a->paused_song = seconds;
  a->last_song = seconds;
}

static void transport_pause(app *a, int on)
{
  if (a->paused == on) return;
  const uint64_t now = SDL_GetTicksNS();
  if (a->stream) {
    if (on)
      SDL_PauseAudioStreamDevice(a->stream);
    else
      SDL_ResumeAudioStreamDevice(a->stream);
    pclock_set_running(&a->clock, !on, now);
  } else if (on)
    a->paused_song = song_seconds(a);
  else
    a->demo_start_ns = now - (uint64_t)(a->paused_song * 1e9);
  a->paused = on;
}

static int texture_of(void *user, int32_t handle, int32_t *w, int32_t *h, const uint8_t **bgra)
{
  const gt3_gen *g = &((app *)user)->session.gen;
  if (handle < 0 || handle >= g->n_texture || !g->texture[handle].bgra) return 0;
  *w = g->texture[handle].width;
  *h = g->texture[handle].height;
  *bgra = g->texture[handle].bgra;
  return 1;
}

/* the script's generation, timeline init and pre-roll (the original's sAPPCODE_INIT), then the renderer's objects */
static int start_demo(app *a)
{
  char error[512];
  if (!gt3_program_load(&a->program, a->assets.data[CT_ASSET_SCRIPT], a->assets.size[CT_ASSET_SCRIPT], error, sizeof error)) return fail("the script", error);
  a->vm = (gt3_vm *)calloc(1, sizeof *a->vm);
  if (!a->vm) return fail("out of memory", NULL);
  gt3_vm_init(a->vm, &a->program);
  gt3_register_natives(a->vm);
  a->session.text = gt3_text_baked;
  a->session.babe = a->assets.data[CT_ASSET_MESH];
  a->session.babe_size = a->assets.size[CT_ASSET_MESH];
  a->session.collect = 1;
  a->demo = 1;
  const int rounding = gt3_round_down();
  const uint64_t t_generate = SDL_GetTicksNS();
  const int ok = gt3_session_init(&a->session, a->vm);
  gt3_round_restore(rounding);
  const uint64_t t_generated = SDL_GetTicksNS();
  if (!ok) return fail("the script stopped during its generation", a->vm->error_message);
  gt3_render *r = a->session.gen.render;
  if (!r) return fail("out of memory", NULL);
  if (!ct_gles_init(&a->gles, &a->gl, a->es, r, texture_of, a)) return fail("the renderer", a->gles.error);
  printf("startup: %.2f s to generate (script, textures, meshes, pre-roll), %.0f ms to set up the renderer\n", (double)(t_generated - t_generate) * 1e-9,
         (double)(SDL_GetTicksNS() - t_generated) * 1e-6);
  a->gles_ok = 1;
  r->on_draw = ct_gles_on_draw;
  r->on_viewport = ct_gles_on_viewport;
  r->user = &a->gles;
  a->demo_start_ns = SDL_GetTicksNS();
  if (!a->opt.capture_dir && a->opt.soak == 0) {
    audio_start(a);
    if (a->opt.start > 0) transport_seek(a, a->opt.start);
  }
  return 0;
}

static void stop_demo(app *a)
{
  audio_stop(a);
  if (a->gles_ok) ct_gles_exit(&a->gles);
  if (a->demo) {
    gt3_session_exit(&a->session);
    gt3_vm_exit(a->vm);
    free(a->vm);
    gt3_program_free(&a->program);
  }
}

/* one picture at song time `seconds`, in the original's rounding mode; the driver is never called in it (the renderer's hooks switch) */
static int make_picture(app *a, double seconds)
{
  const int rounding = gt3_round_down();
  const int ok = gt3_session_frame(&a->session, seconds);
  gt3_round_restore(rounding);
  return ok;
}

/* glGetError after a frame (--gl-check): every error in the queue is counted and the first few are said. */
static void poll_gl_errors(app *a)
{
  uint32_t codes[GL_ERROR_QUEUE];
  int n = 0;
  while (n < GL_ERROR_QUEUE) {
    const uint32_t code = a->gl.GetError();
    if (!code) break;
    codes[n++] = code;
  }
  const uint64_t before = a->glcheck.errors;
  pglcheck_polled(&a->glcheck, codes, n);
  for (int i = 0; i < n && before + (uint64_t)i < 5; i++)
    fprintf(stderr, "candytron: GL error %s (0x%x) after frame %llu\n", pglcheck_error_name(codes[i]), codes[i], (unsigned long long)a->frames);
}

static int draw_frame(app *a)
{
  int w, h;
  SDL_GetWindowSizeInPixels(a->window, &w, &h);
  const double song = song_seconds(a);
  a->last_song = song;
  if (song >= DEMO_SECONDS) return 0;
  const uint64_t t0 = SDL_GetTicksNS();
  if (!make_picture(a, song)) {
    fail("the script stopped", a->vm->error_message);
    a->rc = 1;
    return 0;
  }
  const uint64_t t1 = SDL_GetTicksNS();
  a->gl.BindFramebuffer(CT_GL_FRAMEBUFFER, 0);
  a->gl.Viewport(0, 0, w, h);
  a->gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  a->gl.Clear(CT_GL_COLOR_BUFFER_BIT);
  const pview v = pview_fit(w, h, a->opt.aspect);
  ct_gles_present(&a->gles, v.x, v.y, v.width, v.height);
  if (a->opt.gl_check) poll_gl_errors(a);
  SDL_GL_SwapWindow(a->window);
  const double picture = (double)(t1 - t0) * 1e-6;
  if (!a->perf_n) a->perf_first_ns = t0;
  a->perf_last_ns = SDL_GetTicksNS();
  a->perf_n++;
  a->perf_pic_sum += picture;
  a->perf_present_sum += (double)(SDL_GetTicksNS() - t1) * 1e-6;
  if (picture > a->perf_pic_worst) a->perf_pic_worst = picture;
  if (picture > 20.0) a->perf_slow++;
  if ((a->perf_n & 63) == 0) note_heap(a);
  a->frames++;
  return 1;
}

/* ---- the live loop ---- */

static void toggle_fullscreen(app *a)
{
  const int on = !(SDL_GetWindowFlags(a->window) & SDL_WINDOW_FULLSCREEN);
  if (!SDL_SetWindowFullscreen(a->window, on))
    fprintf(stderr, "candytron: fullscreen is not available here: %s\n", SDL_GetError());
  else
    SDL_SyncWindow(a->window);
}

/* One iteration of the loop; returns 0 when the player is to stop (a->rc has the status). A native process calls it in a loop, the browser from
   requestAnimationFrame. */
static int live_frame(app *a)
{
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return 0;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
      if (e.key.key == SDLK_ESCAPE) return 0;
      if (e.key.key == SDLK_F) toggle_fullscreen(a);
      if (e.key.key == SDLK_SPACE) transport_pause(a, !a->paused);
      if (e.key.key == SDLK_LEFT) transport_seek(a, a->last_song - 10);
      if (e.key.key == SDLK_RIGHT) transport_seek(a, a->last_song + 10);
      if (e.key.key == SDLK_HOME) transport_seek(a, 0);
    }
  }
  if (a->opt.exit_after >= 0 && (double)(SDL_GetTicksNS() - a->start_ns) * 1e-9 >= a->opt.exit_after) return 0;
  return draw_frame(a);
}

/* --gl-check: what the player asked of the driver must balance and the frames must have raised no GL error. Returns 1 when it did not. */
static int report_gl_check(const app *a)
{
  if (!a->opt.gl_check) return 0;
  char objects[256];
  pglcheck_format(&a->glcheck, objects, sizeof objects);
  printf("gl-check: %llu GL errors in %llu polls (%s)\n", (unsigned long long)a->glcheck.errors, (unsigned long long)a->glcheck.polls, objects);
  return a->glcheck.errors || a->glcheck.over_deleted || pglcheck_total(&a->glcheck) != 0;
}

/* the end of a live run: the sound and the demo's objects go first, so that --gl-check can say what is left; returns 1 if it found something */
static int end_live(app *a)
{
  if (a->stream)
    printf("audio: %llu frames rendered, the last picture at song time %.2f s\n", (unsigned long long)(a->audio.base + a->audio.frames), a->last_song);
  report_cost(a);
  stop_demo(a);
  a->demo = a->gles_ok = 0;
  return report_gl_check(a);
}

#if defined(__EMSCRIPTEN__)
/* tells the page that the player is done (the page shows what to do next) */
static void web_ended(int rc)
{
  EM_ASM(
    {
      if (Module.onEnded) Module.onEnded($0);
    },
    rc);
}

static void web_frame(void *arg)
{
  app *a = (app *)arg;
  if (live_frame(a)) return;
  emscripten_cancel_main_loop();
  if (end_live(a)) a->rc = 1;
  web_ended(a->rc);
}
#endif

static int run_live(app *a)
{
  a->start_ns = SDL_GetTicksNS();
#if defined(__EMSCRIPTEN__)
  emscripten_set_main_loop_arg(web_frame, a, 0, 1); /* does not return */
  return 0;
#else
  while (live_frame(a)) {}
  return a->rc;
#endif
}

/* ---- --capture: the pictures at given song times, as files and hashes ---- */

static int write_ppm(const char *path, const uint8_t *rgba, int w, int h)
{
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (size_t i = 0; i < (size_t)w * (size_t)h; i++) fwrite(rgba + i * 4, 1, 3, f);
  return fclose(f) == 0;
}

static int run_capture(app *a)
{
  double defaults[40];
  const double *times = a->opt.times;
  int n = a->opt.n_times, rc = 0;
  if (!n) {
    for (int k = 0; k < 40; k++) defaults[k] = 5.0 * k;
    times = defaults;
    n = 40;
  }
  const gt3_render *r = a->session.gen.render;
  const int w = r->screen_w, h = r->screen_h;
  uint8_t *rgba = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
  if (!rgba) return fail("out of memory", NULL);
  SDL_CreateDirectory(a->opt.capture_dir);
  for (int k = 0; k < n && !rc; k++) {
    if (!make_picture(a, times[k])) {
      rc = fail("the script stopped", a->vm->error_message);
      break;
    }
    ct_gles_read_screen(&a->gles, rgba);
    ct_sha256 sh;
    uint8_t digest[32];
    char hex[65], path[1024];
    ct_sha256_init(&sh);
    ct_sha256_update(&sh, rgba, (size_t)w * (size_t)h * 4u);
    ct_sha256_final(&sh, digest);
    ct_sha256_hex(digest, hex);
    snprintf(path, sizeof path, "%s/frame_%03d.ppm", a->opt.capture_dir, k + 1);
    if (!write_ppm(path, rgba, w, h)) rc = fail("cannot write", path);
    printf("capture: %d t=%g %dx%d sha256=%s\n", k + 1, times[k], w, h, hex);
    a->frames++;
    if (a->opt.gl_check) poll_gl_errors(a);
  }
  free(rgba);
  note_heap(a);
  report_cost(a);
  stop_demo(a);
  a->demo = a->gles_ok = 0;
  if (report_gl_check(a)) rc = 1;
  return rc;
}

/* ---- --soak: the demo for a long time, without waiting for the clock ---- */

/* --soak-rate pictures (30) a second of song time, the time wrapping at the song's end; a line a minute of song time with the heap and the live objects. The first loop of
   the song is the warm-up (buffers grow to their size); from then on nothing may grow: the heap at the end of each loop (the same picture every time) is
   compared with the one at the end of the first. */
static int run_soak(app *a)
{
  const double per_second = a->opt.soak_rate;
  const uint64_t loop_pictures = (uint64_t)(196.0 * per_second), every = (uint64_t)(60.0 * per_second);
  const gt3_render *r = a->session.gen.render;
  uint8_t *rgba = (uint8_t *)malloc((size_t)r->screen_w * (size_t)r->screen_h * 4u);
  if (!rgba) return fail("out of memory", NULL);
  const uint64_t total = (uint64_t)(a->opt.soak * per_second);
  size_t base_heap = 0, last_heap = 0;
  uint32_t base_objects = 0, last_objects = 0;
  int rc = 0, have_base = 0;
  uint64_t loops = 0;
  const uint64_t t_start = SDL_GetTicksNS();
  for (uint64_t i = 0; i < total && !rc; i++) {
    const double t = (double)(i % loop_pictures) / per_second;
    const uint64_t t0 = SDL_GetTicksNS();
    if (!make_picture(a, t)) {
      rc = fail("the script stopped", a->vm->error_message);
      break;
    }
    const double picture = (double)(SDL_GetTicksNS() - t0) * 1e-6;
    a->perf_pic_sum += picture;
    if (picture > a->perf_pic_worst) a->perf_pic_worst = picture;
    if (picture > 20.0) a->perf_slow++;
    if (i % 64 == 0) ct_gles_read_screen(&a->gles, rgba); /* makes the driver finish what it was given */
    if (a->opt.gl_check && i % 16 == 0) poll_gl_errors(a);
    a->perf_n++;
    if ((i + 1) % every == 0 || i + 1 == total) {
      const size_t heap = heap_in_use();
      note_heap(a);
      printf("soak: song time %.0f s, %llu pictures, heap %.2f MB, resident %.1f MB, %u objects live\n", (double)(i + 1) / per_second,
             (unsigned long long)(i + 1), (double)heap / (1024.0 * 1024.0), (double)resident_bytes() / (1024.0 * 1024.0), gt3_vm_live(a->vm));
    }
    if ((i + 1) % loop_pictures == 0) {
      last_heap = heap_in_use();
      last_objects = gt3_vm_live(a->vm);
      loops++;
      if (!have_base) {
        base_heap = last_heap;
        base_objects = last_objects;
        have_base = 1;
      }
    }
  }
  free(rgba);
  const double seconds = (double)(SDL_GetTicksNS() - t_start) * 1e-9;
  printf("soak: %.0f s of song in %.1f s (%.0f pictures a second)\n", a->opt.soak, seconds, (double)total / (seconds > 0 ? seconds : 1));
  if (loops >= 2) {
    const long grown = (long)last_heap - (long)base_heap;
    const int ok = grown <= 64 * 1024 && last_objects == base_objects;
    printf("soak: at the end of loop %llu the heap grew by %ld bytes since the end of the first and the live objects went from %u to %u: %s\n",
           (unsigned long long)loops, grown, base_objects, last_objects, ok ? "ok" : "FAILED");
    if (!ok) rc = 1;
  } else
    printf("soak: shorter than two loops of the song (392 s): nothing judged\n");
  report_cost(a);
  stop_demo(a);
  a->demo = a->gles_ok = 0;
  if (report_gl_check(a)) rc = 1;
  return rc;
}

/* ---- main ---- */

int main(int argc, char **argv)
{
  app *a = (app *)calloc(1, sizeof *a);
  if (!a) return fail("out of memory", NULL);
  int rc = 1;
  char error[256];
  if (!poptions_parse(argc, argv, &a->opt, error, sizeof error)) {
    fprintf(stderr,
            "candytron: %s\n"
            "usage: %s [--assets DIR] [--size WxH] [--aspect W:H] [--fullscreen] [--no-vsync] [--exit-after SECONDS] [--gl-check]\n"
            "          [--start SECONDS] [--capture DIR [--times T,T,...]] [--soak SECONDS [--soak-rate R]] [--selftest] [--verify-assets] [--version] "
            "[--licenses]\n",
            error, argv[0]);
    free(a);
    return 2;
  }
  if (a->opt.version) {
    printf("candytron %s (fr-030: Candytron)\n", CANDYTRON_VERSION);
    free(a);
    return 0;
  }
  if (a->opt.licences) { /* nothing to load: the credits and the notices of what the player is built from */
    const int ok = pcredits_print(stdout, player_licenses, player_n_licenses);
    free(a);
    return ok ? 0 : 1;
  }
  if (!a->opt.selftest && load_assets(a)) goto done;
  if (a->opt.verify_assets) {
    print_assets(a);
    rc = 0;
    goto done;
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    rc = fail("SDL", SDL_GetError());
    goto done;
  }
  if (create_window(a)) goto done;
  if (a->opt.gl_check) install_counters(a);
  if (a->opt.selftest) {
    rc = selftest_run(&a->gl, a->es) ? 1 : 0;
    if (report_gl_check(a)) rc = 1;
    goto done;
  }
  if (start_demo(a)) goto done;
  if (a->opt.capture_dir) {
    rc = run_capture(a);
  } else if (a->opt.soak > 0) {
    rc = run_soak(a);
  } else {
    rc = run_live(a);
    if (end_live(a)) rc = 1;
  }
#if defined(__EMSCRIPTEN__)
  if (a->opt.capture_dir || a->opt.soak > 0) web_ended(rc); /* the live run tells it from its loop */
#endif

done:
  stop_demo(a);
  if (a->context) SDL_GL_DestroyContext(a->context);
  if (a->window) SDL_DestroyWindow(a->window);
  SDL_Quit();
  ct_assets_free(&a->assets);
  free(a);
  return rc;
}
