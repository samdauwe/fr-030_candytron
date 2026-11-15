/* csl_natives.h - the natives of Candytron's script (the intro player's table of genplayer.cpp) - header-only C99 (stb style)

   What a script can observe of a native is its return value (an integer, or an object), the integers it pushes (the splines), and the members it writes
   into its objects (a scene's SRT, an effect chain's Color, Zoom, CamPos ...): those are implemented exactly. What the script cannot observe, the pixels of a
   bitmap, the vertices of a mesh, what a scene draws, is made by the engines the natives call: bitmap.h (textures), mesh.h and babe.h (meshes) and render.h (the
   geometry device and the effect chains).

   What is kept of the object graph is what FindLabel walks, because the script looks objects up by label and the trace shows which one it got:
     scene       link[GT3_SCENE_MESH], link[GT3_SCENE_LIGHT], then the children
     mesh        link[GT3_MESH_MTRL1] (material 1), link[GT3_MESH_ANIM + i] the animation labels (hidden objects of class GENMESHANIM)
     fx chain    link[GT3_FX_OBJECT], link[GT3_FX_INPUT + i] i < 4
     material    link[i] the 16 passes (hidden objects of class GENMATPASS, created on demand)
     material pass  link[GT3_PASS_TEXTRANS + i] the texture transforms (class GENMATRIX), link[GT3_PASS_TEXTURE + i] the bitmaps
   as in the original's FindLabel for the intro (sINTRO_X) build.

   Rounding: the original runs the x87 unit in the mode 0x143f, round down, and its float to integer conversion in sFtol is a floor, while the casts the
   compiler makes truncate (_ftol2). The natives here use `float` arithmetic and expect the host to have switched the rounding mode to downward
   (gt3_round_down(), done by the session): with it the results are those of the original up to the last bit of a binary32. On a host that cannot
   (WebAssembly) they are those of round to nearest, and the trace comparison has a tolerance for it.

   USAGE
     #define GT3_NATIVES_IMPLEMENTATION in the translation unit that has GT3_CSL_IMPLEMENTATION; gt3_register_natives(vm) adds all of them.
*/
#ifndef GT3_NATIVES_H_INCLUDED
#define GT3_NATIVES_H_INCLUDED

#include <candytron/core/babe.h>
#include <candytron/core/bitmap.h>
#include <candytron/core/csl.h>
#include <candytron/core/mesh.h>
#include <candytron/core/rand.h>
#include <candytron/core/render.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GT3_NATIVES_API
#ifdef GT3_NATIVES_STATIC
#define GT3_NATIVES_API static
#else
#define GT3_NATIVES_API extern
#endif
#endif

enum { GT3_SCENE_MESH = 0, GT3_SCENE_LIGHT = 1, GT3_SCENE_PART = 2 };
enum { GT3_MESH_MTRL1 = 0, GT3_MESH_ANIM = 1, GT3_MESH_MAXANIM = 32 };
enum { GT3_FX_OBJECT = 0, GT3_FX_INPUT = 1, GT3_FX_MAXIN = 4 };
enum { GT3_MAT_MAXPASS = 16 };
enum { GT3_PASS_TEXTRANS = 0, GT3_PASS_TEXTURE = 8, GT3_PASS_MAXTEX = 8 };

/* The render targets FXMaster creates before the script runs (CreateRendertargets: 1 of 1024x1024, 3 of 512x256, 3 of 256x256, 3 of 128x64) take the first ten
   texture handles, so that the first bitmap made a texture gets 10. */
#define GT3_RENDER_TARGETS 10

/* Bitmap_Text asks the host for the coverage of a string (the original's sSystem->RenderFont, GDI Arial): `width` x `height` is the bitmap, x, y the position and cw, ch
   the size in pixels, in the order and with the integer arithmetic of the original's call (cw = height * width / 0x10000 of the script's values, ch likewise). It writes
   width * height coverage bytes and returns 1; 0 leaves the text blank. */
typedef int (*gt3_text_fn)(void *user, const char *text, int width, int height, int x, int y, int cw, int ch, uint8_t *coverage);

/* A texture as Bitmap_MakeTexture hands it to the device: 8 bit blue, green, red, alpha. The render targets have none. */
typedef struct gt3_texture {
  int32_t width, height;
  uint8_t *bgra;
} gt3_texture;

/* What the generation natives share, the original's globals: the generator, the Perlin tables, the table of textures. vm->user points to one. */
typedef struct gt3_gen {
  gt3_rng rng;
  gt3_perlin perlin;
  gt3_texture *texture; /* the handle is the index */
  int32_t n_texture, cap_texture;
  gt3_text_fn text;     /* NULL: every string is blank */
  void *text_user;
  gt3_pow_fn pow;       /* NULL: gt3_fpow */
  gt3_mesh_ctx *mesh_ctx;
  const uint8_t *babe;  /* the character's mesh as Mesh_Babe reads it (the intro links it in); NULL: Babe makes an empty mesh */
  size_t babe_size;
  gt3_render
    *render; /* the geometry device, the viewports of the effect chains and the camera (render.h): what the script's scenes and chains are painted with */
} gt3_gen;

/* sInitPerlin() and the render targets: what the original does before the script runs. */
GT3_NATIVES_API void gt3_gen_init(gt3_gen *g);
GT3_NATIVES_API void gt3_gen_exit(gt3_gen *g);

/* registers every native of the intro table (the words the script reaches and the duplicates of the original's table) */
GT3_NATIVES_API void gt3_register_natives(gt3_vm *vm);

/* Switches the floating point unit to the mode of the original (round downward) and back. Returns the previous mode, or -1 where the host has no
   control over it (the natives then compute round to nearest). */
GT3_NATIVES_API int gt3_round_down(void);
GT3_NATIVES_API void gt3_round_restore(int mode);

#ifdef __cplusplus
}
#endif

#endif /* GT3_NATIVES_H_INCLUDED */

#ifdef GT3_NATIVES_IMPLEMENTATION
#ifndef GT3_NATIVES_IMPLEMENTATION_DONE
#define GT3_NATIVES_IMPLEMENTATION_DONE

#include <fenv.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

GT3_NATIVES_API int gt3_round_down(void)
{
#if defined(FE_DOWNWARD) && !defined(__EMSCRIPTEN__)
  const int old = fegetround();
  if (old < 0 || fesetround(FE_DOWNWARD) != 0) return -1;
  return old;
#else
  return -1;
#endif
}

GT3_NATIVES_API void gt3_gen_init(gt3_gen *g)
{
  memset(g, 0, sizeof *g);
  gt3_rng_init(&g->rng);
  gt3_perlin_init(&g->perlin, &g->rng);
  g->texture = (gt3_texture *)calloc(64, sizeof *g->texture);
  g->cap_texture = g->texture ? 64 : 0;
  g->n_texture = g->texture ? GT3_RENDER_TARGETS : 0;
  g->mesh_ctx = gt3_mesh_ctx_create(&g->rng, &g->perlin);
  g->render = (gt3_render *)calloc(1, sizeof *g->render);
  if (g->render && !gt3_render_init(g->render, 800, 600)) {
    gt3_render_exit(g->render);
    free(g->render);
    g->render = NULL;
  }
}

GT3_NATIVES_API void gt3_gen_exit(gt3_gen *g)
{
  for (int32_t i = 0; i < g->n_texture; i++) free(g->texture[i].bgra);
  free(g->texture);
  gt3_mesh_ctx_destroy(g->mesh_ctx);
  if (g->render) gt3_render_exit(g->render);
  free(g->render);
  memset(g, 0, sizeof *g);
}

GT3_NATIVES_API void gt3_round_restore(int mode)
{
#if defined(FE_DOWNWARD) && !defined(__EMSCRIPTEN__)
  if (mode >= 0) fesetround(mode);
#else
  (void)mode;
#endif
}

/* ---- helpers ---- */

/* int / 65536.0f of the original: fild loads the integer exactly, the division is rounded once (to 24 bits, downward) */
static float gt3_i2f(int32_t v) { return (float)((long double)v / 65536.0L); }
static int32_t gt3_f2bits(float f)
{
  int32_t v;
  memcpy(&v, &f, sizeof v);
  return v;
}
/* sFtol: fistp under the original's control word: a floor */
static int32_t gt3_floor_i(float f)
{
  if (!(f > -2147483904.0f && f < 2147483648.0f)) return (int32_t)0x80000000u;
  return (int32_t)floorf(f);
}

static gt3_object *gt3_obj(gt3_vm *vm, int32_t h) { return gt3_object_get(vm, h); }

static int32_t gt3_make(gt3_vm *vm, uint32_t cls, gt3_object **out)
{
  const int32_t h = gt3_new_object(vm, cls);
  *out = gt3_object_get(vm, h);
  return h;
}

/* ---- meshes, bitmaps: identity natives with the bookkeeping that FindLabel needs ---- */

static int32_t gt3_n_ret0(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)vm;
  (void)ints;
  return objs[0];
}

/* ---- bitmaps (genbitmap.cpp): the bitmap natives. Without a generation context (vm->user) the objects are made but have no pixels. ---- */

static gt3_gen *gt3_gen_of(gt3_vm *vm) { return (gt3_gen *)vm->user; }

static float gt3_bits2f(int32_t v)
{
  float f;
  memcpy(&f, &v, sizeof f);
  return f;
}

static gt3_bitmap *gt3_bitmap_of(gt3_vm *vm, int32_t handle)
{
  gt3_object *o = gt3_obj(vm, handle);
  return (o && o->cls == GT3_CID_BITMAP) ? (gt3_bitmap *)o->data : NULL;
}

/* the original's SCRIPTVERIFY: a native that is given no bitmap stops the script */
static int gt3_need_bitmap(gt3_vm *vm, gt3_gen *g, const gt3_bitmap *bm)
{
  if (!g) return 0;
  if (bm) return 1;
  vm->error = 1;
  vm->error_message = "a bitmap native was given something that is not a bitmap";
  return 0;
}

/* a new bitmap object that owns `bm` */
static int32_t gt3_new_bitmap(gt3_vm *vm, gt3_bitmap *bm)
{
  gt3_object *o;
  const int32_t h = gt3_make(vm, GT3_CID_BITMAP, &o);
  if (!o) {
    free(bm);
    return 0;
  }
  o->data = bm;
  return h;
}

/* Bitmap_MakeTexture: the 8 bit pixels go to the table, the handle to the bitmap */
static void gt3_make_texture(gt3_gen *g, gt3_bitmap *bm)
{
  if (g->n_texture == g->cap_texture) {
    const int32_t cap = g->cap_texture ? g->cap_texture * 2 : 64;
    gt3_texture *bigger = (gt3_texture *)realloc(g->texture, (size_t)cap * sizeof *bigger);
    if (!bigger) return;
    g->texture = bigger;
    g->cap_texture = cap;
  }
  uint8_t *bgra = (uint8_t *)malloc((size_t)bm->width * (size_t)bm->height * 4u);
  if (!bgra) return;
  gt3_bitmap_to_bgra(bm, bgra);
  g->texture[g->n_texture].width = bm->width;
  g->texture[g->n_texture].height = bm->height;
  g->texture[g->n_texture].bgra = bgra;
  bm->texture = g->n_texture++;
}

