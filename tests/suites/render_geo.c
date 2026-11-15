/* The geometry the script's scenes and effect chains make, and the draw calls with the Direct3D state of each, against the oracle.
 *
 *   suite_render_geo <assets dir> <oracle fixtures dir> <mesh fixtures dir> [collect]
 *
 * Runs default.pac as the player does (generation, timeline init, pre-roll, 40 frames at 0, 5 ... 195 s) with the renderer of render.h behind the natives, and compares
 *   - every geometry the device was given (geo.txt, 3367 of them: handle, static or dynamic, vertex format, vertex and index counts, the SHA-256 of the vertex bytes and of
 *     the index bytes, the frame): the batches of GenMesh::Prepare, the vertices of the passes, the sprites and thick lines, the quads of the effect chains, and the
 *     device's allocator that decides handles and re-uploads
 *   - the draw calls of every frame (frames.txt): their number, the SHA-256 of the DRAW lines and the SHA-256 of every line of the frame (viewports, camera, matrices,
 *     lights, material, render, texture-stage and sampler states, texture bindings, draws)
 * The hashes are compared when the machine runs in the original's rounding mode with the original's power function (the same condition as the trace suite).
 * With "collect" the session's object collector runs once per picture, as in the player.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/check.h"
#include "support/suite.h"

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
#define CT_SHA256_IMPLEMENTATION
#define CT_ASSETS_IMPLEMENTATION
#include <candytron/core/assets.h>

static char *read_all(const char *path, size_t *size)
{
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *text = (char *)malloc((size_t)n + 1);
  if (text && fread(text, 1, (size_t)n, f) == (size_t)n) {
    text[n] = 0;
    if (size) *size = (size_t)n;
  } else {
    free(text);
    text = NULL;
  }
  fclose(f);
  return text;
}

static size_t split(char *text, char ***lines)
{
  size_t n = 0, cap = 4096;
  char **l = (char **)malloc(cap * sizeof *l);
  for (char *p = text; *p;) {
    if (n == cap) l = (char **)realloc(l, (cap *= 2) * sizeof *l);
    l[n++] = p;
    char *e = strchr(p, '\n');
    if (!e) break;
    *e = 0;
    p = e + 1;
  }
  *lines = l;
  return n;
}

typedef struct geo_check {
  gt3_session *session;
  char **fix; /* the fixture's lines without the comments */
  size_t n_fix, n_seen, mismatches, extra;
  int hashes;
  char *draws; /* the DRAW lines of the frame, as the oracle prints them without the counter, joined by newlines */
  size_t n_draws, cap_draws;
  char *all;   /* every line of the frame: the device's states, matrices, lights and material, and the draws */
  size_t n_all, cap_all;
} geo_check;

static void hash16(const void *data, size_t size, char out[17])
{
  ct_sha256 s;
  uint8_t d[32];
  char hex[65];
  ct_sha256_init(&s);
  ct_sha256_update(&s, data, size);
  ct_sha256_final(&s, d);
  ct_sha256_hex(d, hex);
  memcpy(out, hex, 16);
  out[16] = 0;
}

/* the same line as geo.txt, from what the device holds after GeoEnd */
static void geo_line(char *line, size_t cap, geo_check *c, gt3_geodev *dev, int32_t handle)
{
  const gt3_geo_handle *h = &dev->handle[handle];
  char vh[17] = "", ih[17] = "-";
  if (c->hashes) {
    hash16(gt3_geo_vertices(dev, handle), (size_t)h->vcount * 32u, vh);
    if (h->ibuf) hash16(gt3_geo_indices(dev, handle), (size_t)h->icount * 2u, ih);
  } else {
    strcpy(vh, "-");
    if (!h->ibuf)
      strcpy(ih, "-");
    else
      strcpy(ih, "i");
  }
  snprintf(line, cap, "%d %s %c %d %d %s %s %d", (int)handle, (h->mode & GT3_GEO_STATIC) ? "static" : "dynamic", h->fvf == GT3_FVF_STANDARD ? 'N' : 'D',
           (int)h->vcount, (int)h->icount, vh, ih, c->session->frames);
}

