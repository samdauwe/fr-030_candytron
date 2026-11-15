/* The script machine against the oracle.
 *
 *   suite_csl_trace <assets dir> <oracle fixtures dir> <mesh fixtures dir> [old]
 *
 * Runs default.pac through what the intro player does (generation, timeline init, 104 pre-roll beats, then the frames at 0, 5, ... 195 s) with the
 * natives of csl_natives.h, and compares the trace of the native calls, line by line, with tests/data/candytron/oracle/calls.txt, which the
 * original GenThree code wrote. A line carries the sequence number, the phase, the native's id and name, its object arguments as serial numbers
 * in the order they first appear, its integer arguments, and its result. Object results are compared by their class, and, in the original's
 * rounding mode, by the SHA-256 of the pixels of every bitmap and of the vertices of every mesh. With "old" the mesh of the character is
 * the executable's (assets/babe.mesh.old) and the trace is calls_exe_mesh.txt; without it the mesh is tests/data/candytron/mesh/babe.mesh.
 *
 * The comparison is exact when the machine runs in the original's rounding mode (round downward: gt3_round_down) and the host's libm gives the
 * same pow(); where the host has no control of the rounding mode, the floats the script derives from splines differ in the last bits, and the
 * suite then allows that, and says how many arguments it allowed.
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
    *size = (size_t)n;
  } else {
    free(text);
    text = NULL;
  }
  fclose(f);
  return text;
}

/* splits a text into lines in place; returns the number of lines */
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

/* the oracle's line reduced to what the port reproduces: a null result loses its trailing blank, and an object result loses what follows its class (the hashes of
   bitmaps are kept when `keep_bitmaps` is set: the host's rounding mode is then the original's, and the pixels are the original's; the same holds for the hashes of meshes with `keep_meshes`) */
static void drop_token(char *line, const char *name)
{
  char key[16];
  snprintf(key, sizeof key, " %s=", name);
  char *at = strstr(line, key);
  if (!at) return;
  char *end = strchr(at + 1, ' ');
  if (end)
    memmove(at, end, strlen(end) + 1);
  else
    *at = 0;
}

static void reduce(char *line, int keep_bitmaps, int keep_pixels)
{
  char *arrow = strstr(line, " -> o");
  if (arrow) {
    char *p = arrow + 5;
    while (*p >= '0' && *p <= '9') p++;
    if (*p == ' ') {
      char *kind = p + 1;
      char *end = kind;
      while (*end && *end != ' ') end++;
      if (end - kind == 4 && !strncmp(kind, "mesh", 4)) {
        if (!keep_pixels) {
          drop_token(line, "sha256");
          drop_token(line, "xyz");
          drop_token(line, "w");
        }
      } else if (!(keep_bitmaps && end - kind == 6 && !strncmp(kind, "bitmap", 6)))
        *end = 0;
    }
  }
  size_t n = strlen(line);
  while (n && line[n - 1] == ' ') line[--n] = 0;
}

/* what the trace prints after `bitmap`: the oracle's `256x256 texture=10 sha256=<the 64 bit pixels>` */
static void trace_bitmap(const gt3_vm *vm, const gt3_object *o, FILE *out)
{
  (void)vm;
  const gt3_bitmap *bm = (const gt3_bitmap *)o->data;
  if (!bm) return;
  uint8_t digest[32];
  char hex[65];
  ct_sha256 h;
  ct_sha256_init(&h);
  ct_sha256_update(&h, bm->pixel, (size_t)bm->width * (size_t)bm->height * 8u);
  ct_sha256_final(&h, digest);
  ct_sha256_hex(digest, hex);
  fprintf(out, " %dx%d texture=%d sha256=%s", (int)bm->width, (int)bm->height, (int)bm->texture, hex);
}