static int32_t gt3_n_bitmap_maketexture(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)ints;
  gt3_gen *g = gt3_gen_of(vm);
  gt3_bitmap *bm = gt3_bitmap_of(vm, objs[0]);
  if (gt3_need_bitmap(vm, g, bm)) gt3_make_texture(g, bm);
  return objs[0];
}

static int32_t gt3_n_bitmap_flat(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)objs;
  gt3_gen *g = gt3_gen_of(vm);
  gt3_bitmap *bm = g ? gt3_bitmap_create(GT3_BITMAP_SIZE, GT3_BITMAP_SIZE) : NULL;
  if (bm) gt3_bitmap_flat(bm, ints);
  return gt3_new_bitmap(vm, bm);
}

static int32_t gt3_n_bitmap_merge(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_gen *g = gt3_gen_of(vm);
  gt3_bitmap *bm = gt3_bitmap_of(vm, objs[0]), *bb = gt3_bitmap_of(vm, objs[1]);
  const int mode = gt3_sar(ints[0], 16);
  if (gt3_need_bitmap(vm, g, bm) && gt3_need_bitmap(vm, g, bb)) {
    if (mode >= 0 && mode < 5)
      gt3_bitmap_merge(bm, bb, mode);
    else {
      vm->error = 1;
      vm->error_message = "Bitmap_Merge: the mode is not 0..4";
    }
  }
  return objs[0];
}

static int32_t gt3_n_bitmap_color(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_gen *g = gt3_gen_of(vm);
  gt3_bitmap *bm = gt3_bitmap_of(vm, objs[0]);
  const int mode = gt3_sar(ints[0], 16);
  if (gt3_need_bitmap(vm, g, bm)) {
    if (mode >= 0 && mode < 6)
      gt3_bitmap_color(bm, mode, ints + 1);
    else {
      vm->error = 1;
      vm->error_message = "Bitmap_Color: the mode is not 0..5";
    }
  }
  return objs[0];
}

static int32_t gt3_n_bitmap_glowrect(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_gen *g = gt3_gen_of(vm);
  gt3_bitmap *bm = gt3_bitmap_of(vm, objs[0]);
  if (gt3_need_bitmap(vm, g, bm))
    gt3_bitmap_glowrect(bm, gt3_bits2f(ints[0]), gt3_bits2f(ints[1]), gt3_bits2f(ints[2]), gt3_bits2f(ints[3]), gt3_bits2f(ints[4]), gt3_bits2f(ints[5]),
                        ints + 6, gt3_bits2f(ints[10]), gt3_bits2f(ints[11]), g->pow);
  return objs[0];
}

static int32_t gt3_n_bitmap_perlin(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)objs;
  gt3_gen *g = gt3_gen_of(vm);
  gt3_bitmap *bm = g ? gt3_bitmap_perlin(&g->perlin, ints[0], ints[1], gt3_bits2f(ints[2]), ints[3], ints[4], gt3_bits2f(ints[5]), gt3_bits2f(ints[6]),
                                         ints + 7, ints + 11, g->pow)
                     : NULL;
  return gt3_new_bitmap(vm, bm);
}

/* the original's call: RenderFont(w, h, "arial", text, x * w / 0x10000, y * h / 0x10000, height * w / 0x10000, width * h / 0x10000), a coverage in the low byte of each pixel */
static int32_t gt3_n_bitmap_text(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_gen *g = gt3_gen_of(vm);
  gt3_bitmap *bm = gt3_bitmap_of(vm, objs[0]);
  if (!gt3_need_bitmap(vm, g, bm)) return objs[0];
  const int w = bm->width, h = bm->height;
  uint8_t *coverage = NULL;
  const char *text = gt3_string(vm, ints[8]);
  if (g->text && text) {
    coverage = (uint8_t *)calloc((size_t)w * (size_t)h, 1);
    if (coverage && !g->text(g->text_user, text, w, h, (int)((int64_t)ints[0] * w / 0x10000), (int)((int64_t)ints[1] * h / 0x10000),
                             (int)((int64_t)ints[3] * w / 0x10000), (int)((int64_t)ints[2] * h / 0x10000), coverage)) {
      free(coverage);
      coverage = NULL;
    }
  }
  gt3_bitmap_text(bm, ints + 4, coverage);
  free(coverage);
  return objs[0];
}

/* ---- meshes (genmesh.cpp Mesh_*): the mesh natives. The engine is in mesh.h; the object keeps the bookkeeping FindLabel and the script's members need ---- */

static gt3_mesh *gt3_mesh_of(gt3_vm *vm, int32_t h)
{
  gt3_object *o = gt3_obj(vm, h);
  return (o && o->cls == GT3_CID_MESH) ? (gt3_mesh *)o->data : NULL;
}

static void gt3_destroy_mesh_data(void *data) { gt3_mesh_destroy((gt3_mesh *)data); }

/* the handles inside an object's data: a mesh's materials */
static void gt3_walk_data(const gt3_object *o, void (*visit)(void *ctx, int32_t handle), void *ctx)
{
  if (o->cls != GT3_CID_MESH) return;
  const gt3_mesh *m = (const gt3_mesh *)o->data;
  for (int32_t i = 0; i < m->n_mtrl; i++) visit(ctx, m->mtrl[i]);
}

/* what the object shows of the engine: the recorder state that decides whether AnimLabel makes an object */
static void gt3_mesh_sync(gt3_vm *vm, int32_t h)
{
  gt3_object *o = gt3_obj(vm, h);
  const gt3_mesh *m = o ? (const gt3_mesh *)o->data : NULL;
  if (m) {
    o->store_mode = m->store_mode;
    o->anim_last_op = m->anim_last_op;
  }
}

static void gt3_bits2fv(const int32_t *ints, float *out, int n)
{
  for (int i = 0; i < n; i++) out[i] = gt3_bits2f(ints[i]);
}

static int32_t gt3_n_mesh_new(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)objs;
  (void)ints;
  gt3_gen *g = gt3_gen_of(vm);
  gt3_object *o;
  const int32_t h = gt3_make(vm, GT3_CID_MESH, &o);
  if (!o || !g || !g->mesh_ctx) return h;
  gt3_mesh *m = gt3_mesh_create(g->mesh_ctx);
  if (!m) return 0;
  gt3_mesh_new(m);
  o->data = m;
  return h;
}

static int32_t gt3_n_mesh_cube(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_mesh *m = gt3_mesh_of(vm, objs[0]);
  if (m && !gt3_mesh_cube(m, ints[0], ints[1], ints[2], ints[3])) vm->error = 1;
  return objs[0];
}

static int32_t gt3_n_mesh_selectcube(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_mesh *m = gt3_mesh_of(vm, objs[0]);
  float c[3], s[3];
  gt3_bits2fv(ints, c, 3);
  gt3_bits2fv(ints + 3, s, 3);
  if (m) gt3_mesh_selectcube(m, c, s, ints[6], (uint32_t)ints[7], (uint32_t)ints[8]);
  return objs[0];
}

static int32_t gt3_n_mesh_subdivide(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_mesh *m = gt3_mesh_of(vm, objs[0]);
  if (m) gt3_mesh_subdivide(m, ints[0], gt3_bits2f(ints[1]), ints[2]);
  return objs[0];
}

static int32_t gt3_n_mesh_transform(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_mesh *m = gt3_mesh_of(vm, objs[0]);
  float srt[9];
  gt3_bits2fv(ints + 1, srt, 9);
  if (m) gt3_mesh_transform(m, ints[0], srt);
  gt3_mesh_sync(vm, objs[0]);
  return objs[0];
}

static int32_t gt3_n_mesh_material(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *o = gt3_obj(vm, objs[0]);
  gt3_mesh *m = gt3_mesh_of(vm, objs[0]);
  if (m) gt3_mesh_material(m, objs[1], ints[0], ints[1]);
  if (o && (ints[0] >> 16) == 1) o->link[GT3_MESH_MTRL1] = objs[1]; /* FindLabel only looks at material 1 */
  return objs[0];
}

static int32_t gt3_n_mesh_calcnormals(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_mesh *m = gt3_mesh_of(vm, objs[0]);
  if (m) gt3_mesh_calcnormals(m, ints[0], ints[1]);
  return objs[0];
}

static int32_t gt3_n_mesh_triangulate(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_mesh *m = gt3_mesh_of(vm, objs[0]);
  if (m) gt3_mesh_triangulate(m, ints[0], ints[1], ints[2]);
  return objs[0];
}

static int32_t gt3_n_mesh_perlin(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_mesh *m = gt3_mesh_of(vm, objs[0]);
  float srt[9], ampl[3];
  gt3_bits2fv(ints + 1, srt, 9);
  gt3_bits2fv(ints + 10, ampl, 3);
  if (m) gt3_mesh_perlin(m, ints[0], srt, ampl);
  gt3_mesh_sync(vm, objs[0]);
  return objs[0];
}

static int32_t gt3_n_mesh_selectrandom(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_mesh *m = gt3_mesh_of(vm, objs[0]);
  if (m) gt3_mesh_selectrandom(m, (uint32_t)ints[0], ints[1], (uint32_t)ints[2], (uint32_t)ints[3]);
  return objs[0];
}

static int32_t gt3_n_mesh_extrudewk(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_mesh *m = gt3_mesh_of(vm, objs[0]);
  float s[3], r[3];
  gt3_bits2fv(ints + 4, s, 3);
  gt3_bits2fv(ints + 7, r, 3);
  if (m) gt3_mesh_extrudewk(m, ints[0], ints[1], ints[2], gt3_bits2f(ints[3]), s, r, ints[10]);
  gt3_mesh_sync(vm, objs[0]);
  return objs[0];
}

static int32_t gt3_n_mesh_beginrecord(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)ints;
  gt3_mesh *m = gt3_mesh_of(vm, objs[0]);
  if (m) gt3_mesh_beginrecord(m);
  gt3_mesh_sync(vm, objs[0]);
  return objs[0];
}

/* Mesh_Babe(filename, dummy, anim0, anim1, phase, flags) */
static int32_t gt3_n_mesh_babe(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)objs;
  gt3_gen *g = gt3_gen_of(vm);
  gt3_object *o;
  const int32_t h = gt3_make(vm, GT3_CID_MESH, &o);
  if (!o || !g || !g->mesh_ctx) return h;
  gt3_mesh *m = gt3_mesh_create(g->mesh_ctx);
  if (!m) return 0;
  o->data = m;
  if (g->babe) {
    char error[160];
    if (!gt3_babe_read(m, g->babe, g->babe_size, NULL, error, sizeof error)) {
      vm->error = 1;
      vm->error_message = "Mesh_Babe: the mesh is damaged";
      return 0;
    }
  } else
    gt3_mesh_new(m);
  if (!(ints[5] & 0x10000)) gt3_mesh_storemode(m); /* the flags: bit 16 keeps the mesh out of the recorder's store mode */
  gt3_mesh_mark_anim(m, GT3_GMA_BONE);
  m->anim0 = ints[2] >> 16;
  m->anim1 = ints[3] >> 16;
  gt3_mesh_bones(m, (float)(ints[4] & 0xffff) / 65536.0f);
  gt3_mesh_all2mask(m, 0, ~0u);
  gt3_mesh_sync(vm, h);
  return h;
}