/* without the hashes only the structure is compared: the fixture's hashes are replaced by the same placeholders */
static void reduce(char *line)
{
  char copy[256], *w[8];
  int n = 0;
  snprintf(copy, sizeof copy, "%s", line);
  for (char *p = copy; *p && n < 8;) {
    w[n++] = p;
    char *e = strchr(p, ' ');
    if (!e) break;
    *e = 0;
    p = e + 1;
  }
  if (n < 8) return;
  snprintf(line, 256, "%.12s %.12s %.2s %.12s %.12s - %.2s %.12s", w[0], w[1], w[2], w[3], w[4], strcmp(w[6], "-") ? "i" : "-", w[7]);
}

static void on_geo_end(void *user, gt3_geodev *dev, int32_t handle)
{
  geo_check *c = (geo_check *)user;
  char line[256];
  geo_line(line, sizeof line, c, dev, handle);
  if (c->n_seen >= c->n_fix) {
    c->extra++;
  } else {
    char fix[256];
    snprintf(fix, sizeof fix, "%s", c->fix[c->n_seen]);
    if (!c->hashes) reduce(fix);
    if (strcmp(line, fix)) {
      if (c->mismatches < 12) printf("geometry %lu:\n  port:   %s\n  oracle: %s\n", (unsigned long)c->n_seen, line, fix);
      c->mismatches++;
    }
  }
  c->n_seen++;
}

static void hash64(const void *data, size_t size, char out[65])
{
  ct_sha256 s;
  uint8_t d[32];
  ct_sha256_init(&s);
  ct_sha256_update(&s, data, size);
  ct_sha256_final(&s, d);
  ct_sha256_hex(d, out);
}

/* one draw call, as the oracle's GeoDraw prints it (without the running number): what the device is given, the vertices of the range and the indices */
static void append(char **buf, size_t *n, size_t *cap, const char *line, size_t len)
{
  if (*n + len + 1 > *cap) {
    *cap = (*n + len + 1) * 2;
    *buf = (char *)realloc(*buf, *cap);
  }
  memcpy(*buf + *n, line, len);
  *n += len;
}

static void on_geo_trace(void *user, const char *line)
{
  geo_check *c = (geo_check *)user;
  append(&c->all, &c->n_all, &c->cap_all, line, strlen(line));
  append(&c->all, &c->n_all, &c->cap_all, "\n", 1);
}

static void on_geo_draw(void *user, gt3_geodev *dev, int32_t handle)
{
  geo_check *c = (geo_check *)user;
  const gt3_geo_handle *h = &dev->handle[handle];
  char vh[65], ih[65] = "-", line[512];
  int32_t count = h->icount ? h->icount : h->vcount;
  int32_t ib = h->ibuf;
  const int quad = h->mode == GT3_GEO_QUAD;
  if (quad) {
    ib = 2;
    count = (count * 3) >> 1;
  }
  count /= 3;
  hash64(dev->buffer[h->vbuf].mem + h->vstart, (size_t)h->vcount * 32u, vh);
  if (quad) { /* the device's static quad indices: 0 1 2 0 2 3, then the same from 4, 8 ... */
    static const uint16_t pattern[6] = { 0, 1, 2, 0, 2, 3 };
    uint16_t *q = (uint16_t *)calloc((size_t)count * 3u + 1u, sizeof *q);
    for (int32_t k = 0; k < count * 3; k++) q[k] = (uint16_t)((k / 6) * 4 + pattern[k % 6]);
    hash64(q, (size_t)count * 6u, ih);
    free(q);
  } else if (ib)
    hash64(dev->buffer[ib].mem + h->istart, (size_t)count * 6u, ih);
  const int n = snprintf(line, sizeof line, "DRAW handle=%d fvf=%s mode=%d vertices=%d first=%d indexed=%d triangles=%d vsha=%s isha=%s rt=%d\n", (int)handle,
                         h->fvf == GT3_FVF_STANDARD ? "xyz+nrm+tex1" : "xyz+diffuse+tex2", (int)h->mode, (int)h->vcount, (int)(h->vstart >> 5), ib ? 1 : 0,
                         (int)count, vh, ih, (int)c->session->gen.render->rt_handle);
  append(&c->draws, &c->n_draws, &c->cap_draws, line, (size_t)n);
  append(&c->all, &c->n_all, &c->cap_all, line, (size_t)n);
}