/* the oracle's hash_mesh (oracle_main.cpp): the topology records without their scratch fields, the vertex buffer as xyz and as w, and both in one digest */
static void trace_mesh(const gt3_vm *vm, const gt3_object *o, FILE *out)
{
  const gt3_mesh *m = (const gt3_mesh *)o->data;
  if (!m) return;
  /* debugging aid: CANDYTRON_DUMP_SEQ=<call> CANDYTRON_DUMP_FILE=<path> writes the vertex buffer in the oracle's dump format */
  if (getenv("CANDYTRON_DUMP_SEQ") && getenv("CANDYTRON_DUMP_FILE") && atoi(getenv("CANDYTRON_DUMP_SEQ")) == vm->trace_seq - 1) {
    FILE *f = fopen(getenv("CANDYTRON_DUMP_FILE"), "w");
    for (int32_t i = 0; f && i < m->vert_count * m->vert_size; i++) {
      uint32_t b[4];
      memcpy(b, &m->vbuf[i], 16);
      fprintf(f, "%d %d %08x %08x %08x %08x\n", (int)(i / m->vert_size), (int)(i % m->vert_size), (unsigned)b[0], (unsigned)b[1], (unsigned)b[2],
              (unsigned)b[3]);
    }
    if (f) fclose(f);
  }
  uint8_t d[32];
  char topo[65], xyz[65], w[65], all[65];
  ct_sha256 s;
  ct_sha256_init(&s);
  const int32_t counts[8] = { m->n_vert, m->n_edge, m->n_face, m->n_mtrl, m->vert_count, m->vert_size, m->vert_mask, m->key_count };
  ct_sha256_update(&s, counts, sizeof counts);
  for (int32_t i = 0; i < m->n_vert; i++) {
    const gt3_mvert *v = &m->vert[i];
    const int32_t f[3] = { v->next, v->first, (int32_t)(((uint32_t)v->mask << 16) | ((uint32_t)v->id << 8) | v->used) };
    ct_sha256_update(&s, f, sizeof f);
    ct_sha256_update(&s, v->weight, 4);
    ct_sha256_update(&s, v->matrix, 4);
  }
  for (int32_t i = 0; i < m->n_edge; i++) {
    const gt3_medge *e = &m->edge[i];
    const int32_t f[10] = { e->next[0], e->next[1], e->prev[0], e->prev[1], e->face[0],
                            e->face[1], e->vert[0], e->vert[1], e->crease,  (int32_t)(((uint32_t)e->mask << 16) | ((uint32_t)e->id << 8) | e->used) };
    ct_sha256_update(&s, f, sizeof f);
  }
  for (int32_t i = 0; i < m->n_face; i++) {
    const gt3_mface *f = &m->face[i];
    const int32_t g[3] = { f->material, f->edge, (int32_t)(((uint32_t)f->mask << 16) | ((uint32_t)f->id << 8) | f->used) };
    ct_sha256_update(&s, g, sizeof g);
  }
  ct_sha256_final(&s, d);
  ct_sha256_hex(d, topo);
  ct_sha256 a, b;
  ct_sha256_init(&a);
  ct_sha256_init(&b);
  for (int32_t i = 0; m->vbuf && i < m->vert_count * m->vert_size; i++) {
    ct_sha256_update(&a, &m->vbuf[i], 12);
    ct_sha256_update(&b, &m->vbuf[i].w, 4);
  }
  ct_sha256_final(&a, d);
  ct_sha256_hex(d, xyz);
  ct_sha256_final(&b, d);
  ct_sha256_hex(d, w);
  ct_sha256 t;
  ct_sha256_init(&t);
  ct_sha256_update(&t, topo, 64);
  ct_sha256_update(&t, xyz, 64);
  ct_sha256_final(&t, d);
  ct_sha256_hex(d, all);
  fprintf(out, " verts=%d edges=%d faces=%d mtrls=%d vertbuf=%d*%d prepared=%d sha256=%s topo=%.16s xyz=%.16s w=%.16s", (int)m->n_vert, (int)m->n_edge,
          (int)m->n_face, (int)m->n_mtrl, (int)m->vert_count, (int)m->vert_size, (int)m->prepared, all, topo, xyz, w);
}

/* the arguments of a call line (after the name): split into words */
static int words(char *line, char **w, int max)
{
  int n = 0;
  for (char *p = line; *p && n < max;) {
    w[n++] = p;
    char *e = p;
    if (*p == '"') { /* a string: up to the closing quote */
      e = strchr(p + 1, '"');
      e = e ? e + 1 : p + strlen(p);
    } else
      while (*e && *e != ' ') e++;
    if (!*e) break;
    *e = 0;
    p = e + 1;
  }
  return n;
}