static int32_t gt3_n_mesh_animlabel(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *o = gt3_obj(vm, objs[0]);
  gt3_mesh *m = gt3_mesh_of(vm, objs[0]);
  const uint32_t flags = (uint32_t)ints[1] >> 16;
  if (!o || !m) return objs[0];
  const int32_t before = m->n_anim;
  const int32_t op = m->anim_last_op;
  gt3_mesh_animlabel(m, ints[0], flags);
  if (m->n_anim != before) {
    gt3_object *a;
    const int32_t h = gt3_make(vm, GT3_CID_MESHANIM, &a);
    o = gt3_obj(vm, objs[0]);
    if (!a || !o) return objs[0];
    a->m[0] = a->m[1] = a->m[2] = 0x10000; /* s */
    a->m[14] = op;                         /* Operation */
    a->m[15] = (int32_t)flags;
    a->label = (flags & 1) ? 0 : ints[0];  /* GMF_DOUBLE: this label has no connection */
    o->link[GT3_MESH_ANIM + o->anim_count++] = h;
  }
  return objs[0];
}

/* ---- Sys ---- */

static int32_t gt3_n_sys_copy(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)ints;
  gt3_object *src = gt3_obj(vm, objs[0]);
  if (!src) return 0;
  if (src->cls == GT3_CID_BITMAP) {
    const gt3_bitmap *from = (const gt3_bitmap *)src->data;
    gt3_bitmap *bm = from ? gt3_bitmap_copy(from) : NULL; /* GenBitmap::Copy: no texture */
    gt3_object *o;
    const int32_t h = gt3_make(vm, GT3_CID_BITMAP, &o);
    if (!o) {
      free(bm);
      return 0;
    }
    o->data = bm;
    o->label = gt3_obj(vm, objs[0])->label;
    return h;
  }
  if (src->cls == GT3_CID_MESH) {
    gt3_object *o;
    const int32_t h = gt3_make(vm, GT3_CID_MESH, &o);
    if (!o) return 0;
    src = gt3_obj(vm, objs[0]);
    if (src->data) o->data = gt3_mesh_copy((const gt3_mesh *)src->data); /* GenMesh::Copy */
    o->label = src->label;
    o->link[GT3_MESH_MTRL1] = src->link[GT3_MESH_MTRL1];                 /* the materials are shared */
    o->store_mode = src->store_mode;
    o->anim_last_op = src->anim_last_op;
    for (int i = 0; i < src->anim_count; i++) { /* the animation labels are copied */
      gt3_object *a;
      const int32_t ah = gt3_make(vm, GT3_CID_MESHANIM, &a);
      src = gt3_obj(vm, objs[0]);
      o = gt3_obj(vm, h);
      if (!a) return h;
      const gt3_object *from = gt3_obj(vm, src->link[GT3_MESH_ANIM + i]);
      if (!from) return h;
      memcpy(a->m, from->m, sizeof a->m);
      a->label = from->label;
      o->link[GT3_MESH_ANIM + o->anim_count++] = ah;
    }
    return h;
  }
  return objs[0];
}

/* the object graph of the intro's FindLabel (genmisc.cpp) */
static int32_t gt3_find_label(gt3_vm *vm, int32_t h, int32_t label, int depth)
{
  gt3_object *o = gt3_obj(vm, h);
  if (!o || depth > 64) return 0;
  if (o->label == label) return h;
  int32_t r;
  switch (o->cls) {
  case GT3_CID_SCENE:
    if ((r = gt3_find_label(vm, o->link[GT3_SCENE_MESH], label, depth + 1))) return r;
    if ((r = gt3_find_label(vm, o->link[GT3_SCENE_LIGHT], label, depth + 1))) return r;
    for (uint32_t i = 0; i < o->n_child; i++)
      if ((r = gt3_find_label(vm, gt3_obj(vm, h)->child[i], label, depth + 1))) return r;
    break;
  case GT3_CID_MESH:
    if ((r = gt3_find_label(vm, o->link[GT3_MESH_MTRL1], label, depth + 1))) return r;
    for (int i = 0; i < o->anim_count; i++)
      if ((r = gt3_find_label(vm, o->link[GT3_MESH_ANIM + i], label, depth + 1))) return r;
    break;
  case GT3_CID_FXCHAIN:
    if ((r = gt3_find_label(vm, o->link[GT3_FX_OBJECT], label, depth + 1))) return r;
    for (int i = 0; i < GT3_FX_MAXIN; i++)
      if ((r = gt3_find_label(vm, o->link[GT3_FX_INPUT + i], label, depth + 1))) return r;
    break;
  case GT3_CID_MATERIAL:
    for (int i = 0; i < GT3_MAT_MAXPASS; i++)
      if ((r = gt3_find_label(vm, o->link[i], label, depth + 1))) return r;
    break;
  case GT3_CID_MATPASS:
    for (int i = 0; i < GT3_PASS_MAXTEX; i++)
      if ((r = gt3_find_label(vm, o->link[GT3_PASS_TEXTRANS + i], label, depth + 1))) return r;
    break;
  default: break;
  }
  return 0;
}

static int32_t gt3_n_sys_findlabel(gt3_vm *vm, const int32_t *objs, const int32_t *ints) { return gt3_find_label(vm, objs[0], ints[0], 0); }

/* sFPow of the original (x87: fyl2x, f2xm1, fscale): 0 for a base of 0, not a number for a negative base; the result is truncated */
static int32_t gt3_n_sys_pow(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)vm;
  (void)objs;
  const double a = (double)gt3_i2f(ints[0]), b = (double)gt3_i2f(ints[1]);
  double r;
  if (a == 0.0) {
    r = 0.0;
  } else if (a < 0.0) {
    r = (double)NAN;
  } else {
    /* the x87 computes in 64 bits and stores a double; libm's pow in the mode "downward" can be one ulp below an exact result (4^-3 = 0.015625),
       which the truncation would show: so pow runs in the default mode */
    const int mode = fegetround();
    fesetround(FE_TONEAREST);
    r = pow(a, b);
    fesetround(mode);
  }
  return gt3_dtoi(r * (double)65536.0f);
}

/* ---- splines (genmisc.cpp) ---- */

typedef struct gt3_spline_key {
  float time, value[4];
} gt3_spline_key;
typedef struct gt3_spline {
  int dimensions;
  float tension, continuity, bias;
  gt3_spline_key *key;
  uint32_t n, cap;
} gt3_spline;

static void gt3_release_data(int32_t cls, void *data)
{
  if (cls == GT3_CID_SPLINE) free(((gt3_spline *)data)->key);
}

static gt3_spline *gt3_spline_of(gt3_vm *vm, int32_t h)
{
  gt3_object *o = gt3_obj(vm, h);
  return (o && o->cls == GT3_CID_SPLINE) ? (gt3_spline *)o->data : NULL;
}

static int32_t gt3_n_spline_new(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)objs;
  gt3_object *o;
  const int32_t h = gt3_make(vm, GT3_CID_SPLINE, &o);
  if (!o) return 0;
  gt3_spline *s = (gt3_spline *)calloc(1, sizeof *s);
  if (!s) return 0;
  s->dimensions = ints[0] >> 16;
  s->tension = gt3_i2f(ints[1]);
  s->continuity = gt3_i2f(ints[2]);
  o->data = s;
  return h;
}

static int32_t gt3_n_spline_sort(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)ints;
  gt3_spline *s = gt3_spline_of(vm, objs[0]);
  if (!s) return objs[0];
  for (uint32_t i = 0; i + 1 < s->n; i++)
    for (uint32_t j = i + 1; j < s->n; j++)
      if (s->key[i].time > s->key[j].time) {
        const gt3_spline_key t = s->key[i];
        s->key[i] = s->key[j];
        s->key[j] = t;
      }
  return objs[0];
}

static int32_t gt3_spline_add(gt3_vm *vm, int32_t h, const int32_t *ints, int count)
{
  gt3_spline *s = gt3_spline_of(vm, h);
  if (!s) return h;
  if (s->n == s->cap) {
    const uint32_t cap = s->cap ? s->cap * 2u : 16u;
    gt3_spline_key *bigger = (gt3_spline_key *)realloc(s->key, cap * sizeof *bigger);
    if (!bigger) return h;
    s->key = bigger;
    s->cap = cap;
  }
  gt3_spline_key *k = &s->key[s->n++];
  memset(k, 0, sizeof *k);
  k->time = gt3_i2f(ints[0]);
  for (int i = 0; i < count; i++) k->value[i] = gt3_i2f(ints[1 + i]);
  return h;
}

static int32_t gt3_n_spline_key1(gt3_vm *vm, const int32_t *o, const int32_t *i) { return gt3_spline_add(vm, o[0], i, 1); }
static int32_t gt3_n_spline_key2(gt3_vm *vm, const int32_t *o, const int32_t *i) { return gt3_spline_add(vm, o[0], i, 2); }
static int32_t gt3_n_spline_key3(gt3_vm *vm, const int32_t *o, const int32_t *i) { return gt3_spline_add(vm, o[0], i, 3); }
static int32_t gt3_n_spline_key4(gt3_vm *vm, const int32_t *o, const int32_t *i) { return gt3_spline_add(vm, o[0], i, 4); }

/* GenSpline::Calc, in binary32 like the original */
static void gt3_spline_calc(const gt3_spline *s, float time, float *val)
{
  if (s->n < 2) {
    for (int i = 0; i < s->dimensions; i++) val[i] = s->n == 0 ? 0.0f : s->key[0].value[i];
    return;
  }
  const float last = s->key[s->n - 1].time, first = s->key[0].time;
  time = (time >= last) ? last : (time <= first) ? first : time; /* sRange(time, last, first) */
  for (uint32_t i = 1; i < s->n; i++) {
    float t1 = s->key[i - 1].time, t2 = s->key[i].time;
    if (t1 >= t2) continue;
    if (time <= t2) {
      float a1 = (1.0f - s->tension) * (1.0f - s->continuity) * (1.0f + s->bias) / 2.0f;
      float b1 = (1.0f - s->tension) * (1.0f + s->continuity) * (1.0f - s->bias) / 2.0f;
      float a2 = (1.0f - s->tension) * (1.0f + s->continuity) * (1.0f + s->bias) / 2.0f;
      float b2 = (1.0f - s->tension) * (1.0f - s->continuity) * (1.0f - s->bias) / 2.0f;
      const float *p1 = s->key[i - 1].value, *p2 = s->key[i].value, *p0, *p3;
      float t0, t3;
      if (i >= 2) {
        p0 = s->key[i - 2].value;
        t0 = s->key[i - 2].time;
      } else {
        p0 = p2;
        a1 = -a1;
        t0 = t1 + (t1 - t2);
      }
      if (i + 1 < s->n) {
        p3 = s->key[i + 1].value;
        t3 = s->key[i + 1].time;
      } else {
        p3 = p1;
        b2 = -b2;
        t3 = t2 + (t2 - t1);
      }
      const float fade = (time - t1) / (t2 - t1);
      t0 = t1 - t0;
      t1 = t2 - t1;
      t2 = t3 - t2;
      const float f1 = 2.0f * fade * fade * fade - 3.0f * fade * fade + 1.0f;
      const float f2 = -2.0f * fade * fade * fade + 3.0f * fade * fade;
      float f3 = fade * fade * fade - 2.0f * fade * fade + fade;
      float f4 = fade * fade * fade - fade * fade;
      f3 = f3 * 2.0f * t1 / (t1 + t0);
      f4 = f4 * 2.0f * t1 / (t1 + t2);
      for (int j = 0; j < s->dimensions; j++) {
        val[j] = f1 * p1[j] + f2 * p2[j] + f3 * (a1 * (p1[j] - p0[j]) + b1 * (p2[j] - p1[j])) + f4 * (a2 * (p2[j] - p1[j]) + b2 * (p3[j] - p2[j]));
      }
      return;
    }
  }
}