int suite_render_geo(int argc, char **argv)
{
  if (argc < 4) {
    printf("usage: suite_render_geo <assets dir> <oracle fixtures dir> <mesh fixtures dir>\n");
    return 2;
  }
  char error[512], path[2048];
  ct_assets assets;
  CHECK(ct_assets_load(&assets, argv[1], error, sizeof error), "assets: %s", error);
  if (!assets.data[CT_ASSET_SCRIPT]) return 1;
  gt3_program prog;
  CHECK(gt3_program_load(&prog, assets.data[CT_ASSET_SCRIPT], assets.size[CT_ASSET_SCRIPT], error, sizeof error), "script: %s", error);

  snprintf(path, sizeof path, "%s/geo.txt", argv[2]);
  char *geo_text = read_all(path, NULL);
  /* "collect": the session's object collector runs once per picture, as in the player */
  int collect = 0;
  for (int i = 4; i < argc; i++) collect |= !strcmp(argv[i], "collect");
  const int n_frames = 40;
  const double frame_step = 5.0;
  snprintf(path, sizeof path, "%s/frames.txt", argv[2]);
  char *frames_text = read_all(path, NULL);
  snprintf(path, sizeof path, "%s/babe.mesh", argv[3]);
  size_t babe_size = 0;
  char *babe = read_all(path, &babe_size);
  CHECK(geo_text && frames_text && babe, "cannot read the fixtures");
  if (!geo_text || !frames_text || !babe) return 1;

  char **geo_lines, **frame_lines;
  const size_t n_geo_lines = split(geo_text, &geo_lines), n_frame_lines = split(frames_text, &frame_lines);
  size_t first = 0;
  while (first < n_geo_lines && geo_lines[first][0] == '#') first++;

  gt3_vm *vm = (gt3_vm *)calloc(1, sizeof *vm);
  gt3_vm_init(vm, &prog);
  gt3_register_natives(vm);
  const int rounding = getenv("CANDYTRON_TEST_NEAREST") ? -1 : gt3_round_down();
#ifdef GT3_FPOW_X87
  const int exact = rounding >= 0;
#else
  const int exact = 0;
#endif
  gt3_session s;
  memset(&s, 0, sizeof s);
  geo_check chk;
  memset(&chk, 0, sizeof chk);
  chk.session = &s;
  chk.fix = geo_lines + first;
  chk.n_fix = n_geo_lines - first;
  chk.hashes = exact;
  s.babe = (const uint8_t *)babe;
  s.babe_size = babe_size;
  s.collect = collect;
  s.on_geo_end = on_geo_end;
  s.on_geo_draw = on_geo_draw;
  s.on_geo_trace = on_geo_trace;
  s.geo_user = &chk;

  const int ok_init = gt3_session_init(&s, vm);
  CHECK(ok_init, "the generation, the timeline init and the pre-roll ran without an error (pc %d, %s)", (int)vm->pc,
        vm->error_message ? vm->error_message : "");
  const size_t geo_after_init = chk.n_seen;
  uint32_t draws[40] = { 0 }, live[40] = { 0 };
  char draw_hash[40][65], all_hash[40][65];
  memset(draw_hash, 0, sizeof draw_hash);
  memset(all_hash, 0, sizeof all_hash);
  int frames_ok = 1;
  gt3_render *r = s.gen.render;
  CHECK(r != NULL, "the session has a renderer");
  for (int k = 0; k < n_frames && ok_init && r; k++) {
    const uint32_t before = r->dev.n_draw;
    chk.n_draws = 0;
    chk.n_all = 0;
    live[k] = gt3_vm_live(vm);
    frames_ok &= gt3_session_frame(&s, frame_step * k);
    draws[k] = r->dev.n_draw - before;
    if (chk.n_draws) chk.n_draws--; /* the oracle joins the lines: no newline after the last */
    hash64(chk.draws ? chk.draws : "", chk.n_draws, draw_hash[k]);
    if (chk.n_all) chk.n_all--;
    hash64(chk.all ? chk.all : "", chk.n_all, all_hash[k]);
  }
  CHECK(frames_ok, "%d frames ran without an error", n_frames);
  uint32_t total_draws = 0;
  for (int k = 0; k < n_frames; k++) total_draws += draws[k];
  gt3_round_restore(rounding);

  CHECK(chk.mismatches == 0, "%lu of %lu geometries differ", (unsigned long)chk.mismatches, (unsigned long)chk.n_seen);
  CHECK(chk.n_seen == chk.n_fix, "%lu geometries, the oracle has %lu", (unsigned long)chk.n_seen, (unsigned long)chk.n_fix);

  /* the draw calls of every frame */
  size_t bad_frames = 0, bad_hash = 0, bad_all = 0, nframe = 0;
  uint32_t fixture_draws = 0;
  for (size_t i = 0; i < n_frame_lines && nframe < (size_t)n_frames; i++) {
    if (strncmp(frame_lines[i], "FRAME ", 6)) continue;
    int n = 0;
    unsigned want = 0;
    const char *bar = strchr(frame_lines[i], '|');
    if (!bar || sscanf(frame_lines[i], "FRAME %d", &n) != 1 || sscanf(bar + 1, "%u", &want) != 1) continue;
    fixture_draws += want;
    {
      char want_hash[65] = "";
      const char *h1 = strchr(bar + 1, '|');
      if (h1 && sscanf(h1 + 1, "%64s", want_hash) == 1 && exact && nframe < (size_t)n_frames && strcmp(want_hash, draw_hash[nframe])) {
        if (bad_hash < 8) printf("frame %d: the DRAW lines hash to %.16s, the oracle's to %.16s\n", n, draw_hash[nframe], want_hash);
        bad_hash++;
      }
      char want_all[65] = "";
      const char *h2 = h1 ? strchr(h1 + 1, '|') : NULL;
      if (h2 && sscanf(h2 + 1, "%64s", want_all) == 1 && exact && nframe < (size_t)n_frames && strcmp(want_all, all_hash[nframe])) {
        if (bad_all < 8) printf("frame %d: all the lines hash to %.16s, the oracle's to %.16s\n", n, all_hash[nframe], want_all);
        bad_all++;
      }
    }
    if (draws[nframe] != want) {
      if (bad_frames < 8) printf("frame %d: %u draw calls, the oracle has %u\n", n, draws[nframe], want);
      bad_frames++;
    }
    nframe++;
  }
  CHECK(nframe == (size_t)n_frames, "%lu frames in the fixture", (unsigned long)nframe);
  CHECK(bad_frames == 0, "%lu of %d frames have another number of draw calls", (unsigned long)bad_frames, n_frames);
  CHECK(bad_hash == 0, "%lu of %d frames have other DRAW lines (handles, offsets, vertices, indices, render targets)", (unsigned long)bad_hash, n_frames);
  if (collect) {
    /* the pictures make no objects (the timeline's chains are made once and the script resets the members it animates): the count is the same before every picture, with
       the collector running after each of them and after each pre-roll step */
    uint32_t most = 0;
    for (int k = 0; k < n_frames; k++) most = live[k] > most ? live[k] : most;
    CHECK(most == live[0], "%u objects live before the last picture, %u before the first", most, live[0]);
    printf("render_geo: collecting: %u objects live in every picture, %u freed\n", live[0], vm->n_collected);
  }
  CHECK(bad_all == 0, "%lu of %d frames have other state lines (states, matrices, lights, material, textures)", (unsigned long)bad_all, n_frames);
  printf("render_geo: %lu geometries (%lu in the generation and pre-roll), %u draw calls in the %d frames (oracle %u), %lu viewports, hashes %s\n",
         (unsigned long)chk.n_seen, (unsigned long)geo_after_init, total_draws, n_frames, fixture_draws, (unsigned long)(r ? r->n_viewports : 0),
         exact ? "compared" : "skipped");

  gt3_session_exit(&s);
  gt3_vm_exit(vm);
  free(vm);
  gt3_program_free(&prog);
  ct_assets_free(&assets);
  free(geo_lines);
  free(frame_lines);
  free(geo_text);
  free(frames_text);
  free(babe);
  free(chk.draws);
  free(chk.all);
  printf("render_geo: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