/* the line of the port against the line of the oracle; `tolerated` counts the integer arguments that differ by no more than `slack` */
static int same_line(const char *ours, const char *theirs, int slack, int *tolerated)
{
  if (!strcmp(ours, theirs)) return 1;
  if (!slack || ours[0] != 'C' || theirs[0] != 'C') return 0;
  char a[1024], b[1024], *wa[64], *wb[64];
  snprintf(a, sizeof a, "%s", ours);
  snprintf(b, sizeof b, "%s", theirs);
  const int na = words(a, wa, 64), nb = words(b, wb, 64);
  if (na != nb) return 0;
  int diffs = 0;
  for (int i = 0; i < na; i++) {
    if (!strcmp(wa[i], wb[i])) continue;
    /* an integer argument (8 hex digits) off by a few in the last place of a 16.16 value or of a float's bits */
    if (strlen(wa[i]) != 8 || strlen(wb[i]) != 8) return 0;
    const long x = (long)strtoul(wa[i], NULL, 16), y = (long)strtoul(wb[i], NULL, 16);
    long d = x - y;
    if (d < 0) d = -d;
    if (d > slack) return 0;
    diffs++;
  }
  *tolerated += diffs;
  return 1;
}

/* a line of the state dump: the first four words (S, tag, serial, class) must be equal; the members may differ by `slack` units in the last place of their bits;
   `worst` is the largest difference met */
static int same_state(const char *ours, const char *theirs, long slack, long *worst)
{
  if (!strcmp(ours, theirs)) return 1;
  char a[1024], b[1024], *wa[64], *wb[64];
  snprintf(a, sizeof a, "%s", ours);
  snprintf(b, sizeof b, "%s", theirs);
  const int na = words(a, wa, 64), nb = words(b, wb, 64);
  if (na != nb) return 0;
  for (int i = 0; i < na; i++) {
    if (!strcmp(wa[i], wb[i])) continue;
    if (i < 4 || strlen(wa[i]) != 8 || strlen(wb[i]) != 8) return 0;
    const uint32_t ua = (uint32_t)strtoul(wa[i], NULL, 16), ub = (uint32_t)strtoul(wb[i], NULL, 16);
    long d = (long)ua - (long)ub;
    if (d < 0) d = -d;
    if (d > *worst) *worst = d;
    if (d <= slack) continue;
    /* a float that the script made from a 16.16 integer (floor(v * 65536) / 65536) moves by one quantum when the host rounds to nearest instead of down: that is a small absolute
       step, though many ulps for a small value. Only normal floats are judged so; the integer words (colours, flags) keep the ulp rule */
    const uint32_t ea = (ua >> 23) & 255u, eb = (ub >> 23) & 255u;
    if (ea != 0 && ea != 255 && eb != 0 && eb != 255) {
      float fa, fb;
      memcpy(&fa, &ua, 4);
      memcpy(&fb, &ub, 4);
      const float step = fa > fb ? fa - fb : fb - fa;
      if (step <= 1.0f / 65536.0f * 1.001f) continue;
    }
    return 0;
  }
  return 1;
}