static int32_t gt3_n_spline_calc(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_spline *s = gt3_spline_of(vm, objs[0]);
  if (!s) {
    vm->error = 1;
    return 0;
  }
  float val[4] = { 0, 0, 0, 0 };
  gt3_spline_calc(s, gt3_i2f(ints[0]), val);
  for (int i = 0; i < s->dimensions && i < 4; i++) gt3_push_i(vm, gt3_floor_i(val[i] * 65536.0f));
  return 0;
}

static gt3_render *gt3_render_of(gt3_vm *vm)
{
  gt3_gen *g = (gt3_gen *)vm->user;
  return g ? g->render : NULL;
}

/* ---- the state of a material pass: what GenMatPass keeps besides the members (genmaterial.cpp) ---- */

typedef struct gt3_passdata {
  uint32_t states[256]; /* the Direct3D state stream the natives append to */
  int32_t n_state, optimised;
  uint32_t flags;       /* 1: set the texture factor */
  uint32_t lightmask;   /* default 0xffff */
  int32_t transmode[GT3_PASS_MAXTEX];
  float matlight[17];
} gt3_passdata;

static gt3_passdata *gt3_passd(const gt3_object *p) { return (gt3_passdata *)p->data; }

/* GetColor32 of genbitmap.cpp */
static uint32_t gt3_color32(const int32_t c[4])
{
#define GT3_C8(v) ((uint32_t)((gt3_sar((v), 8) >= 0xff) ? 0xff : (gt3_sar((v), 8) <= 0) ? 0 : gt3_sar((v), 8)))
  return GT3_C8(c[2]) << 0 | GT3_C8(c[1]) << 8 | GT3_C8(c[0]) << 16 | GT3_C8(c[3]) << 24;
#undef GT3_C8
}

static void gt3_matlight_default(float m[17])
{
  for (int i = 0; i < 12; i++) m[i] = 1.0f;  /* diffuse, ambient, specular */
  for (int i = 12; i < 16; i++) m[i] = 0.0f; /* emissive */
  m[16] = 1.0f;                              /* power */
}

/* GenMatPass::Set (the intro's): the state stream, the material, the texture factor, and the textures with their matrices */
static void gt3_pass_set(gt3_vm *vm, gt3_render *r, gt3_object *p)
{
  gt3_passdata *d = gt3_passd(p);
  if (!d->optimised) {
    uint32_t *ptr = d->states + d->n_state;
    gt3_state_end(&ptr);
    d->n_state = (int32_t)(ptr - d->states);
    d->optimised = 1;
  }
  gt3_set_states(r, d->states);
  gt3_set_matlight(r, d->matlight);
  if (d->flags & 1) gt3_set_state(r, GT3_D3DRS_TEXTUREFACTOR, gt3_color32(p->m + 3));
  for (int i = 0; i < 2; i++) {
    const gt3_bitmap *bm = gt3_bitmap_of(vm, p->link[GT3_PASS_TEXTURE + i]);
    if (bm && bm->texture != -1) {
      gt3_mat ttm, m2, m3;
      const gt3_object *tt = gt3_obj(vm, p->link[GT3_PASS_TEXTRANS + i]);
      gt3_get_transform(r, d->transmode[i], &ttm);
      gt3_mat_transr(&ttm);
      if (tt) {
        float srt[9];
        for (int k = 0; k < 9; k++) srt[k] = gt3_bits2f(tt->m[k]);
        gt3_set_state(r, GT3_D3DTSS_TEXTURETRANSFORMFLAGS + (uint32_t)i * 0x20u, 2);
        m2 = ttm;
        gt3_mat_srt(&m3, srt);
        gt3_mat_mul4(&ttm, &m2, &m3);
      } else if (d->transmode[i] == 0) {
        gt3_mat_init(&ttm);
        ttm.i.x = 0.5f;
        ttm.j.y = 0.5f;
        ttm.k.z = 0.5f;
        ttm.l.x = 0.5f;
        ttm.l.y = 0.5f;
        ttm.l.z = 0.5f;
      }
      gt3_set_texture(r, i, bm->texture, &ttm);
    } else
      gt3_set_texture(r, i, -1, NULL);
  }
}

/* MatPass::Set() of pass `pass` of material `material` (0: the default material's pass: no states, the default material, no textures) */
static void gt3_set_pass_cb(void *user, int32_t material, int pass)
{
  gt3_vm *vm = (gt3_vm *)user;
  gt3_render *r = gt3_render_of(vm);
  if (!r) return;
  if (material == 0) {
    float m[17];
    gt3_matlight_default(m);
    gt3_set_matlight(r, m);
    gt3_set_texture(r, 0, -1, NULL);
    gt3_set_texture(r, 1, -1, NULL);
    return;
  }
  const gt3_object *mat = gt3_obj(vm, material);
  gt3_object *p = (mat && mat->cls == GT3_CID_MATERIAL && pass >= 0 && pass < GT3_MAT_MAXPASS) ? gt3_obj(vm, mat->link[pass]) : NULL;
  if (p && p->data) gt3_pass_set(vm, r, p);
}

static uint32_t gt3_pass_lightmask(gt3_vm *vm, int32_t material, int pass)
{
  const gt3_object *mat = gt3_obj(vm, material);
  const gt3_object *p = (mat && mat->cls == GT3_CID_MATERIAL && pass >= 0 && pass < GT3_MAT_MAXPASS) ? gt3_obj(vm, mat->link[pass]) : NULL;
  return (p && p->data) ? gt3_passd(p)->lightmask : 0xffffu;
}

/* ---- what the renderer reads of materials, meshes and scenes (genmaterial.cpp: GenMesh::Prepare / Paint, GenScene::Paint) ---- */

enum { GT3_PASSM_PROGRAMM = 8 }; /* GenMatPass::Programm, kept in a member the script cannot reach and the state dump does not show */

/* the pass `pass` of material `material` (handle, 0: the default material, one pass with the defaults of GenMatPass) */
static int gt3_pass_info(void *user, int32_t material, int pass, int32_t *programm, float *size, float *aspect, float *enlarge)
{
  gt3_vm *vm = (gt3_vm *)user;
  if (material == 0) {
    if (pass != 0) return 0;
    *programm = GT3_MPP_STATIC;
    *size = *aspect = *enlarge = 0.0f;
    return 1;
  }
  gt3_object *mat = gt3_object_get(vm, material);
  if (!mat || mat->cls != GT3_CID_MATERIAL || pass < 0 || pass >= GT3_MAT_MAXPASS) return 0;
  gt3_object *p = gt3_object_get(vm, mat->link[pass]);
  if (!p) return 0;
  *programm = p->m[GT3_PASSM_PROGRAMM];
  *aspect = gt3_bits2f(p->m[0]);
  *size = gt3_bits2f(p->m[1]);
  *enlarge = gt3_bits2f(p->m[7]);
  return 1;
}

/* GenMesh::Paint: the recorded commands with the animation labels' members, then every pass of every material slot */
static void gt3_mesh_paint(gt3_vm *vm, gt3_render *r, int32_t mesh_handle)
{
  gt3_object *o = gt3_obj(vm, mesh_handle);
  gt3_mesh *m = (o && o->cls == GT3_CID_MESH) ? (gt3_mesh *)o->data : NULL;
  if (!m) return;
  if (!m->prepared && !gt3_mesh_prepare(m, gt3_pass_info, vm)) return;
  if (m->store_mode) {
    int32_t members[GT3_MESH_MAXLABEL * 13];
    const int32_t n = m->n_anim < GT3_MESH_MAXLABEL ? m->n_anim : GT3_MESH_MAXLABEL;
    memset(members, 0, sizeof members);
    for (int32_t i = 0; i < n; i++) {
      const gt3_object *a = gt3_obj(vm, o->link[GT3_MESH_ANIM + i]);
      if (a) memcpy(members + i * 13, a->m, 13 * sizeof(int32_t));
    }
    gt3_mesh_replay(m, members);
  }
  for (int32_t mati = 1; mati < m->n_mtrl && mati < m->n_pass_alloc; mati++)
    for (int pasi = 0; pasi < GT3_MAT_MAXPASS; pasi++) {
      int32_t programm;
      float size, aspect, enlarge;
      if (gt3_pass_info(vm, m->mtrl[mati], pasi, &programm, &size, &aspect, &enlarge)) {
        gt3_enable_lights(r, gt3_pass_lightmask(vm, m->mtrl[mati], pasi));
        gt3_mesh_draw_pass(r, m, mati, pasi, programm, size, aspect, enlarge, gt3_set_pass_cb, vm);
      }
    }
}

/* GenScene::PaintR: action 0 sets the lights, action 1 paints the meshes */
static void gt3_scene_paint_r(gt3_vm *vm, gt3_render *r, int32_t scene, const gt3_mat *mat, int action, int depth)
{
  gt3_object *s = gt3_obj(vm, scene);
  if (!s || s->cls != GT3_CID_SCENE || depth > 64) return;
  gt3_mat newmat = *mat, trans, mx;
  float srt[9];
  for (int i = 0; i < 9; i++) srt[i] = gt3_bits2f(s->m[i]);
  const int32_t multiply = s->m[9];
  for (int32_t j = 0; j < multiply; j++) {
    gt3_mat_srt(&trans, srt);
    gt3_mat_mul4(&mx, &trans, &newmat);
    newmat = mx;
    for (uint32_t i = 0; i < s->n_child; i++) gt3_scene_paint_r(vm, r, s->child[i], &mx, action, depth + 1);
    if (action == 0) {
      const gt3_object *lo = gt3_obj(vm, s->link[GT3_SCENE_LIGHT]);
      if (lo && lo->cls == GT3_CID_LIGHT && lo->data) gt3_light_set(r, (const gt3_light_def *)lo->data, &mx);
    } else {
      gt3_set_matrix(r, &mx);
      if (s->link[GT3_SCENE_MESH]) gt3_mesh_paint(vm, r, s->link[GT3_SCENE_MESH]);
    }
  }
}

static void gt3_scene_paint(gt3_vm *vm, gt3_render *r, int32_t scene)
{
  gt3_mat mat;
  gt3_mat_init(&mat);
  gt3_clear_lights(r);
  gt3_scene_paint_r(vm, r, scene, &mat, 0, 0);
  gt3_scene_paint_r(vm, r, scene, &mat, 1, 0);
  gt3_clear_lights(r);
}

/* ---- scenes (genmaterial.cpp) ---- */

static int32_t gt3_scene_new(gt3_vm *vm, gt3_object **out)
{
  const int32_t h = gt3_make(vm, GT3_CID_SCENE, out);
  if (!*out) return 0;
  (*out)->m[0] = (*out)->m[1] = (*out)->m[2] = gt3_f2bits(1.0f); /* SRT = 1,1,1, 0,0,0, 0,0,0 */
  (*out)->m[9] = 1;                                              /* Multiply */
  return h;
}

static void gt3_scene_srt(gt3_object *s, const int32_t *ints)
{
  for (int i = 0; i < 9; i++) s->m[i] = ints[i];
}

static int32_t gt3_n_scene_mesh(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *s;
  const int32_t h = gt3_scene_new(vm, &s);
  if (!s) return 0;
  gt3_scene_srt(s, ints);
  s->link[GT3_SCENE_MESH] = objs[0];
  { /* Scene_Mesh: mesh->Prepare() */
    gt3_object *mo = gt3_obj(vm, objs[0]);
    if (gt3_render_of(vm) && mo && mo->cls == GT3_CID_MESH && mo->data && !gt3_mesh_prepare((gt3_mesh *)mo->data, gt3_pass_info, vm)) {
      vm->error = 1;
      vm->error_message = "Scene: a mesh pass needs more batches than the original has";
    }
  }
  return h;
}

static int32_t gt3_n_scene_trans(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *s;
  const int32_t h = gt3_scene_new(vm, &s);
  if (!s) return 0;
  gt3_scene_srt(s, ints);
  gt3_object_add_child(s, objs[0]);
  return h;
}

static int32_t gt3_n_scene_multiply(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *s;
  const int32_t h = gt3_scene_new(vm, &s);
  if (!s) return 0;
  gt3_scene_srt(s, ints);
  gt3_object_add_child(s, objs[0]);
  s->m[9] = ints[9] >> 16;
  return h;
}

static int32_t gt3_n_scene_add(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)ints;
  gt3_object *s1 = gt3_obj(vm, objs[0]);
  if (!s1 || !gt3_obj(vm, objs[1])) return 0;
  int32_t sb = objs[0];
  if (!(s1->n_child > 1)) {
    gt3_object *n;
    sb = gt3_scene_new(vm, &n);
    if (!n) return 0;
    gt3_object_add_child(n, objs[0]);
  }
  gt3_object_add_child(gt3_obj(vm, sb), objs[1]);
  return sb;
}

static int32_t gt3_n_scene_label(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *s = gt3_obj(vm, objs[0]);
  if (s) s->label = ints[0];
  return objs[0];
}

static int32_t gt3_n_scene_light(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)objs;
  gt3_object *s, *l;
  const int32_t h = gt3_scene_new(vm, &s);
  if (!s) return 0;
  const int32_t lh = gt3_make(vm, GT3_CID_LIGHT, &l);
  s = gt3_obj(vm, h);
  if (!l) return 0;
  gt3_scene_srt(s, ints);
  s->m[9] = 1;
  s->link[GT3_SCENE_LIGHT] = lh;
  l->label = ints[11];
  gt3_light_def *def = (gt3_light_def *)calloc(1, sizeof *def);
  if (!def) return 0;
  l->data = def;
  def->mode = gt3_sar(ints[9], 16) + 1; /* bit 3 is the camera space flag */
  def->mask = 1u << (gt3_sar(ints[10], 16) & 31);
  def->range = gt3_bits2f(ints[12]);
  def->rangecut = gt3_bits2f(ints[13]);
  def->amplify = gt3_bits2f(ints[14]);
  def->falloff = gt3_bits2f(ints[15]);
  def->inner = gt3_bits2f(ints[16]) * 1.57079637f; /* sPIF / 2 */
  def->outer = gt3_bits2f(ints[17]) * 1.57079637f;
  for (int i = 0; i < 4; i++) {
    def->diffuse[i] = ints[18 + i];
    def->ambient[i] = ints[22 + i];
    def->specular[i] = ints[26 + i];
  }
  return h;
}

/* ---- materials (genmaterial.cpp, the intro's PopPass) ---- */

static int32_t gt3_n_material_new(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)objs;
  (void)ints;
  gt3_object *o;
  return gt3_make(vm, GT3_CID_MATERIAL, &o);
}

/* the pass `pass >> 16` of material `obj`, made on demand */
static gt3_object *gt3_pop_pass(gt3_vm *vm, int32_t obj, int32_t pass)
{
  gt3_object *mat = gt3_obj(vm, obj);
  const int p = pass >> 16;
  if (!mat || mat->cls != GT3_CID_MATERIAL || p < 0 || p >= GT3_MAT_MAXPASS) return NULL;
  if (!mat->link[p]) {
    gt3_object *np;
    const int32_t h = gt3_make(vm, GT3_CID_MATPASS, &np);
    mat = gt3_obj(vm, obj);
    if (!np) return NULL;
    np->m[GT3_PASSM_PROGRAMM] = GT3_MPP_STATIC; /* GenMatPass: Programm = MPP_STATIC */
    gt3_passdata *d = (gt3_passdata *)calloc(1, sizeof *d);
    if (!d) return NULL;
    d->lightmask = 0xffff;
    gt3_matlight_default(d->matlight);
    np->data = d;
    mat->link[p] = h;
  }
  return gt3_obj(vm, mat->link[p]);
}

/* the state stream of a pass has room for 256 words: a native adds at most 100 */
static int gt3_pass_room(gt3_vm *vm, const gt3_object *p)
{
  if (gt3_passd(p)->n_state + 100 <= 256) return 1;
  vm->error = 1;
  vm->error_message = "a material pass has more state than the original's buffer holds";
  return 0;
}

/* GenMatPass members: Aspect, Size, RenderPass, Color(4), Enlarge */
static int32_t gt3_n_mat_base(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *p = gt3_pop_pass(vm, objs[0], ints[0]);
  if (p && gt3_pass_room(vm, p)) {
    gt3_passdata *d = gt3_passd(p);
    const uint32_t flags = (uint32_t)gt3_sar(ints[1], 16);
    uint32_t *ptr = d->states + d->n_state;
    for (int i = 0; i < 4; i++) p->m[3 + i] = ints[4 + i];
    if (flags & GT3_MBF_COLOR) d->flags |= 1;
    gt3_state_base(&ptr, flags, gt3_sar(ints[2], 16), gt3_color32(ints + 4));
    d->n_state = (int32_t)(ptr - d->states);
  }
  return objs[0];
}

static int32_t gt3_n_mat_texture(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *p = gt3_pop_pass(vm, objs[0], ints[0]);
  const int stage = ints[1] >> 16;
  if (p && stage >= 0 && stage < GT3_PASS_MAXTEX && gt3_pass_room(vm, p)) {
    gt3_passdata *d = gt3_passd(p);
    uint32_t flags = (uint32_t)gt3_sar(ints[2], 16), *ptr = d->states + d->n_state;
    const uint32_t uv = (flags & GT3_MTF_UVMASK) >> 8;
    p->link[GT3_PASS_TEXTURE + stage] = objs[1];
    d->transmode[stage] = 0;
    if (uv > 0x0a) { /* the texture matrix modes: 0x0b (view), 0x0c (model view) */
      flags = (flags & ~(uint32_t)GT3_MTF_UVMASK) | GT3_MTF_UVPOS;
      d->transmode[stage] = (int32_t)uv - 0x0a;
    }
    gt3_state_tex(&ptr, stage, flags);
    d->n_state = (int32_t)(ptr - d->states);
  }
  /* a bitmap that is not a texture yet becomes one when a material first uses it */
  gt3_gen *g = gt3_gen_of(vm);
  gt3_bitmap *bm = gt3_bitmap_of(vm, objs[1]);
  if (g && bm && bm->texture == -1) gt3_make_texture(g, bm);
  return objs[0];
}

static int32_t gt3_n_mat_finalizer(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *p = gt3_pop_pass(vm, objs[0], ints[0]);
  if (p) {
    p->m[1] = ints[3]; /* Size */
    p->m[0] = ints[2]; /* Aspect */
    p->m[7] = ints[4]; /* Enlarge */
    p->m[GT3_PASSM_PROGRAMM] = gt3_sar(ints[1], 16);
  }
  return objs[0];
}

static int32_t gt3_n_mat_label(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *p = gt3_pop_pass(vm, objs[0], ints[1]);
  if (p) p->label = ints[0];
  return objs[0];
}

static int32_t gt3_n_mat_textrans(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *p = gt3_pop_pass(vm, objs[0], ints[1]);
  const int stage = ints[2] >> 16;
  if (p && stage >= 0 && stage < GT3_PASS_MAXTEX) {
    gt3_object *mx;
    const int32_t mh = gt3_make(vm, GT3_CID_MATRIX, &mx);
    p = gt3_pop_pass(vm, objs[0], ints[1]);
    if (!mx || !p) return objs[0];
    for (int i = 0; i < 9; i++) mx->m[i] = ints[3 + i];
    mx->label = ints[0];
    p->link[GT3_PASS_TEXTRANS + stage] = mh;
  }
  return objs[0];
}

static int32_t gt3_n_mat_light(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *p = gt3_pop_pass(vm, objs[0], ints[0]);
  if (p && gt3_pass_room(vm, p)) {
    gt3_passdata *d = gt3_passd(p);
    uint32_t *ptr = d->states + d->n_state;
    *ptr++ = GT3_D3DRS_DIFFUSEMATERIALSOURCE;
    *ptr++ = (uint32_t)gt3_sar(ints[1], 16);
    *ptr++ = GT3_D3DRS_AMBIENTMATERIALSOURCE;
    *ptr++ = (uint32_t)gt3_sar(ints[3], 16);
    *ptr++ = GT3_D3DRS_SPECULARMATERIALSOURCE;
    *ptr++ = (uint32_t)gt3_sar(ints[2], 16);
    *ptr++ = GT3_D3DRS_EMISSIVEMATERIALSOURCE;
    *ptr++ = (uint32_t)gt3_sar(ints[4], 16);
    *ptr++ = GT3_D3DRS_SPECULARENABLE;
    *ptr++ = (uint32_t)(gt3_sar(ints[22], 16) & 1);
    d->n_state = (int32_t)(ptr - d->states);
    for (int i = 0; i < 4; i++) {
      d->matlight[0 + i] = gt3_i2f(ints[5 + i]);   /* diffuse */
      d->matlight[4 + i] = gt3_i2f(ints[13 + i]);  /* ambient */
      d->matlight[8 + i] = gt3_i2f(ints[9 + i]);   /* specular */
      d->matlight[12 + i] = gt3_i2f(ints[17 + i]); /* emissive */
    }
    d->matlight[16] = gt3_bits2f(ints[21]);
    d->lightmask = (uint32_t)ints[23];
  }
  return objs[0];
}