int suite_csl_trace(int argc, char **argv)
{
  if (argc < 4) {
    printf("usage: suite_csl_trace <assets dir> <oracle fixtures dir> <mesh fixtures dir>\n");
    return 2;
  }
  char error[512];
  ct_assets assets;
  CHECK(ct_assets_load(&assets, argv[1], error, sizeof error), "assets: %s", error);
  if (!assets.data[CT_ASSET_SCRIPT]) return 1;

  gt3_program prog;
  CHECK(gt3_program_load(&prog, assets.data[CT_ASSET_SCRIPT], assets.size[CT_ASSET_SCRIPT], error, sizeof error), "script: %s", error);
  /* the size of the production's script */
  CHECK(prog.count == 21619u, "%u words", prog.count);
  int users = 0;
  for (int i = 0; i < GT3_MAXUSER; i++) users += prog.user[i] != 0;
  CHECK(users == 21, "%d user words", users);
  CHECK(prog.user[GT3_WORD_GENERATE] && prog.user[GT3_WORD_TIMELINE_INIT] && prog.user[GT3_WORD_FRAME], "the three entry points exist");

  gt3_vm *vm = (gt3_vm *)calloc(1, sizeof *vm);
  gt3_vm_init(vm, &prog);
  gt3_register_natives(vm);
  char path[2048];
  const int n_frames = 40;
  const double frame_step = 5.0;
  const char *variant = argc > 4 && !strcmp(argv[4], "old") ? "_exe_mesh" : ""; /* the suites run side by side: each has its own files */
  snprintf(path, sizeof path, "%s/csl_trace%s.txt", CANDYTRON_TEST_WORK_DIR, variant);
  vm->trace = fopen(path, "w+");
  CHECK(vm->trace != NULL, "cannot write %s", path);
  if (!vm->trace) return 1;

  /* CANDYTRON_TEST_NEAREST=1 runs the machine in the host's default rounding mode, as WebAssembly must */
  const int rounding = getenv("CANDYTRON_TEST_NEAREST") ? -1 : gt3_round_down();
  /* the 64 bit pixels are exact only in the original's rounding mode and with the original's power function (the x87 itself: gt3_fpow); the software power function, and a
     host that rounds to nearest, leave a few 15 bit lanes off by one, which the 8 bit textures of the suite `textures` do not show */
#ifdef GT3_FPOW_X87
  const int keep_bitmaps = rounding >= 0;
#else
  const int keep_bitmaps = 0;
#endif
  vm->trace_bitmap = keep_bitmaps ? trace_bitmap : NULL;
  vm->trace_mesh = trace_mesh;
  /* "old": the executable's character (assets/babe.mesh.old) instead of the source tree's, against calls_exe_mesh.txt, the trace of the oracle built with the key coder of the
     executable: the topology is the source tree's, the keys another quantisation of the animation, so the vertex hashes differ from calls.txt */
  const int old_mesh = argc > 4 && !strcmp(argv[4], "old");
  const int keep_meshes = keep_bitmaps;
  gt3_session s;
  memset(&s, 0, sizeof s);
  char babe_path[2048];
  snprintf(babe_path, sizeof babe_path, "%s/babe.mesh", argv[3]);
  size_t babe_size = 0;
  char *babe = old_mesh ? NULL : read_all(babe_path, &babe_size);
  CHECK(old_mesh || babe != NULL, "cannot read %s", babe_path);
  s.babe = old_mesh ? assets.data[CT_ASSET_MESH] : (const uint8_t *)babe;
  s.babe_size = old_mesh ? assets.size[CT_ASSET_MESH] : babe_size;
  char state_path[2048];
  snprintf(state_path, sizeof state_path, "%s/csl_state%s.txt", CANDYTRON_TEST_WORK_DIR, variant);
  s.state = fopen(state_path, "w+");
  CHECK(s.state != NULL, "cannot write %s", state_path);
  if (!s.state) return 1;
  const int ok_init = gt3_session_init(&s, vm);
  CHECK(ok_init, "the generation, the timeline init and the pre-roll ran without an error (pc %d)", (int)vm->pc);
  const uint32_t stores_before_frames = vm->member_stores;
  int frames_ok = 1;
  for (int k = 0; k < n_frames && ok_init; k++) frames_ok &= gt3_session_frame(&s, frame_step * k);
  const uint32_t stores_in_frames = vm->member_stores - stores_before_frames;
  CHECK(frames_ok, "%d frames ran without an error", n_frames);
  gt3_round_restore(rounding);
  fclose(s.state);
  gt3_session_exit(&s);
  fclose(vm->trace);
  vm->trace = NULL;

  /* the two traces */
  size_t n_ours = 0, n_fix = 0;
  char *ours_text = read_all(path, &n_ours);
  char fix_path[2048];
  snprintf(fix_path, sizeof fix_path, "%s/%s", argv[2], old_mesh ? "calls_exe_mesh.txt" : "calls.txt");
  char *fix_text = read_all(fix_path, &n_fix);
  CHECK(ours_text && fix_text, "cannot read the traces");
  if (!ours_text || !fix_text) return 1;
  char **ours, **theirs, **ours_base;
  size_t lo = split(ours_text, &ours);
  ours_base = ours;
  const size_t lt = split(fix_text, &theirs);

  /* the fixture: skip the comment header; reduce every line */
  size_t first = 0;
  while (first < lt && theirs[first][0] == '#') first++;
  const int slack = rounding >= 0 ? 0 : (getenv("CANDYTRON_TEST_SLACK") ? atoi(getenv("CANDYTRON_TEST_SLACK")) : 2);
  int tolerated = 0;
  size_t mismatches = 0, calls = 0, fixture_calls = 0;
  const size_t nl = lt - first;
  for (size_t i = first; i < lt; i++) fixture_calls += theirs[i][0] == 'C';
  CHECK(lo == nl, "the traces have %lu and %lu lines", (unsigned long)lo, (unsigned long)nl);
  for (size_t i = 0; i < lo && i < nl; i++) {
    reduce(theirs[first + i], keep_bitmaps, keep_meshes);
    reduce(ours[i], keep_bitmaps, keep_meshes);
    if (ours[i][0] == 'C') calls++;
    if (!same_line(ours[i], theirs[first + i], slack, &tolerated)) {
      if (mismatches < 12) printf("line %lu:\n  port:   %.230s\n  oracle: %.230s\n", (unsigned long)(i + 1), ours[i], theirs[first + i]);
      mismatches++;
    }
  }
  CHECK(mismatches == 0, "%lu of %lu lines differ", (unsigned long)mismatches, (unsigned long)lo);
  CHECK(calls == 2654, "%lu native calls (the fixture has %lu)", (unsigned long)calls, (unsigned long)fixture_calls);
  CHECK(vm->progress == 2397, "%u progress steps (the original's progress bar)", vm->progress);
  printf("csl_trace: %u member stores in all, %u of them in the %d frames\n", vm->member_stores, stores_in_frames, n_frames);
  printf("csl_trace: %lu lines, %lu native calls, rounding %s, %d integer arguments tolerated\n", (unsigned long)lo, (unsigned long)calls,
         rounding >= 0 ? "downward (exact)" : "to nearest (tolerant)", tolerated);

  free(ours_base);
  free(theirs);
  free(ours_text);
  free(fix_text);
  free(babe);

  /* the members the script wrote: spline results, scene and effect parameters, the arithmetic of the generation */
  size_t n_state_ours = 0, n_state_fix = 0;
  char *state_ours_text = read_all(state_path, &n_state_ours);
  snprintf(fix_path, sizeof fix_path, "%s/state.txt", argv[2]);
  char *state_fix_text = read_all(fix_path, &n_state_fix);
  CHECK(state_ours_text && state_fix_text, "cannot read the state dumps");
  if (state_ours_text && state_fix_text) {
    char **so, **sf;
    size_t lso = split(state_ours_text, &so);
    const size_t lsf = split(state_fix_text, &sf);
    size_t f0 = 0;
    while (f0 < lsf && sf[f0][0] == '#') f0++;
    const long state_slack = rounding >= 0 ? 0 : (getenv("CANDYTRON_TEST_STATE_SLACK") ? atol(getenv("CANDYTRON_TEST_STATE_SLACK")) : 4096);
    long worst = 0;
    size_t bad = 0;
    CHECK(lso == lsf - f0, "the state dumps have %lu and %lu lines", (unsigned long)lso, (unsigned long)(lsf - f0));
    for (size_t i = 0; i < lso && i < lsf - f0; i++) {
      if (!same_state(so[i], sf[f0 + i], state_slack, &worst)) {
        if (bad < 8) printf("state line %lu:\n  port:   %.230s\n  oracle: %.230s\n", (unsigned long)(i + 1), so[i], sf[f0 + i]);
        bad++;
      }
    }
    CHECK(bad == 0, "%lu of %lu state lines differ", (unsigned long)bad, (unsigned long)lso);
    printf("csl_trace: %lu state lines, rounding %s, largest difference %ld units in the last place\n", (unsigned long)lso,
           rounding >= 0 ? "downward (exact)" : "to nearest (tolerant)", worst);
    free(so);
    free(sf);
  }
  free(state_ours_text);
  free(state_fix_text);
  gt3_vm_exit(vm);
  free(vm);
  gt3_program_free(&prog);
  ct_assets_free(&assets);
  printf("csl_trace: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