/* ---- effect chains (genfx.cpp). Members: Color(4) Zoom(2) Rotate Center(2) CamPos(3) CamRot(3) ---- */

enum { GT3_FXM_COLOR = 0, GT3_FXM_ZOOM = 4, GT3_FXM_ROTATE = 6, GT3_FXM_CENTER = 7, GT3_FXM_CAMPOS = 9, GT3_FXM_CAMROT = 12 };

/* the original's RenderHandler of a chain is kept in member 15, out of the script's reach (it reaches 0..14) */
enum {
  GT3_RH_NOP = 0,
  GT3_RH_BEGINVIEW = 1,
  GT3_RH_ENDVIEW = 2,
  GT3_RH_LAYER2D = 3,
  GT3_RH_MERGECOLOR = 4,
  GT3_RH_PAINT = 5,
  GT3_RH_BLEND4X = 6,
  GT3_FXM_HANDLER = 15
};

/* what a GenFXChain holds besides the members: Param[10], ParamF[16], the FXMaster viewport it renders to (-1: none) and the id of the last render */
typedef struct gt3_fxdata {
  int32_t param[10];
  float paramf[16];
  int32_t viewport, render_id;
} gt3_fxdata;

static gt3_fxdata *gt3_fxd(const gt3_object *o) { return (gt3_fxdata *)o->data; }

/* the effect chain behind a handle, NULL when it is none (the original's SCRIPTVERIFY) */
static gt3_object *gt3_fxobj(gt3_vm *vm, int32_t h)
{
  gt3_object *o = gt3_obj(vm, h);
  return (o && o->cls == GT3_CID_FXCHAIN && o->data) ? o : NULL;
}

static int32_t gt3_fx_new(gt3_vm *vm, gt3_object **out, int handler)
{
  const int32_t h = gt3_make(vm, GT3_CID_FXCHAIN, out);
  if (!*out) return 0;
  gt3_fxdata *d = (gt3_fxdata *)calloc(1, sizeof *d);
  if (!d) {
    *out = NULL;
    return 0;
  }
  d->viewport = -1;
  (*out)->data = d;
  (*out)->m[GT3_FXM_HANDLER] = handler;
  return h;
}

/* SCRIPTVERIFY(chain->Viewport != -1) after FXMaster->Alloc */
static int gt3_fx_alloc(gt3_vm *vm, gt3_object *c, int32_t ocount, int32_t size)
{
  gt3_render *r = gt3_render_of(vm);
  gt3_fxd(c)->viewport = r ? gt3_fxm_alloc(r, gt3_sar(ocount, 16), gt3_sar(size, 16)) : 0;
  if (gt3_fxd(c)->viewport != -1) return 1;
  vm->error = 1;
  vm->error_message = "out of fxchain buffers";
  return 0;
}

/* FXMaster->Ref(viewport, abs(ocount >> 16) - 1) */
static void gt3_fx_ref(gt3_vm *vm, const gt3_object *c, int32_t ocount)
{
  gt3_render *r = gt3_render_of(vm);
  const int32_t n = gt3_sar(ocount, 16);
  if (r) gt3_fxm_ref(r, gt3_fxd(c)->viewport, (n < 0 ? -n : n) - 1);
}

static void gt3_fx_unref(gt3_vm *vm, const gt3_object *input)
{
  gt3_render *r = gt3_render_of(vm);
  if (r) gt3_fxm_ref(r, gt3_fxd(input)->viewport, -1);
}

static int32_t gt3_n_fx_new(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)objs;
  gt3_object *c;
  const int32_t h = gt3_fx_new(vm, &c, GT3_RH_NOP);
  if (!c) return 0;
  gt3_fx_alloc(vm, c, ints[1], ints[0]);
  for (int i = 0; i < 4; i++) c->m[GT3_FXM_COLOR + i] = ints[2 + i];
  return h;
}

/* FXChain_BeginViewport(chn, pos(3), parm2(3), aspect, mode, ocount, color(4), zoom): `ints` start at pos */
static int32_t gt3_fx_begin(gt3_vm *vm, int32_t chn, const int32_t *ints, int32_t ocount)
{
  gt3_object *c;
  const int32_t h = gt3_fx_new(vm, &c, GT3_RH_BEGINVIEW);
  gt3_object *in = gt3_fxobj(vm, chn);
  if (!c || !in) return 0;
  c->link[GT3_FX_INPUT + 0] = chn;
  gt3_fxd(c)->viewport = gt3_fxd(in)->viewport;
  gt3_fxd(c)->param[0] = gt3_sar(ints[7], 16);
  /* the original assigns the two ints to float members: (float)int, not the bits (the 16.16 scale is divided out when the camera is made) */
  c->m[GT3_FXM_ZOOM + 1] = gt3_f2bits((float)ints[6]);                                 /* Zoom.y = aspect */
  c->m[GT3_FXM_ZOOM + 0] = gt3_f2bits((float)ints[13]);                                /* Zoom.x = zoom */
  for (int i = 0; i < 4; i++) c->m[GT3_FXM_COLOR + i] = ints[9 + i];
  for (int i = 0; i < 6; i++) c->m[GT3_FXM_CAMPOS + i] = gt3_f2bits(gt3_i2f(ints[i])); /* CamPos(3), then CamRot(3) from parm2 */
  gt3_fx_ref(vm, c, ocount);
  return h;
}

static int32_t gt3_fx_paint(gt3_vm *vm, int32_t chn, int32_t scene, int32_t ocount)
{
  gt3_object *c;
  const int32_t h = gt3_fx_new(vm, &c, GT3_RH_PAINT);
  gt3_object *in = gt3_fxobj(vm, chn);
  if (!c || !in || !gt3_obj(vm, scene)) return 0;
  c->link[GT3_FX_OBJECT] = scene;
  c->link[GT3_FX_INPUT + 0] = chn;
  gt3_fxd(c)->viewport = gt3_fxd(in)->viewport;
  gt3_fx_ref(vm, c, ocount);
  return h;
}

static int32_t gt3_fx_end(gt3_vm *vm, int32_t chn, int32_t ocount)
{
  gt3_object *c;
  const int32_t h = gt3_fx_new(vm, &c, GT3_RH_ENDVIEW);
  gt3_object *in = gt3_fxobj(vm, chn);
  if (!c || !in) return 0;
  c->link[GT3_FX_INPUT + 0] = chn;
  gt3_fxd(c)->viewport = gt3_fxd(in)->viewport;
  gt3_fx_ref(vm, c, ocount);
  return h;
}

static int32_t gt3_n_fx_beginviewport(gt3_vm *vm, const int32_t *objs, const int32_t *ints) { return gt3_fx_begin(vm, objs[0], ints, ints[8]); }
static int32_t gt3_n_fx_paintscene(gt3_vm *vm, const int32_t *objs, const int32_t *ints) { return gt3_fx_paint(vm, objs[0], objs[1], ints[0]); }
static int32_t gt3_n_fx_endviewport(gt3_vm *vm, const int32_t *objs, const int32_t *ints) { return gt3_fx_end(vm, objs[0], ints[0]); }

static int32_t gt3_n_fx_viewport(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  const int32_t b = gt3_fx_begin(vm, objs[0], ints, 0x10000);
  if (!b) return 0;
  const int32_t p = gt3_fx_paint(vm, b, objs[1], 0x10000);
  if (!p) return 0;
  return gt3_fx_end(vm, p, ints[8]);
}

static int32_t gt3_n_fx_layer2d(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *c;
  const int32_t h = gt3_fx_new(vm, &c, GT3_RH_LAYER2D);
  gt3_object *in = gt3_fxobj(vm, objs[0]);
  if (!c || !in || !gt3_obj(vm, objs[1])) return 0;
  c->link[GT3_FX_OBJECT] = objs[1];
  c->link[GT3_FX_INPUT + 0] = objs[0];
  for (int i = 0; i < 4; i++) c->m[GT3_FXM_COLOR + i] = ints[i];
  c->m[GT3_FXM_ZOOM + 0] = ints[15];
  c->m[GT3_FXM_ZOOM + 1] = ints[16];
  c->m[GT3_FXM_ROTATE] = ints[17];
  c->m[GT3_FXM_CENTER + 0] = ints[18];
  c->m[GT3_FXM_CENTER + 1] = ints[19];
  for (int i = 0; i < 10; i++) gt3_fxd(c)->param[i] = ints[4 + i];
  gt3_fxd(c)->viewport = gt3_fxd(in)->viewport;
  gt3_fx_ref(vm, c, ints[14]);
  return h;
}

static int32_t gt3_n_fx_merge(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *c;
  const int32_t h = gt3_fx_new(vm, &c, GT3_RH_MERGECOLOR);
  if (!c || !gt3_fxobj(vm, objs[0]) || !gt3_fxobj(vm, objs[1])) return 0;
  c->link[GT3_FX_INPUT + 0] = objs[0];
  c->link[GT3_FX_INPUT + 1] = objs[1];
  /* sRange<sU32>(param >> 8, 255, 0): a negative amount is a large unsigned number, and 255 */
  const uint32_t amount = (uint32_t)gt3_sar(ints[1], 8);
  gt3_fxd(c)->param[0] = gt3_sar(ints[0], 16) + 3;
  gt3_fxd(c)->param[1] = (int32_t)(((amount >= 255u ? 255u : amount) << 24) | 0xffffffu);
  c->m[GT3_FXM_ZOOM + 0] = ints[2];
  c->m[GT3_FXM_ZOOM + 1] = ints[3];
  c->m[GT3_FXM_ROTATE] = ints[4];
  c->m[GT3_FXM_CENTER + 0] = ints[5];
  c->m[GT3_FXM_CENTER + 1] = ints[6];
  if (!gt3_fx_alloc(vm, c, ints[8], ints[7])) return h;
  gt3_fx_unref(vm, gt3_obj(vm, objs[0]));
  gt3_fx_unref(vm, gt3_obj(vm, objs[1]));
  return h;
}

static int32_t gt3_n_fx_color(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *c;
  const int32_t h = gt3_fx_new(vm, &c, GT3_RH_MERGECOLOR);
  if (!c || !gt3_fxobj(vm, objs[0])) return 0;
  c->link[GT3_FX_INPUT + 0] = objs[0];
  gt3_fxd(c)->param[0] = gt3_sar(ints[0], 16) + 8; /* sMBM_ADDCOL */
  gt3_fxd(c)->param[1] = (int32_t)gt3_color32(ints + 1);
  {
    const int32_t lighten = gt3_sar(ints[12] + 65536, 10);
    gt3_fxd(c)->param[2] = lighten >= 255 ? 255 : lighten <= 0 ? 0 : lighten;
  }
  c->m[GT3_FXM_ZOOM + 0] = ints[7];
  c->m[GT3_FXM_ZOOM + 1] = ints[8];
  c->m[GT3_FXM_ROTATE] = ints[9];
  c->m[GT3_FXM_CENTER + 0] = ints[10];
  c->m[GT3_FXM_CENTER + 1] = ints[11];
  if (!gt3_fx_alloc(vm, c, ints[6], ints[5])) return h;
  gt3_fx_unref(vm, gt3_obj(vm, objs[0]));
  return h;
}

static int32_t gt3_n_fx_blend4x(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *c;
  const int32_t h = gt3_fx_new(vm, &c, GT3_RH_BLEND4X);
  if (!c || !gt3_fxobj(vm, objs[0])) return 0;
  c->link[GT3_FX_INPUT + 0] = objs[0];
  if (!gt3_fx_alloc(vm, c, ints[18], ints[17])) return h;
  gt3_fx_unref(vm, gt3_obj(vm, objs[0]));
  for (int i = 0; i < 16; i++) gt3_fxd(c)->paramf[i] = gt3_bits2f(ints[i]);
  gt3_fxd(c)->param[0] = gt3_sar(ints[16], 16);
  {
    const int32_t amplify = gt3_sar(ints[19], 10);
    gt3_fxd(c)->param[1] = (int32_t)((uint32_t)(amplify >= 255 ? 255 : amplify <= 0 ? 0 : amplify) * 0x01010101u);
  }
  return h;
}

/* FXChain_Label: labels the chain, or the begin chain of a begin / paint / end triple, and returns the chain it was given */
static int32_t gt3_n_fx_label(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  gt3_object *chain = gt3_obj(vm, objs[0]);
  if (!chain) return 0;
  gt3_object *target = chain;
  gt3_object *in0 = gt3_obj(vm, chain->link[GT3_FX_INPUT + 0]);
  if (chain->m[GT3_FXM_HANDLER] == GT3_RH_ENDVIEW && in0 && in0->m[GT3_FXM_HANDLER] == GT3_RH_PAINT && gt3_obj(vm, in0->link[GT3_FX_INPUT + 0]))
    target = gt3_obj(vm, in0->link[GT3_FX_INPUT + 0]);
  target->label = ints[0];
  return objs[0];
}

static int32_t gt3_n_nothing(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)vm;
  (void)objs;
  (void)ints;
  return 0;
}

/* ---- the render handlers of the chains (genfx.cpp RH_*): the geometry and the camera; the device states (blend modes, textures, lights) come with the backend ---- */

static void gt3_fx_zoom_center(const gt3_object *c, float zoom[2], float center[2])
{
  zoom[0] = gt3_bits2f(c->m[GT3_FXM_ZOOM + 0]);
  zoom[1] = gt3_bits2f(c->m[GT3_FXM_ZOOM + 1]);
  center[0] = gt3_bits2f(c->m[GT3_FXM_CENTER + 0]);
  center[1] = gt3_bits2f(c->m[GT3_FXM_CENTER + 1]);
}

static void gt3_fx_put(float *vp, float x, float y, float z, uint32_t color, float u0, float v0, float u1, float v1)
{
  vp[0] = x;
  vp[1] = y;
  vp[2] = z;
  memcpy(&vp[3], &color, sizeof color);
  vp[4] = u0;
  vp[5] = v0;
  vp[6] = u1;
  vp[7] = v1;
}

static void gt3_fx_handler(gt3_vm *vm, gt3_render *r, gt3_object *c, gt3_fxview *view)
{
  gt3_fxdata *d = gt3_fxd(c);
  gt3_geodev *dev = &r->dev;
  float zoom[2], center[2], buf[2], *vp;
  gt3_fx_zoom_center(c, zoom, center);
  const int32_t rotate = c->m[GT3_FXM_ROTATE];
  const uint32_t color = gt3_color32(c->m + GT3_FXM_COLOR);

  switch (c->m[GT3_FXM_HANDLER]) {
  case GT3_RH_NOP:
    view->clear_flags = 3;
    view->clear_color = color;
    gt3_begin_viewport(r, view);
    gt3_end_viewport(r);
    break;

  case GT3_RH_BEGINVIEW: {
    gt3_cam cam;
    float pos[3], rot[3];
    for (int i = 0; i < 3; i++) {
      pos[i] = gt3_bits2f(c->m[GT3_FXM_CAMPOS + i]);
      rot[i] = gt3_bits2f(c->m[GT3_FXM_CAMROT + i]);
    }
    gt3_trace_camin(r, pos, rot, d->param[0]);
    gt3_cam_from_chain(&cam, pos, rot, d->param[0], zoom[0], zoom[1]);
    view->clear_flags = (uint32_t)d->param[0] & 3u;
    view->clear_color = color;
    gt3_begin_viewport(r, view);
    gt3_set_camera(r, &cam);
    break;
  }

  case GT3_RH_ENDVIEW: gt3_end_viewport(r); break;

  case GT3_RH_LAYER2D: {
    gt3_mat mat;
    view->clear_flags = (uint32_t)(gt3_sar(d->param[9], 16) & 3);
    gt3_begin_viewport(r, view);
    gt3_mat_init(&mat);
    mat.i.x = (float)r->dview_w;
    mat.j.y = (float)r->dview_h;
    gt3_set_camera_ortho(r);
    gt3_set_matrix(r, &mat);
    gt3_set_pass_cb(vm, c->link[GT3_FX_OBJECT], 0);
    if (d->param[9] & 0x20000) gt3_set_state(r, GT3_D3DRS_ZFUNC, GT3_D3DCMP_ALWAYS);
    const int32_t handle = gt3_geo_add(dev, GT3_FVF_DOUBLE, GT3_GEO_QUAD);
    gt3_geo_begin(dev, handle, 4, 0, &vp, NULL);
    for (int i = 0; i < 4; i++) {
      const int j = i ^ (i >> 1);
      gt3_rotate_from_chain(buf, gt3_i2f((j & 1) ? d->param[2] : d->param[0]), gt3_i2f((j & 2) ? d->param[3] : d->param[1]), zoom, rotate, center, 1);
      gt3_fx_put(vp, buf[0] - 0.5f / mat.i.x, buf[1] - 0.5f / mat.j.y, gt3_i2f(d->param[8]), color, gt3_i2f((j & 1) ? d->param[6] : d->param[4]),
                 gt3_i2f((j & 2) ? d->param[7] : d->param[5]), 0.0f, 0.0f);
      vp += 8;
    }
    gt3_geo_end(dev, handle, -1, 0);
    gt3_geo_draw(dev, handle);
    gt3_geo_rem(dev, handle);
    gt3_end_viewport(r);
    break;
  }

  case GT3_RH_MERGECOLOR: {
    const gt3_object *i0 = gt3_obj(vm, c->link[GT3_FX_INPUT + 0]), *i1 = gt3_obj(vm, c->link[GT3_FX_INPUT + 1]);
    if (!i0 || i0->cls != GT3_CID_FXCHAIN || gt3_fxd(i0)->viewport <= 0) break;
    const gt3_fxview *src1 = &r->view[gt3_fxd(i0)->viewport], *src2 = NULL;
    if (i1 && i1->cls == GT3_CID_FXCHAIN && gt3_fxd(i1)->viewport >= 0) src2 = &r->view[gt3_fxd(i1)->viewport];
    uint32_t col = (uint32_t)d->param[1], states[256], *p = states;
    view->clear_flags = GT3_VCF_COLOR;
    gt3_begin_viewport(r, view);
    gt3_state_base(&p, 0, d->param[0], 0);
    gt3_state_tex(&p, 0, GT3_MTF_CLAMP | GT3_MTF_FILTER);
    gt3_state_tex(&p, 1, GT3_MTF_UV1 | GT3_MTF_FILTER | GT3_MTF_CLAMP);
    if (!src2) {
      *p++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
      *p++ = GT3_D3DTOP_MODULATE4X;
      *p++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG1;
      *p++ = GT3_D3DTA_CURRENT;
      *p++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG2;
      *p++ = GT3_D3DTA_DIFFUSE | GT3_D3DTA_ALPHAREPLICATE;
      *p++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
      *p++ = GT3_D3DTOP_SELECTARG1;
      *p++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAARG1;
      *p++ = GT3_D3DTA_CURRENT;
      *p++ = GT3_D3DTSS_2 | GT3_D3DTSS_COLOROP;
      *p++ = GT3_D3DTOP_DISABLE;
      col = (col & 0xffffffu) | ((uint32_t)d->param[2] << 24);
    }
    gt3_state_end(&p);
    gt3_set_camera_ortho(r);
    {
      gt3_mat mat;
      gt3_mat_init(&mat);
      gt3_set_matrix(r, &mat);
    }
    gt3_set_states(r, states);
    gt3_set_texture(r, 0, src1->render_target, NULL);
    if (src2)
      gt3_set_texture(r, 1, src2->render_target, NULL);
    else
      src2 = src1;
    const int32_t handle = gt3_geo_add(dev, GT3_FVF_DOUBLE, GT3_GEO_QUAD);
    gt3_geo_begin(dev, handle, 4, 0, &vp, NULL);
    for (int i = 0; i < 4; i++) {
      const int j = i ^ (i >> 1);
      gt3_rotate_from_chain(buf, (j & 1) ? 1.0f : 0.0f, (j & 2) ? 1.0f : 0.0f, zoom, rotate, center, 0);
      gt3_fx_put(vp, (float)((j & 1) ? view->x1 - view->x0 : 0) - 0.5f, (float)((j & 2) ? view->y1 - view->y0 : 0) - 0.5f, 1.0f, col, buf[0] * src1->xscale,
                 buf[1] * src1->yscale, (j & 1) ? src2->xscale : 0.0f, (j & 2) ? src2->yscale : 0.0f);
      vp += 8;
    }
    gt3_geo_end(dev, handle, -1, 0);
    gt3_geo_draw(dev, handle);
    gt3_geo_rem(dev, handle);
    gt3_end_viewport(r);
    gt3_set_texture(r, 0, 0, NULL);
    gt3_set_texture(r, 1, 0, NULL);
    break;
  }

  case GT3_RH_PAINT: gt3_scene_paint(vm, r, c->link[GT3_FX_OBJECT]); break;

  case GT3_RH_BLEND4X: {
    const gt3_object *i0 = gt3_obj(vm, c->link[GT3_FX_INPUT + 0]);
    if (!i0 || i0->cls != GT3_CID_FXCHAIN || gt3_fxd(i0)->viewport < 0) break;
    const gt3_fxview *src = &r->view[gt3_fxd(i0)->viewport];
    uint32_t states[256], *p = states;
    gt3_begin_viewport(r, view);
    gt3_state_tex(&p, 0, GT3_MTF_CLAMP | GT3_MTF_FILTER);
    gt3_state_tex(&p, 1, GT3_MTF_UV1 | GT3_MTF_CLAMP | GT3_MTF_FILTER);
    gt3_set_camera_ortho(r);
    {
      gt3_mat mat;
      gt3_mat_init(&mat);
      gt3_set_matrix(r, &mat);
    }
    gt3_set_texture(r, 0, src->render_target, NULL);
    gt3_set_texture(r, 1, src->render_target, NULL);
    float ox = 0.0f, oy = 0.0f, sx = src->xscale, sy = src->yscale;
    if (d->param[0]) { /* coordinate mode not 0 */
      ox = (float)(src->x1 - src->x0);
      oy = (float)(src->y1 - src->y0);
      sx /= ox;
      sy /= oy;
    }
    const float *ip = d->paramf;
    const int32_t handle = gt3_geo_add(dev, GT3_FVF_DOUBLE, GT3_GEO_QUAD);
    for (int i = 0; i < 2; i++) {
      gt3_state_base(&p, GT3_MBF_ZOFF | (i ? GT3_MBF_BLENDADD : 0), GT3_MBM_TEX, 0);
      p[0] = GT3_D3DTSS_ALPHAOP | GT3_D3DTSS_0;
      p[1] = GT3_D3DTOP_SELECTARG2;
      p[2] = GT3_D3DTSS_COLOROP | GT3_D3DTSS_1;
      p[3] = GT3_D3DTOP_MODULATEALPHA_ADDCOLOR;
      p[4] = GT3_D3DTSS_COLORARG1 | GT3_D3DTSS_1;
      p[5] = GT3_D3DTA_CURRENT;
      p[6] = GT3_D3DTSS_COLORARG2 | GT3_D3DTSS_1;
      p[7] = GT3_D3DTA_TEXTURE;
      p += 8;
      gt3_state_end(&p);
      gt3_set_states(r, states);
      gt3_geo_begin(dev, handle, 4, 0, &vp, NULL);
      for (int j = 0; j < 4; j++) {
        const int k = j ^ (j >> 1);
        gt3_fx_put(vp, (float)((k & 1) ? view->x1 - view->x0 : 0) - 0.5f, (float)((k & 2) ? view->y1 - view->y0 : 0) - 0.5f, 1.0f, (uint32_t)d->param[1],
                   sx * ((k & 1) ? ip[2] + ox : ip[0]), sy * ((k & 2) ? ip[3] + oy : ip[1]), sx * ((k & 1) ? ip[6] + ox : ip[4]),
                   sy * ((k & 2) ? ip[7] + oy : ip[5]));
        vp += 8;
      }
      ip += 8;
      gt3_geo_end(dev, handle, -1, 0);
      gt3_geo_draw(dev, handle);
      p = states;
    }
    gt3_geo_rem(dev, handle);
    gt3_end_viewport(r);
    gt3_set_texture(r, 0, 0, NULL);
    gt3_set_texture(r, 1, 0, NULL);
    break;
  }

  default: break;
  }
}

/* GenFXChain::RenderR: the inputs first, then this chain's handler, once per render */
static void gt3_fx_render_r(gt3_vm *vm, gt3_render *r, int32_t chain, int32_t id, int depth)
{
  gt3_object *c = gt3_obj(vm, chain);
  if (!c || c->cls != GT3_CID_FXCHAIN || !c->data || depth > 256) return;
  if (gt3_fxd(c)->render_id == id) return;
  for (int i = 0; i < GT3_FX_MAXIN; i++)
    if (c->link[GT3_FX_INPUT + i]) gt3_fx_render_r(vm, r, c->link[GT3_FX_INPUT + i], id, depth + 1);
  if (gt3_fxd(c)->viewport != -1) gt3_fx_handler(vm, r, c, &r->view[gt3_fxd(c)->viewport]);
  gt3_fxd(c)->render_id = id;
}

static int32_t gt3_n_render_chain(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)ints;
  gt3_render *r = gt3_render_of(vm);
  const gt3_object *c = gt3_obj(vm, objs[0]);
  if (!c || c->cls != GT3_CID_FXCHAIN) {
    vm->error = 1;
    vm->error_message = "RenderChain: not an effect chain";
    return 0;
  }
  if (r) gt3_fx_render_r(vm, r, objs[0], ++r->render_counter, 0);
  return 0;
}

static int32_t gt3_n_fx_reset_alloc(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)objs;
  (void)ints;
  gt3_render *r = gt3_render_of(vm);
  if (r) gt3_fxm_reset(r);
  return 0;
}

/* a word of the table that the script never reaches: it stops the machine, so that a script that does is found out */
static int32_t gt3_n_unused(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)objs;
  (void)ints;
  vm->error = 1;
  vm->error_message = "a native that the port does not implement";
  return 0;
}

GT3_NATIVES_API void gt3_register_natives(gt3_vm *vm)
{
  static const struct {
    int id, ints, objs, ret;
    gt3_native_fn fn;
    const char *name;
  } t[] = {
    /* meshes */
    { 0x01, 4, 1, 2, gt3_n_mesh_cube, "Cube" },
    { 0x03, 9, 1, 2, gt3_n_mesh_selectcube, "SelectCube" },
    { 0x04, 3, 1, 2, gt3_n_mesh_subdivide, "Subdivide" },
    { 0x06, 10, 1, 2, gt3_n_mesh_transform, "Transform" },
    { 0x0a, 2, 0, 2, gt3_n_mesh_new, "NewMesh" },
    { 0x0b, 2, 2, 2, gt3_n_mesh_material, "MeshMaterial" },
    { 0x10, 3, 1, 2, gt3_n_mesh_calcnormals, "CalcNormals" },
    { 0x13, 3, 1, 2, gt3_n_mesh_triangulate, "Triangulate" },
    { 0x18, 13, 1, 2, gt3_n_mesh_perlin, "Perlin3D" },
    { 0x1b, 6, 0, 2, gt3_n_mesh_babe, "Babe" },
    { 0x1c, 0, 1, 2, gt3_n_mesh_beginrecord, "BeginRecord" },
    { 0x1d, 2, 1, 2, gt3_n_mesh_animlabel, "AnimLabel" },
    { 0x1f, 4, 1, 2, gt3_n_mesh_selectrandom, "SelectRandom" },
    { 0x22, 11, 1, 2, gt3_n_mesh_extrudewk, "ExtrudeWK" },
    /* splines */
    { 0x41, 3, 0, 2, gt3_n_spline_new, "Spline" },
    { 0x42, 0, 1, 2, gt3_n_spline_sort, "SplineSort" },
    { 0x43, 2, 1, 2, gt3_n_spline_key1, "SplineKey1" },
    { 0x44, 3, 1, 2, gt3_n_spline_key2, "SplineKey2" },
    { 0x45, 4, 1, 2, gt3_n_spline_key3, "SplineKey3" },
    { 0x46, 5, 1, 2, gt3_n_spline_key4, "SplineKey4" },
    { 0x47, 1, 1, 0, gt3_n_spline_calc, "SplineCalc1" },
    { 0x48, 1, 1, 0, gt3_n_spline_calc, "SplineCalc2" },
    { 0x49, 1, 1, 0, gt3_n_spline_calc, "SplineCalc3" },
    { 0x4a, 1, 1, 0, gt3_n_spline_calc, "SplineCalc4" },
    /* system */
    { 0x55, 0, 1, 2, gt3_n_sys_copy, "Copy" },
    { 0x58, 1, 1, 2, gt3_n_sys_findlabel, "FindLabel" },
    { 0x59, 2, 0, 1, gt3_n_sys_pow, "Pow" },
    /* bitmaps */
    { 0x82, 0, 1, 2, gt3_n_bitmap_maketexture, "MakeTexture" },
    { 0x84, 4, 0, 2, gt3_n_bitmap_flat, "Flat" },
    { 0x85, 1, 2, 2, gt3_n_bitmap_merge, "Merge" },
    { 0x86, 5, 1, 2, gt3_n_bitmap_color, "Color" },
    { 0x87, 12, 1, 2, gt3_n_bitmap_glowrect, "GlowRect" },
    { 0x91, 9, 1, 2, gt3_n_bitmap_text, "Text" },
    { 0x92, 15, 0, 2, gt3_n_bitmap_perlin, "Perlin" },
    /* scenes */
    { 0xc1, 9, 1, 2, gt3_n_scene_mesh, "Scene" },
    { 0xc2, 9, 1, 2, gt3_n_scene_trans, "TransScene" },
    { 0xc3, 10, 1, 2, gt3_n_scene_multiply, "Multiply" },
    { 0xc4, 0, 2, 2, gt3_n_scene_add, "AddScene" },
    { 0xc5, 0, 1, 2, gt3_n_ret0, "PaintScene" },
    { 0xc6, 1, 1, 2, gt3_n_scene_label, "LabelScene" },
    { 0xc7, 30, 0, 2, gt3_n_scene_light, "LightScene" },
    /* materials (0xf1..0xf5 are the same words again in the original's table) */
    { 0xe0, 0, 0, 2, gt3_n_material_new, "NewMaterial" },
    { 0xe1, 8, 1, 2, gt3_n_mat_base, "MatBase" },
    { 0xe2, 3, 2, 2, gt3_n_mat_texture, "MatTexture" },
    { 0xe3, 5, 1, 2, gt3_n_mat_finalizer, "MatFinalizer" },
    { 0xe4, 2, 1, 2, gt3_n_mat_label, "MatLabel" },
    { 0xe5, 12, 1, 2, gt3_n_mat_textrans, "MatTexTrans" },
    { 0xe9, 24, 1, 2, gt3_n_mat_light, "MatLight" },
    { 0xf1, 8, 1, 2, gt3_n_mat_base, "MatBase" },
    { 0xf2, 3, 2, 2, gt3_n_mat_texture, "MatTexture" },
    { 0xf3, 5, 1, 2, gt3_n_mat_finalizer, "MatFinalizer" },
    { 0xf4, 2, 1, 2, gt3_n_mat_label, "MatLabel" },
    { 0xf5, 12, 1, 2, gt3_n_mat_textrans, "MatTexTrans" },
    /* effect chains */
    { 0x100, 6, 0, 2, gt3_n_fx_new, "NewFXChain" },
    { 0x101, 14, 1, 2, gt3_n_fx_beginviewport, "BeginViewport" },
    { 0x102, 0, 1, 0, gt3_n_render_chain, "RenderChain" },
    { 0x103, 20, 2, 2, gt3_n_fx_layer2d, "Layer2D" },
    { 0x104, 9, 2, 2, gt3_n_fx_merge, "FXMerge" },
    { 0x105, 13, 1, 2, gt3_n_fx_color, "FXColor" },
    { 0x106, 1, 1, 2, gt3_n_fx_endviewport, "EndViewport" },
    { 0x107, 1, 2, 2, gt3_n_fx_paintscene, "PaintScene" },
    { 0x109, 20, 1, 2, gt3_n_fx_blend4x, "Blend4x" },
    { 0x10a, 14, 2, 2, gt3_n_fx_viewport, "Viewport" },
    { 0x10b, 0, 0, 0, gt3_n_fx_reset_alloc, "FXResetAlloc" },
    { 0x10c, 2, 1, 2, gt3_n_fx_label, "LabelChain" },
  };
  (void)gt3_n_unused;
  (void)gt3_n_nothing;
  for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) gt3_vm_add_native(vm, t[i].id, t[i].ints, t[i].objs, t[i].ret, t[i].fn, t[i].name);
  vm->destroy_mesh = gt3_destroy_mesh_data;
  vm->release_data = gt3_release_data;
  vm->walk_data = gt3_walk_data;
}

#endif /* GT3_NATIVES_IMPLEMENTATION_DONE */
#endif /* GT3_NATIVES_IMPLEMENTATION */
