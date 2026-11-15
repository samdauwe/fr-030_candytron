/* mesh.h - the half-edge mesh of Candytron's GenThree (genmesh.cpp) and its vertex recorder - header-only C99 (stb style)

   Topology (vertices, edges, faces as the original's records, in the original's order and numbering) and the vertex buffer (position, normal, uv0) of the intro's meshes.
   Every operation that computes vertices goes through the recorder, as in the original: RecBegin / RecEnd collect a command stream of `ADDSCALE`, `XFORM`, `NORMAL`,
   `PERLIN`, `BONE`, `F2VN` and `EWK` commands, which is played at once and, for a mesh in store mode, kept to be played again every frame with the animation labels.
   Only what the intro uses is here (sINTRO, sINTRO_X, sPLAYER): no Displace, Bevel, Cut, Torus, Sphere, XSI import.

   Floating point: the original's x87 unit runs with a 24 bit significand and rounds down; run the natives under gt3_round_down() (csl_natives.h) and the single precision
   arithmetic of this file is the same arithmetic. sin and cos are in gt3_mesh_sin / gt3_mesh_cos.

   In ONE .c file define GT3_MESH_IMPLEMENTATION before including; GT3_MESH_STATIC makes the functions static. The recorder's scratch memory is a gt3_mesh_ctx the caller owns. */
#ifndef GT3_MESH_H_INCLUDED
#define GT3_MESH_H_INCLUDED

#include <stdint.h>
#include <stdlib.h>

#include <candytron/core/bitmap.h>
#include <candytron/core/rand.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GT3_MESH_API
#ifdef GT3_MESH_STATIC
#define GT3_MESH_API static
#else
#define GT3_MESH_API extern
#endif
#endif

typedef struct gt3_vec {
  float x, y, z, w;
} gt3_vec;
typedef struct gt3_mat {
  gt3_vec i, j, k, l;
} gt3_mat;

/* the selection masks are 00vvffee: vertex, face, edge */
typedef struct gt3_mvert {
  uint8_t mask, id, select, used;
  int32_t next, first, temp, temp2, reindex;
  uint8_t weight[4], matrix[4];
} gt3_mvert;

typedef struct gt3_medge {
  uint8_t mask, id, select, used;
  int32_t next[2], prev[2]; /* index = edge << 1 | direction */
  int32_t face[2], vert[2];
  int32_t temp[2];
  int32_t crease;
} gt3_medge;

typedef struct gt3_mface {
  uint8_t mask, id, select, used;
  int32_t material; /* 0: deleted */
  int32_t edge;
  int32_t temp, temp2, temp3;
} gt3_mface;

typedef struct gt3_mbone {
  float base_srt[9], trans_srt[9];
  gt3_mat matrix;
  int32_t parent;
} gt3_mbone;

typedef struct gt3_mcurve {
  int32_t matrix, curve;
} gt3_mcurve;

/* animation label: what GenMeshAnim keeps besides the members the script writes (s, r, t, v, p: 13 words, held by the caller) */
typedef struct gt3_manim {
  int32_t matrix_index, operation;
  uint32_t flags;
  gt3_mat omat;
  gt3_vec ovec;
} gt3_manim;

enum { GT3_GMA_NOP = 0, GT3_GMA_TRANSFORM = 1, GT3_GMA_PERLIN = 2, GT3_GMA_BONE = 3, GT3_GMA_EXNORMAL = 4, GT3_GMA_EWK = 5 };
enum { GT3_GMF_DOUBLE = 1 };
enum { GT3_GMI_POS = 0, GT3_GMI_NORMAL = 1, GT3_GMI_UV0 = 5 };
enum { GT3_GMF_POS = 1, GT3_GMF_NORMAL = 2, GT3_GMF_TANGENT = 4, GT3_GMF_UV0 = 0x20 };
enum { GT3_MESH_MAXLABEL = 32, GT3_REC_MATRICES = 1024, GT3_REC_WORDS = 1024 * 1024 * 2 };

/* the recorder's scratch memory (the original's RBData, RBMat, RBMesh...) and the tables the commands use: one per generation */
struct gt3_mesh;
typedef struct gt3_mesh_ctx {
  uint32_t *data;
  int32_t count, index;
  gt3_mat mat[GT3_REC_MATRICES];
  struct gt3_mesh *owner;
  gt3_rng *rng;
  const gt3_perlin *perlin;
} gt3_mesh_ctx;

/* One pass of one material slot, as GenMesh::Prepare (DoPass1) leaves it for the renderer (render.h): the batches of at most 0x7f00 vertices a draw call may take, each
   the list of the mesh vertices it uses (and for the triangle programs the indices into that list), and the device's geometry handle once the first paint made one. */
enum { GT3_MPASS_MAXBATCH = 16, GT3_MPASS_MAXPASS = 16 };
typedef struct gt3_mpass {
  int32_t used, programm, n_batch;
  int32_t geometry[GT3_MPASS_MAXBATCH], icount[GT3_MPASS_MAXBATCH], vcount[GT3_MPASS_MAXBATCH];
  uint16_t *ib[GT3_MPASS_MAXBATCH];
  int32_t *vb[GT3_MPASS_MAXBATCH];
} gt3_mpass;

typedef struct gt3_mesh {
  gt3_mesh_ctx *ctx;
  gt3_mvert *vert;
  int32_t n_vert, cap_vert;
  gt3_medge *edge;
  int32_t n_edge, cap_edge;
  gt3_mface *face;
  int32_t n_face, cap_face;
  int32_t *mtrl; /* handle of the material of every material slot (0: none); slot 0 is unused */
  int32_t n_mtrl, cap_mtrl;

  gt3_vec *vbuf; /* structure of arrays: vert_size vectors per vertex */
  int32_t vert_mask, vert_size, vert_count, vert_alloc, vert_map[16];

  gt3_mbone *bone;
  int32_t n_bone;
  gt3_mcurve *curve;
  int32_t n_curve;
  int32_t key_count, curve_count;
  float *key_buf;
  int32_t prepared, anim0, anim1;
  gt3_mpass *pass; /* n_pass_alloc x GT3_MPASS_MAXPASS: [material slot][pass], made by gt3_mesh_prepare (render.h) */
  int32_t n_pass_alloc;

  int32_t store_mode, rec_mode, rec_level;
  gt3_vec *rec_vert;
  int32_t n_rec_vert;
  uint32_t *rec_cmd;
  int32_t n_rec_cmd, cap_rec_cmd;
  gt3_mat *rec_mat;
  int32_t n_rec_mat, cap_rec_mat;

  gt3_manim anim[GT3_MESH_MAXLABEL];
  int32_t n_anim, anim_last_mat, anim_last_op;

  int32_t rec_last_source, rec_last_dest, rec_op_count, rec_op_count_at, rec_command_level;
} gt3_mesh;

/* ---- life ---- */
GT3_MESH_API gt3_mesh_ctx *gt3_mesh_ctx_create(gt3_rng *rng, const gt3_perlin *perlin);
GT3_MESH_API void gt3_mesh_ctx_destroy(gt3_mesh_ctx *c);
GT3_MESH_API gt3_mesh *gt3_mesh_create(gt3_mesh_ctx *c);
GT3_MESH_API void gt3_mesh_destroy(gt3_mesh *m);
GT3_MESH_API gt3_mesh *gt3_mesh_copy(const gt3_mesh *src);
GT3_MESH_API void gt3_mesh_init(gt3_mesh *m, uint32_t vertmask, int32_t vertcount);

/* ---- the operations of the script's natives (genmesh.cpp Mesh_*), arguments as the script gives them (16.16 integers, floats) ---- */
GT3_MESH_API void gt3_mesh_new(gt3_mesh *m); /* Mesh_NewMesh: Init(0x23, 1024) */
GT3_MESH_API int gt3_mesh_cube(gt3_mesh *m, int32_t tx, int32_t ty, int32_t tz, int32_t crease);
GT3_MESH_API void gt3_mesh_selectcube(gt3_mesh *m, const float center[3], const float size[3], int32_t flags, uint32_t dmask1, uint32_t dmask0);
GT3_MESH_API void gt3_mesh_subdivide(gt3_mesh *m, int32_t mask, float alpha, int32_t count);
GT3_MESH_API void gt3_mesh_transform(gt3_mesh *m, int32_t mask, const float srt[9]);
GT3_MESH_API void gt3_mesh_material(gt3_mesh *m, int32_t handle, int32_t id, int32_t mask);
GT3_MESH_API void gt3_mesh_calcnormals(gt3_mesh *m, int32_t mode, int32_t mask);
GT3_MESH_API void gt3_mesh_triangulate(gt3_mesh *m, int32_t thres, int32_t mask, int32_t type);
GT3_MESH_API void gt3_mesh_perlin(gt3_mesh *m, int32_t mask, const float srt[9], const float ampl[3]);
GT3_MESH_API void gt3_mesh_selectrandom(gt3_mesh *m, uint32_t ratio, int32_t seed, uint32_t dmask1, uint32_t dmask0);
GT3_MESH_API void gt3_mesh_extrudewk(gt3_mesh *m, int32_t mask, int32_t mode, int32_t count, float distance, const float s[3], const float r[3],
                                     int32_t scalemode);
GT3_MESH_API void gt3_mesh_beginrecord(gt3_mesh *m);
GT3_MESH_API void gt3_mesh_animlabel(gt3_mesh *m, int32_t label, uint32_t flags);
GT3_MESH_API void gt3_mesh_mark_anim(gt3_mesh *m, int mode);
GT3_MESH_API void gt3_mesh_all2mask(gt3_mesh *m, uint32_t dmask1, uint32_t dmask0);
GT3_MESH_API void gt3_mesh_storemode(gt3_mesh *m);
GT3_MESH_API int32_t gt3_mesh_bones(gt3_mesh *m, float phase);

/* The per frame replay (GenMesh::RecReplay): `label_members` are the 13 words (s, r, t, v, p) the script wrote into each animation label, n_anim x 13. */
GT3_MESH_API void gt3_mesh_replay(gt3_mesh *m, const int32_t *label_members);

/* the arithmetic that the original takes from the C library or the x87 */
GT3_MESH_API float gt3_mesh_sin(float x);
GT3_MESH_API float gt3_mesh_cos(float x);

#ifdef __cplusplus
}
#endif

#endif /* GT3_MESH_H_INCLUDED */

#ifdef GT3_MESH_IMPLEMENTATION
#ifndef GT3_MESH_IMPLEMENTATION_DONE
#define GT3_MESH_IMPLEMENTATION_DONE

#include <fenv.h>
#include <math.h>
#include <string.h>

#define GT3_IM_XFORM  1
#define GT3_IM_BONE   2
#define GT3_IM_ADDSCL 3
#define GT3_IM_NORMAL 4
#define GT3_IM_PERLIN 7
#define GT3_IM_F2VN   9
#define GT3_IM_EWK    10

#define GT3_RM_POS    GT3_GMF_POS
#define GT3_RM_NORMAL (GT3_GMF_NORMAL | GT3_GMF_TANGENT)
#define GT3_RM_REST   0x1f8u /* colour 0 and 1, uv 0 to 3 */
#define GT3_RM_ALL    (GT3_RM_POS | GT3_RM_NORMAL | GT3_RM_REST)

#define GT3_PI2F 6.28318530717958647692528676655901f

/* ---- memory: zeroed, grows by doubling; running out is fatal (as the original's new) ---- */

static void gt3_mesh_reserve(void **p, int32_t *cap, size_t size, int32_t need)
{
  if (need <= *cap) return;
  int32_t n = need > *cap * 2 ? need : *cap * 2;
  /* a first allocation comes from calloc: a large one (the babe mesh reserves 64 K vertices, 6 + 3 + 2 MB of arrays) is then zero pages that cost no
     memory until they are written; a growth is a realloc whose new part is cleared */
  void *q = *p ? realloc(*p, (size_t)n * size) : calloc((size_t)n, size);
  if (!q) abort();
  if (*p) memset((char *)q + (size_t)*cap * size, 0, (size_t)(n - *cap) * size);
  *p = q;
  *cap = n;
}

/* the original's sArray::AtLeast + Count */
#define GT3_ENSURE(m, arr, n_arr, cap, need) gt3_mesh_reserve((void **)&(m)->arr, &(m)->cap, sizeof(*(m)->arr), (need))

/* ---- float helpers ---- */

/* The original calls the C library's double sin / cos and uses the result as it comes back from the x87 (a double's value, in an extended register, rounded to 24 bits
   only where it is stored or multiplied). The libm of the host is asked in round to nearest; the callers convert in the current mode. */
static double gt3_sin_d(double x)
{
  volatile double v; /* read back after the mode is restored: the compiler may not move the caller's conversion above fesetround */
  const int mode = fegetround();
  fesetround(FE_TONEAREST);
  v = sin(x);
  fesetround(mode);
  return v;
}
static double gt3_cos_d(double x)
{
  volatile double v; /* read back after the mode is restored: the compiler may not move the caller's conversion above fesetround */
  const int mode = fegetround();
  fesetround(FE_TONEAREST);
  v = cos(x);
  fesetround(mode);
  return v;
}
GT3_MESH_API float gt3_mesh_sin(float x) { return (float)gt3_sin_d((double)x); }
GT3_MESH_API float gt3_mesh_cos(float x) { return (float)gt3_cos_d((double)x); }

static int32_t gt3_mesh_ftol(float f) { return (int32_t)floorf(f); }

static void gt3_v_init4(gt3_vec *v, float x, float y, float z, float w)
{
  v->x = x;
  v->y = y;
  v->z = z;
  v->w = w;
}
/* sVector::Init3 leaves w alone (Init with three arguments clears it) */
static void gt3_v_init3(gt3_vec *v, float x, float y, float z)
{
  v->x = x;
  v->y = y;
  v->z = z;
}
static float gt3_v_dot3(const gt3_vec *a, const gt3_vec *b) { return a->x * b->x + a->y * b->y + a->z * b->z; }
static void gt3_v_sub3(gt3_vec *d, const gt3_vec *a, const gt3_vec *b)
{
  d->x = a->x - b->x;
  d->y = a->y - b->y;
  d->z = a->z - b->z;
}
static void gt3_v_scale3(gt3_vec *v, float s)
{
  v->x *= s;
  v->y *= s;
  v->z *= s;
}
static void gt3_v_addscale3(gt3_vec *v, const gt3_vec *a, float s)
{
  v->x += a->x * s;
  v->y += a->y * s;
  v->z += a->z * s;
}
static void gt3_v_cross3(gt3_vec *d, const gt3_vec *a, const gt3_vec *b)
{
  d->x = a->y * b->z - a->z * b->y;
  d->y = a->z * b->x - a->x * b->z;
  d->z = a->x * b->y - a->y * b->x;
}
static float gt3_v_abs3(const gt3_vec *v) { return sqrtf(gt3_v_dot3(v, v)); }
static void gt3_v_unitsafe3(gt3_vec *v)
{
  const float e = gt3_v_dot3(v, v);
  if (e > 0.000001)
    gt3_v_scale3(v, 1.0f / sqrtf(e));
  else {
    v->x = 1;
    v->y = 0;
    v->z = 0;
  }
}
static void gt3_v_rotate3(gt3_vec *d, const gt3_mat *m, const gt3_vec *v)
{
  d->x = m->i.x * v->x;
  d->y = m->i.y * v->x;
  d->z = m->i.z * v->x;
  gt3_v_addscale3(d, &m->j, v->y);
  gt3_v_addscale3(d, &m->k, v->z);
}
static void gt3_v_rotate3_self(gt3_vec *d, const gt3_mat *m)
{
  const gt3_vec v = *d;
  gt3_v_rotate3(d, m, &v);
}

static void gt3_mat_init(gt3_mat *m)
{
  gt3_v_init4(&m->i, 1, 0, 0, 0);
  gt3_v_init4(&m->j, 0, 1, 0, 0);
  gt3_v_init4(&m->k, 0, 0, 1, 0);
  gt3_v_init4(&m->l, 0, 0, 0, 1);
}

static void gt3_mat_euler_pi2(gt3_mat *m, const float a[3])
{
  float v = a[0] * GT3_PI2F;
  const float sx = gt3_mesh_sin(v), cx = gt3_mesh_cos(v);
  v = a[1] * GT3_PI2F;
  const float sy = gt3_mesh_sin(v), cy = gt3_mesh_cos(v);
  v = a[2] * GT3_PI2F;
  const float sz = gt3_mesh_sin(v), cz = gt3_mesh_cos(v);
  m->i.x = cy * cz;
  m->i.y = cy * sz;
  m->i.z = -sy;
  m->i.w = 0.0f;
  /* the original's compiler reads -a * b as -(a * b): the product is rounded, then negated (not the same as (-a) * b when rounding downward) */
  m->j.x = -(cx * sz) + sx * sy * cz;
  m->j.y = cx * cz + sx * sy * sz;
  m->j.z = sx * cy;
  m->j.w = 0.0f;
  m->k.x = sx * sz + cx * sy * cz;
  m->k.y = -(sx * cz) + cx * sy * sz;
  m->k.z = cx * cy;
  m->k.w = 0.0f;
  gt3_v_init4(&m->l, 0, 0, 0, 1);
}

static void gt3_mat_srt(gt3_mat *m, const float *srt)
{
  gt3_mat_euler_pi2(m, srt + 3);
  gt3_v_scale3(&m->i, srt[0]);
  gt3_v_scale3(&m->j, srt[1]);
  gt3_v_scale3(&m->k, srt[2]);
  gt3_v_init3(&m->l, srt[6], srt[7], srt[8]);
}

static void gt3_mat_trans3(gt3_mat *m)
{
  float t = m->i.y;
  m->i.y = m->j.x;
  m->j.x = t;
  t = m->i.z;
  m->i.z = m->k.x;
  m->k.x = t;
  t = m->j.z;
  m->j.z = m->k.y;
  m->k.y = t;
}

static void gt3_mat_srt_inv(gt3_mat *m, const float *srt)
{
  gt3_vec v;
  gt3_mat_euler_pi2(m, srt + 3);
  gt3_mat_trans3(m);
  gt3_v_scale3(&m->i, 1.0f / srt[0]);
  gt3_v_scale3(&m->j, 1.0f / srt[1]);
  gt3_v_scale3(&m->k, 1.0f / srt[2]);
  gt3_v_init3(&v, -srt[6], -srt[7], -srt[8]);
  gt3_v_rotate3(&m->l, m, &v);
}

/* the intro's loop; d must not be a or b */
static void gt3_mat_mul4(gt3_mat *d, const gt3_mat *a, const gt3_mat *b)
{
  float *tt = &d->i.x;
  const float *aa = &a->i.x, *bb = &b->i.x;
  for (int i = 0; i < 16; i++)
    tt[i] = aa[(i & 12) + 0] * bb[0 + (i & 3)] + aa[(i & 12) + 1] * bb[4 + (i & 3)] + aa[(i & 12) + 2] * bb[8 + (i & 3)] + aa[(i & 12) + 3] * bb[12 + (i & 3)];
}

/* ---- life ---- */

GT3_MESH_API gt3_mesh_ctx *gt3_mesh_ctx_create(gt3_rng *rng, const gt3_perlin *perlin)
{
  gt3_mesh_ctx *c = (gt3_mesh_ctx *)calloc(1, sizeof *c);
  if (!c) return NULL;
  c->data = (uint32_t *)calloc(GT3_REC_WORDS, sizeof(uint32_t));
  if (!c->data) {
    free(c);
    return NULL;
  }
  c->rng = rng;
  c->perlin = perlin;
  return c;
}

GT3_MESH_API void gt3_mesh_ctx_destroy(gt3_mesh_ctx *c)
{
  if (!c) return;
  free(c->data);
  free(c);
}

static void gt3_mvert_init(gt3_mvert *v)
{
  memset(v, 0, sizeof *v);
  v->used = 1;
  v->next = v->first = v->temp = v->temp2 = v->reindex = -1;
  for (int i = 0; i < 4; i++) v->matrix[i] = 0xff;
}

static void gt3_medge_init(gt3_medge *e)
{
  memset(e, 0, sizeof *e);
  e->used = 1;
  e->next[0] = e->next[1] = e->prev[0] = e->prev[1] = e->face[0] = e->face[1] = e->vert[0] = e->vert[1] = e->temp[0] = e->temp[1] = -1;
}

static void gt3_mface_init(gt3_mface *f)
{
  f->mask = f->id = f->select = 0;
  f->used = 1;
  f->material = 1;
  f->edge = -1;
  f->temp = -1;
}

GT3_MESH_API gt3_mesh *gt3_mesh_create(gt3_mesh_ctx *c)
{
  gt3_mesh *m = (gt3_mesh *)calloc(1, sizeof *m);
  if (!m) return NULL;
  m->ctx = c;
  gt3_mesh_reserve((void **)&m->vert, &m->cap_vert, sizeof *m->vert, 16);
  gt3_mesh_reserve((void **)&m->edge, &m->cap_edge, sizeof *m->edge, 16);
  gt3_mesh_reserve((void **)&m->face, &m->cap_face, sizeof *m->face, 16);
  gt3_mesh_reserve((void **)&m->mtrl, &m->cap_mtrl, sizeof *m->mtrl, 16);
  m->n_mtrl = 2;
  m->anim1 = 16;
  m->rec_command_level = -1;
  return m;
}

GT3_MESH_API void gt3_mesh_destroy(gt3_mesh *m)
{
  if (!m) return;
  for (int32_t i = 0; i < m->n_pass_alloc * GT3_MPASS_MAXPASS; i++)
    for (int b = 0; b < GT3_MPASS_MAXBATCH; b++) {
      free(m->pass[i].ib[b]);
      free(m->pass[i].vb[b]);
    }
  free(m->pass);
  free(m->vert);
  free(m->edge);
  free(m->face);
  free(m->mtrl);
  free(m->vbuf);
  free(m->bone);
  free(m->curve);
  free(m->key_buf);
  free(m->rec_vert);
  free(m->rec_cmd);
  free(m->rec_mat);
  free(m);
}

GT3_MESH_API void gt3_mesh_init(gt3_mesh *m, uint32_t vertmask, int32_t vertcount)
{
  m->vert_mask = (int32_t)vertmask;
  m->vert_size = 0;
  m->vert_alloc = vertcount;
  m->vert_count = 0;
  for (int i = 0; i < 16; i++) m->vert_map[i] = (vertmask & (1u << i)) ? m->vert_size++ : -1;
  free(m->vbuf);
  m->vbuf = (gt3_vec *)calloc((size_t)m->vert_size * (size_t)m->vert_alloc, sizeof(gt3_vec));
  if (!m->vbuf) abort();
  GT3_ENSURE(m, vert, n_vert, cap_vert, vertcount);
  GT3_ENSURE(m, edge, n_edge, cap_edge, vertcount * 2);
  GT3_ENSURE(m, face, n_face, cap_face, vertcount / 2);
}

GT3_MESH_API void gt3_mesh_new(gt3_mesh *m) { gt3_mesh_init(m, 0x23, 1024); }

GT3_MESH_API gt3_mesh *gt3_mesh_copy(const gt3_mesh *o)
{
  gt3_mesh *m = gt3_mesh_create(o->ctx);
  if (!m) return NULL;
  GT3_ENSURE(m, vert, n_vert, cap_vert, o->n_vert);
  memcpy(m->vert, o->vert, (size_t)o->n_vert * sizeof *m->vert);
  m->n_vert = o->n_vert;
  GT3_ENSURE(m, edge, n_edge, cap_edge, o->n_edge);
  memcpy(m->edge, o->edge, (size_t)o->n_edge * sizeof *m->edge);
  m->n_edge = o->n_edge;
  GT3_ENSURE(m, face, n_face, cap_face, o->n_face);
  memcpy(m->face, o->face, (size_t)o->n_face * sizeof *m->face);
  m->n_face = o->n_face;
  GT3_ENSURE(m, mtrl, n_mtrl, cap_mtrl, o->n_mtrl);
  memcpy(m->mtrl, o->mtrl, (size_t)o->n_mtrl * sizeof *m->mtrl);
  m->n_mtrl = o->n_mtrl;
  m->prepared = 0;
  m->anim0 = o->anim0;
  m->anim1 = o->anim1;

  const int32_t n_vert = o->n_vert;
  gt3_mesh_init(m, (uint32_t)o->vert_mask, o->vert_alloc);
  m->n_vert = n_vert;
  memcpy(m->vbuf, o->vbuf, (size_t)o->vert_size * (size_t)o->vert_count * sizeof(gt3_vec));
  m->vert_count = o->vert_count;

  if (o->n_bone) {
    m->bone = (gt3_mbone *)malloc((size_t)o->n_bone * sizeof *m->bone);
    if (!m->bone) abort();
    memcpy(m->bone, o->bone, (size_t)o->n_bone * sizeof *m->bone);
    m->n_bone = o->n_bone;
  }
  if (o->n_curve) {
    m->curve = (gt3_mcurve *)malloc((size_t)o->n_curve * sizeof *m->curve);
    if (!m->curve) abort();
    memcpy(m->curve, o->curve, (size_t)o->n_curve * sizeof *m->curve);
    m->n_curve = o->n_curve;
  }
  m->key_count = o->key_count;
  m->curve_count = o->curve_count;
  if (o->key_buf) {
    const size_t n = (size_t)o->key_count * (size_t)o->curve_count;
    m->key_buf = (float *)malloc(n * sizeof(float));
    if (!m->key_buf) abort();
    memcpy(m->key_buf, o->key_buf, n * sizeof(float));
  }

  m->store_mode = o->store_mode;
  if (o->n_rec_vert) {
    m->rec_vert = (gt3_vec *)malloc((size_t)o->n_rec_vert * sizeof(gt3_vec));
    if (!m->rec_vert) abort();
    memcpy(m->rec_vert, o->rec_vert, (size_t)o->n_rec_vert * sizeof(gt3_vec));
    m->n_rec_vert = o->n_rec_vert;
  }
  if (o->n_rec_cmd) {
    m->rec_cmd = (uint32_t *)malloc(((size_t)o->n_rec_cmd + 1) * sizeof(uint32_t));
    if (!m->rec_cmd) abort();
    memcpy(m->rec_cmd, o->rec_cmd, ((size_t)o->n_rec_cmd + 1) * sizeof(uint32_t));
    m->n_rec_cmd = m->cap_rec_cmd = o->n_rec_cmd;
  }
  if (o->n_rec_mat) {
    m->rec_mat = (gt3_mat *)malloc((size_t)o->n_rec_mat * sizeof(gt3_mat));
    if (!m->rec_mat) abort();
    memcpy(m->rec_mat, o->rec_mat, (size_t)o->n_rec_mat * sizeof(gt3_mat));
    m->n_rec_mat = m->cap_rec_mat = o->n_rec_mat;
  }
  m->n_anim = o->n_anim;
  m->anim_last_mat = o->anim_last_mat;
  m->anim_last_op = o->anim_last_op;
  memcpy(m->anim, o->anim, sizeof m->anim);
  return m;
}

/* ---- the recorder ---- */

static uint32_t *gt3_rec_begin(gt3_mesh *m, uint32_t command)
{
  gt3_mesh_ctx *c = m->ctx;
  if (m->rec_mode == 0) {
    c->count = 0;
    c->index = m->n_rec_mat;
    c->owner = m;
    m->rec_mode = 1;
  }
  m->rec_level++;
  if (command) {
    c->data[c->count++] = command;
    m->rec_op_count_at = c->count++;
    m->rec_last_source = 0;
    m->rec_last_dest = 0;
    m->rec_op_count = 0;
    m->rec_command_level = m->rec_level - 1;
  }
  return c->data + c->count;
}

static void gt3_rec_play(gt3_mesh *m, uint32_t *data, gt3_mat *mats);

static void gt3_rec_end(gt3_mesh *m)
{
  gt3_mesh_ctx *c = m->ctx;
  m->rec_level--;
  if (m->rec_level == m->rec_command_level) {
    c->data[m->rec_op_count_at] = (uint32_t)m->rec_op_count;
    m->rec_command_level = -1;
  }
  if (m->rec_level == 0 && m->store_mode) {
    int32_t o = m->n_rec_cmd, n = o + c->count;
    if (n != o) {
      gt3_mesh_reserve((void **)&m->rec_cmd, &m->cap_rec_cmd, sizeof *m->rec_cmd, n + 1);
      memcpy(m->rec_cmd + o, c->data, sizeof(uint32_t) * (size_t)(n - o));
      m->n_rec_cmd = n;
      m->rec_cmd[n] = 0;
    }
    o = m->n_rec_mat;
    n = c->index;
    if (n != o) {
      gt3_mesh_reserve((void **)&m->rec_mat, &m->cap_rec_mat, sizeof *m->rec_mat, n);
      memcpy(m->rec_mat + o, c->mat + o, sizeof(gt3_mat) * (size_t)(n - o));
      m->n_rec_mat = n;
    }
  }
  if (m->rec_level == 0) {
    c->data[c->count++] = 0;
    if (m->rec_mode == 1) gt3_rec_play(m, c->data, c->mat);
    c->owner = NULL;
    c->count = 0;
    c->index = 0;
    m->rec_mode = 0;
  }
}

static void gt3_rec_end_at(gt3_mesh *m, uint32_t *data)
{
  m->ctx->count = (int32_t)(data - m->ctx->data);
  gt3_rec_end(m);
}

static int32_t gt3_rec_matrix(gt3_mesh *m, const gt3_mat *mat)
{
  m->ctx->mat[m->ctx->index] = *mat;
  return m->ctx->index++;
}

GT3_MESH_API void gt3_mesh_storemode(gt3_mesh *m)
{
  if (!m->store_mode) {
    m->store_mode = 1;
    const int32_t n = m->vert_count * m->vert_size;
    free(m->rec_vert);
    m->rec_vert = (gt3_vec *)malloc(((size_t)n + 1) * sizeof(gt3_vec));
    if (!m->rec_vert) abort();
    memcpy(m->rec_vert, m->vbuf, sizeof(gt3_vec) * (size_t)n);
    m->n_rec_vert = n;
  }
}

GT3_MESH_API void gt3_mesh_mark_anim(gt3_mesh *m, int mode)
{
  if (m->store_mode) {
    m->anim_last_op = mode;
    m->anim_last_mat = m->n_rec_mat;
  }
}

GT3_MESH_API void gt3_mesh_animlabel(gt3_mesh *m, int32_t label, uint32_t flags)
{
  (void)label;
  if (m->store_mode && m->n_anim < GT3_MESH_MAXLABEL && m->anim_last_op != GT3_GMA_NOP) {
    gt3_manim *a = &m->anim[m->n_anim++];
    memset(a, 0, sizeof *a);
    a->flags = flags;
    a->matrix_index = m->anim_last_mat;
    a->operation = m->anim_last_op;
    if (m->anim_last_mat < m->n_rec_mat) a->omat = m->rec_mat[m->anim_last_mat];
    if (m->anim_last_mat + 1 < m->n_rec_mat) a->ovec = m->rec_mat[m->anim_last_mat + 1].i;
  }
}

/* RecAddScale: the sum of `count` weighted vectors into the destination, for each vertex component in `mode` */
static void gt3_rec_addscale_array(gt3_mesh *m, int32_t dest, uint32_t count, int32_t mode, const uint32_t *dptr)
{
  dest *= m->vert_size;
  uint32_t *data = gt3_rec_begin(m, 0);
  mode &= m->vert_mask;
  for (int j = 0; j <= 8; j++) {
    if (mode & (1 << j)) {
      const int32_t o = m->vert_map[j];
      m->rec_op_count++;
      *data++ = count;
      for (uint32_t i = 0; i < count; i++) {
        const int32_t v = (int32_t)dptr[i * 2] * m->vert_size + o;
        *data++ = (uint32_t)(v - m->rec_last_source);
        m->rec_last_source = v;
        *data++ = dptr[i * 2 + 1];
      }
      *data++ = (uint32_t)(dest + o - m->rec_last_dest);
      m->rec_last_dest = dest + o;
    }
  }
  gt3_rec_end_at(m, data);
}

static uint32_t gt3_f2u(float f)
{
  uint32_t u;
  memcpy(&u, &f, 4);
  return u;
}
static float gt3_u2f(uint32_t u)
{
  float f;
  memcpy(&f, &u, 4);
  return f;
}

static void gt3_rec_addscale(gt3_mesh *m, int32_t dest, uint32_t count, int32_t mode, const int32_t *v, const float *w)
{
  uint32_t values[64 * 2];
  for (uint32_t i = 0; i < count; i++) {
    values[i * 2 + 0] = (uint32_t)v[i];
    values[i * 2 + 1] = gt3_f2u(w[i]);
  }
  gt3_rec_addscale_array(m, dest, count, mode, values);
}

/* ---- the commands ---- */

static uint32_t *gt3_rec_transform(gt3_mesh *m, uint32_t *data, const gt3_mat *matrices)
{
  uint32_t count = *data++;
  const uint32_t sj = *data++;
  uint32_t dj = *data++;
  const gt3_mat *mtx = matrices + *data++;
  float f;
  if (dj & 0x10) {
    dj &= 0x0f;
    f = 1.0f;
  } else
    f = 0.0f;
  while (count--) {
    const uint32_t vert = *data++;
    const gt3_vec *src = m->vbuf + vert * (uint32_t)m->vert_size + sj;
    gt3_vec *dst = m->vbuf + vert * (uint32_t)m->vert_size + dj;
    const float sx = src->x, sy = src->y, sz = src->z;
    dst->x = dst->x * f + sx * mtx->i.x + sy * mtx->j.x + sz * mtx->k.x + src->w * mtx->l.x;
    dst->y = dst->y * f + sx * mtx->i.y + sy * mtx->j.y + sz * mtx->k.y + src->w * mtx->l.y;
    dst->z = dst->z * f + sx * mtx->i.z + sy * mtx->j.z + sz * mtx->k.z + src->w * mtx->l.z;
    dst->w = src->w;
  }
  return data;
}

static uint32_t *gt3_rec_bone(gt3_mesh *m, uint32_t *data, const gt3_mat *matrices)
{
  uint32_t count = *data++;
  while (count--) {
    gt3_vec *vtx = m->vbuf + *data++;
    float dx = 0, dy = 0, dz = 0;
    for (int i = 0; i < 4; i++) {
      const float weight = gt3_u2f(*data++);
      const gt3_mat *mtx = matrices + *data++;
      dx += weight * (vtx->x * mtx->i.x + vtx->y * mtx->j.x + vtx->z * mtx->k.x + mtx->l.x);
      dy += weight * (vtx->x * mtx->i.y + vtx->y * mtx->j.y + vtx->z * mtx->k.y + mtx->l.y);
      dz += weight * (vtx->x * mtx->i.z + vtx->y * mtx->j.z + vtx->z * mtx->k.z + mtx->l.z);
    }
    vtx->x = dx;
    vtx->y = dy;
    vtx->z = dz;
  }
  return data;
}

static uint32_t *gt3_rec_addscale_inner(gt3_mesh *m, uint32_t *code)
{
  uint32_t count = *code++;
  gt3_vec *src = m->vbuf, *dst = m->vbuf;
  while (count--) {
    uint32_t vcount = *code++;
    float sx = 0, sy = 0, sz = 0, sw = 0;
    do {
      src += (int32_t)*code++;
      const float wgt = gt3_u2f(*code++);
      sx += wgt * src->x;
      sy += wgt * src->y;
      sz += wgt * src->z;
      sw += wgt * src->w;
    } while (--vcount);
    dst += (int32_t)*code++;
    dst->x = sx;
    dst->y = sy;
    dst->z = sz;
    dst->w = sw;
  }
  return code;
}

static uint32_t *gt3_rec_normal(gt3_mesh *m, uint32_t *code)
{
  int32_t count = (int32_t)*code++;
  const int32_t sn = m->vert_map[GT3_GMI_NORMAL];
  const uint32_t mask = *code++;
  while (count--) {
    int32_t vcount = (int32_t)*code++;
    gt3_vec *vd = m->vbuf + *code++;
    const gt3_vec *v0 = m->vbuf + *code++;
    const gt3_vec *v2 = m->vbuf + *code++;
    gt3_vec n, t1, t2, nm;
    float t1l, t2l;
    gt3_v_sub3(&t2, v2, v0);
    float t2l_ = gt3_v_abs3(&t2);
    t2l = t2l_;
    gt3_v_init4(&n, 0, 0, 0, 0);
    while (--vcount > 0) {
      t1 = t2;
      t1l = t2l;
      v0 = m->vbuf + *code++;
      v2 = m->vbuf + *code++;
      gt3_v_sub3(&t2, v2, v0);
      t2l = gt3_v_abs3(&t2);
      if (mask & 1) {
        gt3_v_cross3(&nm, &t1, &t2);
        if (t1l * t2l > 1e-20f) gt3_v_addscale3(&n, &nm, 1.0f / (t1l * t2l));
      }
    }
    if (mask & 1) {
      gt3_v_unitsafe3(&n);
      vd[sn] = n;
    }
  }
  return code;
}

static uint32_t *gt3_rec_perlin(gt3_mesh *m, uint32_t *code, const gt3_mat *matrices)
{
  uint32_t count = *code++;
  const uint32_t mat = *code++;
  const gt3_vec amp = matrices[mat + 1].i;
  const gt3_perlin *pl = m->ctx->perlin;
  while (count--) {
    gt3_vec *vert = &m->vbuf[*code++ * (uint32_t)m->vert_size];
    gt3_vec t0, t1, t2;
    float fs[3];
    int32_t is[3];
    /* t0.Rotate34(matrices[mat], *vert) */
    t0.x = matrices[mat].i.x * vert->x;
    t0.y = matrices[mat].i.y * vert->x;
    t0.z = matrices[mat].i.z * vert->x;
    t0.w = matrices[mat].i.w * vert->x;
    t0.x += matrices[mat].j.x * vert->y;
    t0.y += matrices[mat].j.y * vert->y;
    t0.z += matrices[mat].j.z * vert->y;
    t0.w += matrices[mat].j.w * vert->y;
    t0.x += matrices[mat].k.x * vert->z;
    t0.y += matrices[mat].k.y * vert->z;
    t0.z += matrices[mat].k.z * vert->z;
    t0.w += matrices[mat].k.w * vert->z;
    t0.x += matrices[mat].l.x;
    t0.y += matrices[mat].l.y;
    t0.z += matrices[mat].l.z;
    t0.w += matrices[mat].l.w;
    const float *t0f = &t0.x;
    for (int i = 0; i < 3; i++) {
      fs[i] = t0f[i];
      is[i] = gt3_mesh_ftol(fs[i]);
      fs[i] -= (float)is[i];
      is[i] &= 255;
      fs[i] = fs[i] * fs[i] * fs[i] * (10.0f + fs[i] * (6.0f * fs[i] - 15.0f));
    }
#define IX   is[0]
#define IY   is[1]
#define IZ   is[2]
#define P    pl->permute
#define G(k) ((const gt3_vec *)pl->gradient3d[(k) & 15])
    /* trilerp */
    {
      const gt3_vec *a = G(P[P[P[IX] + IY] + IZ]), *b = G(P[P[P[IX + 1] + IY] + IZ]);
      t0.x = a->x + (b->x - a->x) * fs[0];
      t0.y = a->y + (b->y - a->y) * fs[0];
      t0.z = a->z + (b->z - a->z) * fs[0];
    }
    {
      const gt3_vec *a = G(P[P[P[IX] + IY + 1] + IZ]), *b = G(P[P[P[IX + 1] + IY + 1] + IZ]);
      t1.x = a->x + (b->x - a->x) * fs[0];
      t1.y = a->y + (b->y - a->y) * fs[0];
      t1.z = a->z + (b->z - a->z) * fs[0];
    }
    t0.x = t0.x + (t1.x - t0.x) * fs[1];
    t0.y = t0.y + (t1.y - t0.y) * fs[1];
    t0.z = t0.z + (t1.z - t0.z) * fs[1];
    {
      const gt3_vec *a = G(P[P[P[IX] + IY] + IZ + 1]), *b = G(P[P[P[IX + 1] + IY] + IZ + 1]);
      t1.x = a->x + (b->x - a->x) * fs[0];
      t1.y = a->y + (b->y - a->y) * fs[0];
      t1.z = a->z + (b->z - a->z) * fs[0];
    }
    {
      const gt3_vec *a = G(P[P[P[IX] + IY + 1] + IZ + 1]), *b = G(P[P[P[IX + 1] + IY + 1] + IZ + 1]);
      t2.x = a->x + (b->x - a->x) * fs[0];
      t2.y = a->y + (b->y - a->y) * fs[0];
      t2.z = a->z + (b->z - a->z) * fs[0];
    }
    t1.x = t1.x + (t2.x - t1.x) * fs[1];
    t1.y = t1.y + (t2.y - t1.y) * fs[1];
    t1.z = t1.z + (t2.z - t1.z) * fs[1];
    t0.x = t0.x + (t1.x - t0.x) * fs[2];
    t0.y = t0.y + (t1.y - t0.y) * fs[2];
    t0.z = t0.z + (t1.z - t0.z) * fs[2];
#undef IX
#undef IY
#undef IZ
#undef P
#undef G
    vert->x += t0.x * amp.x;
    vert->y += t0.y * amp.y;
    vert->z += t0.z * amp.z;
  }
  return code;
}

static void gt3_mesh_calcnormal(const gt3_mesh *m, gt3_vec *n, int32_t o0, int32_t o1, int32_t o2)
{
  gt3_vec t1, t2;
  gt3_v_sub3(&t1, &m->vbuf[o1], &m->vbuf[o0]);
  gt3_v_sub3(&t2, &m->vbuf[o2], &m->vbuf[o0]);
  gt3_v_cross3(n, &t1, &t2);
}

static uint32_t *gt3_rec_f2vn(gt3_mesh *m, uint32_t *code)
{
  uint32_t count = *code++;
  const int32_t sn = m->vert_map[GT3_GMI_NORMAL];
  while (count--) {
    const int32_t v0 = (int32_t)*code++, v1 = (int32_t)*code++, v2 = (int32_t)*code++;
    gt3_mesh_calcnormal(m, &m->vbuf[v1 + sn], v0, v1, v2);
    gt3_v_unitsafe3(&m->vbuf[v1 + sn]);
  }
  return code;
}

static uint32_t *gt3_rec_ewk(gt3_mesh *m, uint32_t *code, const gt3_mat *matrices)
{
  const int32_t sn = m->vert_map[GT3_GMI_NORMAL];
  int32_t gcount = (int32_t)*code++;
  const uint32_t local = *code++;
  const uint32_t mtx = *code++;
  const int32_t vs = m->vert_size;
  float vals[3];
  gt3_mat mat;

  const float ic = 1.0f / matrices[mtx].j.w;
  const float dist = matrices[mtx].i.w * ic;
  for (int i = 0; i < 3; i++) vals[i] = (&matrices[mtx].j.x)[i] * ic;
  gt3_mat_euler_pi2(&mat, vals);

  while (gcount--) {
    const int32_t count = (int32_t)*code++;
    gt3_vec sum, t;
    gt3_v_init4(&sum, 0, 0, 0, 0);
    if (local) {
      for (int32_t i = 0; i != count; i++) {
        gt3_vec *vd = m->vbuf + code[i] * (uint32_t)vs;
        sum.x += vd->x + vd[sn].x * dist;
        sum.y += vd->y + vd[sn].y * dist;
        sum.z += vd->z + vd[sn].z * dist;
      }
    }
    gt3_v_scale3(&sum, 1.0f / (float)count);
    for (int32_t i = 0; i != count; i++) {
      gt3_vec *vd = m->vbuf + code[i] * (uint32_t)vs;
      t.x = (vd->x + vd[sn].x * dist - sum.x) * matrices[mtx].i.x + sum.x;
      t.y = (vd->y + vd[sn].y * dist - sum.y) * matrices[mtx].i.y + sum.y;
      t.z = (vd->z + vd[sn].z * dist - sum.z) * matrices[mtx].i.z + sum.z;
      gt3_v_rotate3(vd, &mat, &t);
      gt3_v_rotate3_self(&vd[sn], &mat);
    }
    code += count;
  }
  return code;
}

static void gt3_rec_play(gt3_mesh *m, uint32_t *data, gt3_mat *mats)
{
  while (*data) {
    switch (*data++) {
    case GT3_IM_XFORM: data = gt3_rec_transform(m, data, mats); break;
    case GT3_IM_BONE: data = gt3_rec_bone(m, data, mats); break;
    case GT3_IM_ADDSCL: data = gt3_rec_addscale_inner(m, data); break;
    case GT3_IM_NORMAL: data = gt3_rec_normal(m, data); break;
    case GT3_IM_PERLIN: data = gt3_rec_perlin(m, data, mats); break;
    case GT3_IM_F2VN: data = gt3_rec_f2vn(m, data); break;
    case GT3_IM_EWK: data = gt3_rec_ewk(m, data, mats); break;
    default: return;
    }
  }
}

/* ---- bones and the per frame replay ---- */

static void gt3_mesh_bones_modify(gt3_mesh *m, int32_t matrix, float phase)
{
  gt3_mat *mp = matrix < 0 ? &m->ctx->mat[m->ctx->index] : &m->rec_mat[matrix];
  gt3_mat mat;

  if (m->n_bone < 1) return; /* a mesh made without Babe's data has no skeleton (and is never painted by the original) */
  if (m->key_count > 0 && m->n_curve > 0) {
    int32_t i = (int32_t)(((float)m->anim0 + phase * (float)(m->anim1 - m->anim0)) * 1024.0f);
    const int32_t ki0 = (i / 1024) % m->key_count, ki1 = (i / 1024 + 1) % m->key_count;
    i &= 1023;
    const float f1 = (float)i / 1024.0f;
    for (i = 0; i < m->n_curve; i++) {
      const float a = m->key_buf[i * m->key_count + ki0], b = m->key_buf[i * m->key_count + ki1];
      const float val = a + (b - a) * f1;
      m->bone[m->curve[i].matrix].trans_srt[m->curve[i].curve] = val;
    }
  }

  gt3_mat_srt(&m->bone[0].matrix, m->bone[0].trans_srt);
  gt3_mat_srt_inv(&mat, m->bone[0].base_srt);
  gt3_mat_mul4(&mp[0], &mat, &m->bone[0].matrix);
  for (int32_t i = 1; i < m->n_bone; i++) {
    gt3_mat_srt(&mat, m->bone[i].trans_srt);
    gt3_mat_mul4(&m->bone[i].matrix, &mat, &m->bone[m->bone[i].parent].matrix);
    gt3_mat_srt_inv(&mat, m->bone[i].base_srt);
    gt3_mat_mul4(&mp[i], &mat, &m->bone[i].matrix);
  }
  for (int32_t i = 0; i < m->n_bone; i++) {
    mp[i].i.z = -mp[i].i.z;
    mp[i].j.z = -mp[i].j.z;
    mp[i].k.z = -mp[i].k.z;
    mp[i].l.z = -mp[i].l.z;
  }
}

GT3_MESH_API int32_t gt3_mesh_bones(gt3_mesh *m, float phase)
{
  if (m->n_bone < 1) return 0; /* a mesh made without Babe's data has no skeleton */
  uint32_t *data = gt3_rec_begin(m, GT3_IM_BONE);
  gt3_mesh_bones_modify(m, -1, phase);
  const int32_t mi = m->ctx->index;
  m->ctx->index += m->n_bone;
  int32_t lm = 0;
  for (int32_t i = 0; i < m->n_vert; i++) {
    if (m->vert[i].matrix[0] != 0xff) {
      m->rec_op_count++;
      *data++ = (uint32_t)(i * m->vert_size);
      for (int j = 0; j < 4; j++) {
        float wgt;
        if (m->vert[i].matrix[j] != 0xff) {
          wgt = (float)m->vert[i].weight[j] / 255.0f;
          lm = m->vert[i].matrix[j] + mi;
        } else
          wgt = 0.0f;
        *data++ = gt3_f2u(wgt);
        *data++ = (uint32_t)lm;
      }
    }
  }
  gt3_rec_end_at(m, data);
  return mi;
}

GT3_MESH_API void gt3_mesh_replay(gt3_mesh *m, const int32_t *members)
{
  float srt[12] = { 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  if (!m->store_mode || m->n_rec_cmd <= 0) return;
  for (int32_t i = 0; i < m->n_anim; i++) {
    const gt3_manim *g = &m->anim[i];
    gt3_mat mat;
    if (!(g->flags & GT3_GMF_DOUBLE))
      for (int j = 0; j < 12; j++) srt[j] = (float)members[i * 13 + j] / 65536.0f;
    switch (g->operation) {
    case GT3_GMA_TRANSFORM:
      gt3_mat_srt(&mat, srt);
      gt3_mat_mul4(&m->rec_mat[g->matrix_index], &g->omat, &mat);
      break;
    case GT3_GMA_PERLIN: {
      gt3_mat_srt(&mat, srt);
      gt3_mat_mul4(&m->rec_mat[g->matrix_index], &g->omat, &mat);
      gt3_vec *v = &m->rec_mat[g->matrix_index + 1].i;
      gt3_v_init4(v, srt[9], srt[10], srt[11], 0);
      v->x += g->ovec.x;
      v->y += g->ovec.y;
      v->z += g->ovec.z;
      break;
    }
    case GT3_GMA_BONE:
      gt3_mesh_bones_modify(m, g->matrix_index, (float)(((int32_t)members[i * 13 + 12] + gt3_mesh_ftol(g->omat.i.x * 65536.0f)) & 0xffff) / 65536.0f);
      break;
    case GT3_GMA_EWK: {
      gt3_mat *r = &m->rec_mat[g->matrix_index];
      *r = g->omat;
      r->i.x *= srt[0];
      r->i.y *= srt[1];
      r->i.z *= srt[2];
      r->i.w += (float)members[i * 13 + 12] / 65536.0f;
      r->j.x += srt[3];
      r->j.y += srt[4];
      r->j.z += srt[5];
      break;
    }
    default: break;
    }
  }
  memcpy(m->vbuf, m->rec_vert, sizeof(gt3_vec) * (size_t)m->n_rec_vert);
  gt3_rec_play(m, m->rec_cmd, m->rec_mat);
}

/* ---- half-edge navigation (i = edge << 1 | direction) ---- */

#define EDGE_(i) (m->edge[(i) >> 1])
static int32_t gt3_face_id(const gt3_mesh *m, int32_t i) { return EDGE_(i).face[i & 1]; }
static int32_t gt3_face_id_i(const gt3_mesh *m, int32_t i) { return EDGE_(i).face[~i & 1]; }
static int32_t gt3_vert_id(const gt3_mesh *m, int32_t i) { return EDGE_(i).vert[i & 1]; }
static int32_t gt3_next_face_edge(const gt3_mesh *m, int32_t i) { return EDGE_(i).next[i & 1]; }
static int32_t gt3_prev_face_edge(const gt3_mesh *m, int32_t i) { return EDGE_(i).prev[i & 1]; }
static int32_t gt3_next_vert_edge(const gt3_mesh *m, int32_t i) { return EDGE_(i).prev[i & 1] ^ 1; }
static int32_t gt3_prev_vert_edge(const gt3_mesh *m, int32_t i) { return EDGE_(i).next[~i & 1]; }
#undef EDGE_

#define VERT(i)       (m->vert[(i)])
#define EDGE(i)       (m->edge[(i)])
#define FACE(i)       (m->face[(i)])
#define GET_FACE(i)   (m->face[gt3_face_id(m, (i))])
#define GET_FACE_I(i) (m->face[gt3_face_id_i(m, (i))])
#define GET_VERT(i)   (m->vert[gt3_vert_id(m, (i))])

static void gt3_sel(uint8_t *mask, uint32_t mask1, uint32_t mask0) { *mask = (uint8_t)((*mask & ~mask0) | mask1); }

static int32_t gt3_add_vert(gt3_mesh *m)
{
  const int32_t c = m->n_vert;
  /* Realloc(c + 1) */
  if (c + 1 >= m->vert_alloc) {
    int32_t ns = c + 1 > m->vert_alloc * 2 ? c + 1 : m->vert_alloc * 2;
    gt3_vec *nd = (gt3_vec *)calloc((size_t)ns * (size_t)m->vert_size, sizeof(gt3_vec));
    if (!nd) abort();
    memcpy(nd, m->vbuf, sizeof(gt3_vec) * (size_t)m->vert_size * (size_t)m->vert_count);
    free(m->vbuf);
    m->vbuf = nd;
    m->vert_alloc = ns;
  }
  m->vert_count = c + 1;
  GT3_ENSURE(m, vert, n_vert, cap_vert, c + 1);
  m->n_vert++;
  return c;
}

static int32_t gt3_add_new_vert(gt3_mesh *m)
{
  const int32_t v = gt3_add_vert(m);
  gt3_mvert_init(&VERT(v));
  VERT(v).first = VERT(v).next = VERT(v).reindex = v;
  return v;
}

static int32_t gt3_add_copied_vert(gt3_mesh *m, int32_t src)
{
  const int32_t v = gt3_add_vert(m);
  VERT(v) = VERT(src);
  VERT(v).reindex = v;
  return v;
}

static void gt3_split_face(gt3_mesh *m, int32_t i0, int32_t i1, uint32_t dmask1, uint32_t dmask0)
{
  const int32_t p0 = gt3_prev_face_edge(m, i0), p1 = gt3_prev_face_edge(m, i1), f0 = gt3_face_id(m, i0);
  const int32_t ee = m->n_edge;
  GT3_ENSURE(m, edge, n_edge, cap_edge, ee + 1);
  m->n_edge++;
  const int32_t f1 = m->n_face;
  GT3_ENSURE(m, face, n_face, cap_face, f1 + 1);
  m->n_face++;

  gt3_medge *edge = m->edge;
  gt3_mface *face = m->face;
  edge[ee] = edge[i0 / 2];
  edge[ee].crease = 0;
  gt3_sel(&edge[ee].mask, dmask1, dmask0);
  edge[ee].vert[0] = gt3_vert_id(m, i1);
  edge[ee].vert[1] = gt3_vert_id(m, i0);
  edge[ee].face[0] = f0;
  edge[ee].face[1] = f0;
  edge[ee].next[0] = i0;
  edge[ee].next[1] = i1;
  edge[ee].prev[0] = p1;
  edge[ee].prev[1] = p0;
  edge[ee].temp[0] = -1;
  edge[ee].temp[1] = -1;

  edge[i0 >> 1].prev[i0 & 1] = ee * 2;
  edge[p0 >> 1].next[p0 & 1] = ee * 2 + 1;
  edge[i1 >> 1].prev[i1 & 1] = ee * 2 + 1;
  edge[p1 >> 1].next[p1 & 1] = ee * 2;

  face[f1] = face[f0];
  gt3_sel(&face[f1].mask, dmask1 >> 8, dmask0 >> 8);
  face[f0].edge = i0;
  face[f1].edge = i1;

  int32_t i = i1;
  do {
    edge[i / 2].face[i & 1] = f1;
    i = gt3_next_face_edge(m, i);
  } while (i != i1);
}

static void gt3_fix_vert_cycle(gt3_mesh *m, int32_t i)
{
  int32_t v0, v, vn, e;
  v0 = v = gt3_vert_id(m, i);
  VERT(v).first = v0;
  VERT(v).next = v0;
  e = i;
  do {
    EDGE(e / 2).vert[e & 1] = v;
    e = gt3_next_vert_edge(m, e);
    vn = gt3_vert_id(m, e);
    if (EDGE(e / 2).crease && vn != v) {
      VERT(v).next = vn;
      v = vn;
      VERT(v).first = v0;
      VERT(v).next = v0;
    }
  } while (e != i);
}

static void gt3_split_bridge(gt3_mesh *m, int32_t a, int32_t d, int32_t *va, int32_t *vb, uint32_t dmask1, uint32_t dmask0)
{
  int32_t e, p0, p1, n0, n1, v0, v1, cf, i, lc = 0;
  v1 = gt3_vert_id(m, a);
  e = m->n_edge;
  GT3_ENSURE(m, edge, n_edge, cap_edge, e + 1);
  m->n_edge = e + 1;
  v0 = *va;

  gt3_sel(&VERT(v0).mask, dmask1 >> 16, dmask0 >> 16);
  VERT(v0).first = v0;
  VERT(v0).next = v0;

  a = a ^ 1;
  p0 = gt3_prev_face_edge(m, d);
  n1 = gt3_next_face_edge(m, a);

  cf = 0;
  if (n1 == d) {
    p0 = e * 2 + 1;
    n1 = e * 2;
  } else {
    for (i = n1; i != d; i = gt3_prev_vert_edge(m, i)) {
      if (EDGE(i / 2).crease) {
        cf |= EDGE(i / 2).crease;
        lc = i;
      }
    }
  }

  gt3_medge *edge = m->edge;
  edge[e] = edge[a / 2];
  edge[e].crease = cf;
  gt3_sel(&edge[e].mask, dmask1, dmask0);
  edge[e].vert[0] = v0;
  edge[e].vert[1] = v1;
  edge[e].face[0] = gt3_face_id(m, d);
  edge[e].face[1] = gt3_face_id(m, a);
  edge[e].next[0] = n0 = d;
  edge[e].next[1] = n1;
  edge[e].prev[0] = p0;
  edge[e].prev[1] = p1 = a;
  edge[e].temp[0] = -1;
  edge[e].temp[1] = -1;

  edge[n0 / 2].prev[n0 & 1] = e * 2;
  edge[p0 / 2].next[p0 & 1] = e * 2;
  edge[n1 / 2].prev[n1 & 1] = e * 2 + 1;
  edge[p1 / 2].next[p1 & 1] = e * 2 + 1;

  if (cf) {
    v0 = gt3_vert_id(m, lc);
    v1 = gt3_add_copied_vert(m, v0);
    gt3_sel(&VERT(v1).mask, dmask1 >> 16, dmask0 >> 16);
    EDGE(lc / 2).vert[lc & 1] = v1;
  }

  gt3_fix_vert_cycle(m, e * 2);
  gt3_fix_vert_cycle(m, d);

  *va = gt3_vert_id(m, e * 2);
  *vb = gt3_vert_id(m, n1);
}

static int32_t gt3_add_edge(gt3_mesh *m, int32_t v0, int32_t v1, int32_t face)
{
  for (int32_t i = 0; i < m->n_edge; i++) {
    if (EDGE(i).face[1] == -1) {
      if (VERT(EDGE(i).vert[0]).first == VERT(v1).first && VERT(EDGE(i).vert[1]).first == VERT(v0).first) {
        EDGE(i).vert[1] = v0;
        EDGE(i).face[1] = face;
        FACE(face).edge = i * 2 + 1;
        return i * 2 + 1;
      }
    }
  }
  const int32_t e = m->n_edge;
  GT3_ENSURE(m, edge, n_edge, cap_edge, e + 1);
  m->n_edge = e + 1;
  gt3_medge_init(&EDGE(e));
  EDGE(e).vert[0] = v0;
  EDGE(e).vert[1] = v1;
  EDGE(e).face[0] = face;
  EDGE(e).face[1] = -1;
  FACE(face).edge = e * 2;
  return e * 2;
}

static void gt3_reindex(gt3_mesh *m)
{
  for (int32_t i = 0; i < m->n_vert; i++) {
    VERT(i).first = VERT(VERT(i).first).reindex;
    VERT(i).next = VERT(VERT(i).next).reindex;
  }
  for (int32_t i = 0; i < m->n_edge; i++) {
    EDGE(i).vert[0] = VERT(EDGE(i).vert[0]).reindex;
    EDGE(i).vert[1] = VERT(EDGE(i).vert[1]).reindex;
  }
}

static int gt3_is_border_edge(const gt3_mesh *m, int32_t i, int32_t mode)
{
  const int s0 = GET_FACE(i).select;
  int s1 = GET_FACE_I(i).select;
  if ((mode == 1 && (EDGE(i / 2).crease & GT3_RM_NORMAL)) || mode >= 2) s1 = 0;
  return s0 && !s1;
}

/* ---- selection (the sINTRO_X versions: edges are not touched) ---- */

static void gt3_mask2sel(gt3_mesh *m, uint32_t mask)
{
  if (mask & 0x00ff0000u)
    for (int32_t i = 0; i < m->n_vert; i++) VERT(i).select = (VERT(i).mask & (mask >> 16)) != 0;
  if (mask & 0x0000ff00u)
    for (int32_t i = 0; i < m->n_face; i++) FACE(i).select = (FACE(i).mask & (mask >> 8)) != 0;
}

static void gt3_all2sel(gt3_mesh *m, int sel)
{
  for (int32_t i = 0; i < m->n_vert; i++) VERT(i).select = (uint8_t)sel;
  for (int32_t i = 0; i < m->n_face; i++) FACE(i).select = (uint8_t)(sel && FACE(i).material);
}

static void gt3_sel2mask(gt3_mesh *m, uint32_t dmask1, uint32_t dmask0)
{
  const uint32_t mask = dmask1 | dmask0;
  if (mask & 0x00ff0000u)
    for (int32_t i = 0; i < m->n_vert; i++)
      if (VERT(i).select) gt3_sel(&VERT(i).mask, dmask1 >> 16, dmask0 >> 16);
  if (mask & 0x0000ff00u)
    for (int32_t i = 0; i < m->n_face; i++)
      if (FACE(i).select) gt3_sel(&FACE(i).mask, dmask1 >> 8, dmask0 >> 8);
}

GT3_MESH_API void gt3_mesh_all2mask(gt3_mesh *m, uint32_t dmask1, uint32_t dmask0)
{
  const uint32_t mask = dmask1 | dmask0;
  for (int32_t i = 0; i < m->n_vert; i++) {
    if (mask & 0x00ff0000u) gt3_sel(&VERT(i).mask, dmask1 >> 16, dmask0 >> 16);
    VERT(i).select = 1;
  }
  for (int32_t i = 0; i < m->n_face; i++) {
    if (!FACE(i).material) continue;
    if (mask & 0x0000ff00u) gt3_sel(&FACE(i).mask, dmask1 >> 8, dmask0 >> 8);
    FACE(i).select = 1;
  }
}

static void gt3_face2vert(gt3_mesh *m)
{
  for (int32_t i = 0; i < m->n_vert; i++) VERT(i).select = 0;
  for (int32_t i = 0; i < m->n_face; i++) {
    if (FACE(i).select) {
      int32_t e, ee;
      e = ee = FACE(i).edge;
      do {
        GET_VERT(e).select = 1;
        e = gt3_next_face_edge(m, e);
      } while (e != ee);
    }
  }
}

static void gt3_set_material(gt3_mesh *m, int32_t id, int32_t handle)
{
  const int32_t o = m->n_mtrl;
  GT3_ENSURE(m, mtrl, n_mtrl, cap_mtrl, id + 1);
  for (int32_t i = o; i <= id; i++) m->mtrl[i] = 0;
  if (id + 1 > m->n_mtrl) m->n_mtrl = id + 1;
  m->mtrl[id] = handle;
  for (int32_t i = 0; i < m->n_face; i++)
    if (FACE(i).select) FACE(i).material = id;
}

static int32_t gt3_find_crease(const gt3_mesh *m, int32_t edge, int32_t mask)
{
  int32_t ee = edge;
  do {
    if (EDGE(edge / 2).crease & mask)
      ee = edge;
    else
      edge = gt3_prev_vert_edge(m, edge);
  } while (ee != edge);
  return edge;
}

/* ---- construction ---- */

static void gt3_trans_vert(gt3_mesh *m, const gt3_mat *mat, int32_t sj, int32_t djj)
{
  sj = m->vert_map[sj];
  if (sj == -1) return;
  int32_t dj = m->vert_map[djj & 0x0f];
  if (dj == -1) return;
  dj |= djj & 0x10;

  uint32_t *data = gt3_rec_begin(m, GT3_IM_XFORM);
  *data++ = (uint32_t)sj;
  *data++ = (uint32_t)dj;
  *data++ = (uint32_t)gt3_rec_matrix(m, mat);
  for (int32_t i = 0; i < m->n_vert; i++) {
    if (VERT(i).select) {
      *data++ = (uint32_t)i;
      m->rec_op_count++;
    }
  }
  gt3_rec_end_at(m, data);
}

static void gt3_select_cube(gt3_mesh *m, const float vc[3], const float vs[3], int set, uint32_t dmask1, uint32_t dmask0)
{
  for (int32_t i = 0; i < m->n_vert; i++) {
    const gt3_vec *p = &m->vbuf[i * m->vert_size];
    VERT(i).temp = 0;
    if (fabsf(p->x - vc[0]) <= vs[0] && fabsf(p->y - vc[1]) <= vs[1] && fabsf(p->z - vc[2]) <= vs[2]) {
      gt3_sel(&VERT(i).mask, dmask1 >> 16, dmask0 >> 16);
      VERT(i).select = 1;
      VERT(i).temp = 1;
    } else if (set) {
      gt3_sel(&VERT(i).mask, 0, dmask1 >> 16);
      VERT(i).select = 0;
    }
  }
  for (int32_t i = 0; i < m->n_face; i++) {
    if (!FACE(i).material) continue;
    int32_t e, ee;
    e = ee = FACE(i).edge;
    int all = 1;
    do {
      if (!GET_VERT(e).temp) all = 0;
      e = gt3_next_face_edge(m, e);
    } while (ee != e && all);
    if (all) {
      gt3_sel(&FACE(i).mask, dmask1 >> 8, dmask0 >> 8);
      FACE(i).select = 1;
    } else if (set) {
      gt3_sel(&FACE(i).mask, 0, dmask1 >> 8);
      FACE(i).select = 0;
    }
  }
}

static void gt3_ring(gt3_mesh *m, int32_t count, uint32_t dmask, float radius, float phase)
{
  GT3_ENSURE(m, vert, n_vert, cap_vert, count);
  GT3_ENSURE(m, edge, n_edge, cap_edge, count);
  m->n_edge = count;
  GT3_ENSURE(m, face, n_face, cap_face, 2);
  m->n_face = 2;
  gt3_mface_init(&FACE(0));
  FACE(0).edge = 0;
  gt3_mface_init(&FACE(1));
  FACE(1).edge = 1;
  const int32_t sj = m->vert_map[GT3_GMI_UV0];
  for (int32_t i = 0; i < count; i++) {
    gt3_add_new_vert(m);
    gt3_vec *vp = &m->vbuf[i * m->vert_size];
    /* sFSin(...) * radius: the product of the extended result with the float, rounded once */
    vp->x = (float)((long double)gt3_sin_d((double)(phase + (float)i * GT3_PI2F / (float)count)) * (long double)radius);
    volatile double ncos = -gt3_cos_d((double)(phase + (float)i * GT3_PI2F / (float)count)); /* the sign is applied to the call's result, before the product */
    vp->y = (float)((long double)ncos * (long double)radius);
    vp->w = 1.0f;
    if (sj != -1) {
      vp[sj].x = (float)i * 1.0f / (float)count;
      vp[sj].w = 1.0f;
    }
    const int32_t in = (i + 1) % count, ip = (i + count - 1) % count;
    gt3_medge *ed = &EDGE(i);
    gt3_medge_init(ed);
    ed->next[0] = 2 * in;
    ed->next[1] = 2 * ip + 1;
    ed->prev[0] = 2 * ip;
    ed->prev[1] = 2 * in + 1;
    ed->face[0] = 0;
    ed->face[1] = 1;
    ed->vert[0] = i;
    ed->vert[1] = in;
  }
  gt3_mesh_all2mask(m, dmask, 0);
  gt3_all2sel(m, 0);
}

static void gt3_extrude(gt3_mesh *m, uint32_t dmask1, uint32_t dmask0, int32_t mode)
{
  int32_t i, e, ee, ec, fc, f, va, vb = 0;
  uint32_t i0, i1, *data;
  gt3_rec_begin(m, GT3_IM_ADDSCL);
  ec = m->n_edge;
  fc = m->n_face;
  (void)fc;
  for (i = 0; i < ec * 2; i++) {
    EDGE(i / 2).temp[i & 1] = -1;
    if (gt3_is_border_edge(m, i, mode)) {
      EDGE(i / 2).temp[i & 1] = 0;
      e = i;
      do {
        e = gt3_next_vert_edge(m, e);
      } while (!gt3_is_border_edge(m, e ^ 1, mode));
      va = gt3_add_copied_vert(m, gt3_vert_id(m, i));
      gt3_split_bridge(m, e, i, &va, &vb, dmask1, dmask0);
      const int32_t src[1] = { gt3_vert_id(m, i) };
      const float w[1] = { 1.0f };
      gt3_rec_addscale(m, va, 1, (int32_t)GT3_RM_ALL, src, w);
    }
  }
  for (i = 0; i < ec * 2; i++) {
    if (!EDGE(i / 2).temp[i & 1]) {
      i0 = (uint32_t)gt3_prev_face_edge(m, i);
      i1 = (uint32_t)gt3_next_face_edge(m, i);
      i1 = (uint32_t)gt3_next_face_edge(m, (int32_t)i1);
      f = m->n_face;
      gt3_split_face(m, (int32_t)i1, (int32_t)i0, dmask1, dmask0);
      FACE(f).select = 0;
    }
  }
  gt3_rec_end(m);

  if (mode == 3) {
    data = gt3_rec_begin(m, GT3_IM_F2VN);
    for (i = 0; i < m->n_face; i++) {
      if (FACE(i).select) {
        e = ee = FACE(i).edge;
        do {
          m->rec_op_count++;
          *data++ = (uint32_t)(gt3_vert_id(m, gt3_prev_face_edge(m, e)) * m->vert_size);
          *data++ = (uint32_t)(gt3_vert_id(m, e) * m->vert_size);
          *data++ = (uint32_t)(gt3_vert_id(m, gt3_next_face_edge(m, e)) * m->vert_size);
          e = gt3_next_face_edge(m, e);
        } while (e != ee);
      }
    }
    gt3_rec_end_at(m, data);
  }
}

static void gt3_subdivide(gt3_mesh *m, float alpha)
{
  int32_t i, j, k, count, e, ee, c, v, vv;
  int32_t va0, va1, vb0, vb1, va, vb = 0;
  int32_t ec, fc, ovc, s0, s1;
  uint32_t temp[64 * 2 + 2];
  float ftemp;

  gt3_rec_begin(m, GT3_IM_ADDSCL);
  ovc = m->n_vert;

  for (i = 0; i < m->n_vert; i++) {
    VERT(i).temp = -1;
    VERT(i).reindex = i;
  }

  /* centre positions */
  fc = m->n_face;
  for (i = 0; i < fc; i++) {
    FACE(i).temp = -1;
    FACE(i).temp2 = -1;
    if (FACE(i).select) {
      count = 0;
      ee = e = FACE(i).edge;
      do {
        v = gt3_vert_id(m, e);
        temp[count * 2] = (uint32_t)v;
        if (VERT(v).temp == -1) {
          vv = gt3_add_copied_vert(m, v);
          VERT(v).temp = vv;
          VERT(v).temp2 = e;
          VERT(v).reindex = vv;
        }
        e = gt3_next_face_edge(m, e);
        count++;
      } while (e != ee);

      c = gt3_add_new_vert(m);
      FACE(i).temp2 = c;
      VERT(c).select = 1;

      ftemp = 1.0f / (float)count;
      for (j = 0; j < count; j++) temp[j * 2 + 1] = gt3_f2u(ftemp);
      gt3_rec_addscale_array(m, c, (uint32_t)count, (int32_t)(GT3_RM_POS | GT3_RM_REST), temp);
    }
  }

  /* even vertices */
  alpha *= 4.0f;
  for (i = 0; i < ovc; i++) {
    if (VERT(i).temp != -1) {
      vv = VERT(i).temp;
      k = 0;
      ee = VERT(i).temp2;
      e = ee;
      ec = 0;
      c = -1;
      do {
        temp[k * 2] = (uint32_t)gt3_vert_id(m, e ^ 1);
        k++;
        if (GET_FACE(e).temp2 != -1) {
          temp[k * 2] = (uint32_t)GET_FACE(e).temp2;
          k++;
        } else
          c = e;
        if (gt3_vert_id(m, e) == i) ec |= EDGE(e / 2).crease;
        e = gt3_next_vert_edge(m, e);
      } while (ee != e);

      if (c == -1) {
        ftemp = alpha / (float)(k * k);
        for (j = 0; j < k; j++) temp[j * 2 + 1] = gt3_f2u(ftemp);
        temp[k * 2] = (uint32_t)i;
        ftemp = ((float)k - alpha) / (float)k;
        temp[k * 2 + 1] = gt3_f2u(ftemp);
        gt3_rec_addscale_array(m, vv, (uint32_t)(k + 1), (int32_t)(~(uint32_t)ec & ~(uint32_t)GT3_RM_NORMAL), temp);
        {
          const int32_t src[1] = { i };
          const float w[1] = { 1.0f };
          gt3_rec_addscale(m, vv, 1, (int32_t)((uint32_t)ec & ~(uint32_t)GT3_RM_NORMAL), src, w);
        }
      } else {
        va = gt3_vert_id(m, c ^ 1);
        do {
          c = gt3_next_vert_edge(m, c);
        } while (GET_FACE_I(c).temp2 != -1);
        vb = gt3_vert_id(m, c ^ 1);
        ftemp = alpha / 32.0f;
        const int32_t src[3] = { i, va, vb };
        const float w[3] = { 1.0f - 2.0f * ftemp, ftemp, ftemp };
        gt3_rec_addscale(m, vv, 3, (int32_t)(GT3_RM_POS | GT3_RM_REST), src, w);
      }
    }
  }

  /* split edges */
  alpha /= 16.0f;
  ftemp = 0.5f - alpha;
  ec = m->n_edge;
  for (i = 0; i < ec * 2; i += 2) {
    va0 = gt3_vert_id(m, i);
    va1 = gt3_vert_id(m, gt3_next_face_edge(m, i));
    vb1 = gt3_vert_id(m, i ^ 1);
    vb0 = gt3_vert_id(m, gt3_next_face_edge(m, i ^ 1));

    s0 = GET_FACE(i).select;
    s1 = GET_FACE_I(i).select;
    if (s0 || s1) {
      va = gt3_add_new_vert(m);
      gt3_split_bridge(m, gt3_next_vert_edge(m, i ^ 1), gt3_prev_vert_edge(m, i ^ 1), &va, &vb, 0x100000, 0);

      if (GET_FACE(i).temp2 == -1 || GET_FACE_I(i).temp2 == -1) {
        {
          const int32_t src[2] = { va0, va1 };
          const float w[2] = { 0.5f, 0.5f };
          gt3_rec_addscale(m, va, 2, (int32_t)(GT3_RM_POS | GT3_RM_REST), src, w);
        }
        if (va != vb) {
          const int32_t src[2] = { vb0, vb1 };
          const float w[2] = { 0.5f, 0.5f };
          gt3_rec_addscale(m, vb, 2, (int32_t)(GT3_RM_POS | GT3_RM_REST), src, w);
        }
      } else {
        const int32_t f0 = FACE(EDGE(i / 2).face[0]).temp2, f1 = FACE(EDGE(i / 2).face[1]).temp2;
        if (va != vb) {
          j = EDGE(i / 2).crease;
          {
            const int32_t src[4] = { va0, va1, f0, f1 };
            const float w[4] = { ftemp, ftemp, alpha, alpha };
            gt3_rec_addscale(m, va, 4, (int32_t)(~(uint32_t)j & ~(uint32_t)GT3_RM_NORMAL), src, w);
          }
          {
            const int32_t src[2] = { va0, va1 };
            const float w[2] = { 0.5f, 0.5f };
            gt3_rec_addscale(m, va, 2, (int32_t)((uint32_t)j & ~(uint32_t)GT3_RM_NORMAL), src, w);
          }
          {
            const int32_t src[4] = { vb0, vb1, f0, f1 };
            const float w[4] = { ftemp, ftemp, alpha, alpha };
            gt3_rec_addscale(m, vb, 4, (int32_t)(~(uint32_t)j & ~(uint32_t)GT3_RM_NORMAL), src, w);
          }
          {
            const int32_t src[2] = { vb0, vb1 };
            const float w[2] = { 0.5f, 0.5f };
            gt3_rec_addscale(m, vb, 2, (int32_t)((uint32_t)j & ~(uint32_t)GT3_RM_NORMAL), src, w);
          }
        } else {
          const int32_t src[4] = { va0, va1, f0, f1 };
          const float w[4] = { ftemp, ftemp, alpha, alpha };
          gt3_rec_addscale(m, va, 4, (int32_t)(GT3_RM_POS | GT3_RM_REST), src, w);
        }
      }

      if (s0) GET_FACE(i).temp = i;
      if (s1) GET_FACE_I(i).temp = gt3_prev_face_edge(m, i ^ 1);
    }
  }

  /* faces */
  fc = m->n_face;
  for (i = 0; i < fc; i++) {
    if (FACE(i).select && FACE(i).material != 0) {
      e = FACE(i).temp;
      count = 0;
      ee = e;
      int32_t tmp[64];
      do {
        ee = gt3_next_face_edge(m, ee);
        tmp[count++] = ee;
        ee = gt3_next_face_edge(m, ee);
      } while (e != ee);
      va = FACE(i).temp2;
      gt3_split_bridge(m, gt3_next_vert_edge(m, tmp[0]), tmp[0], &va, &vb, 0, 0);
      for (j = 1; j < count; j++) gt3_split_face(m, gt3_prev_face_edge(m, gt3_prev_face_edge(m, gt3_prev_face_edge(m, tmp[j]))), tmp[j], 0, 0);
    }
  }

  gt3_reindex(m);
  gt3_rec_end(m);
}

static void gt3_single_crease(gt3_mesh *m, int32_t i, uint32_t dmask1, uint32_t dmask0, int32_t what)
{
  if (!EDGE(i / 2).crease) {
    for (int32_t j = 0; j < 2; j++) {
      int32_t k = i ^ j;
      const int32_t v0 = gt3_vert_id(m, k);
      const int32_t vf = VERT(v0).first;
      int32_t got = 0, e = k;
      do {
        got |= EDGE(e / 2).crease;
        e = gt3_next_vert_edge(m, e);
      } while (e != k);
      if (got) {
        const int32_t v = gt3_add_copied_vert(m, v0);
        gt3_sel(&VERT(v).mask, dmask1 >> 16, dmask0 >> 16);
        VERT(v).first = vf;
        VERT(v0).next = v;
        {
          const int32_t src[1] = { v0 };
          const float w[1] = { 1.0f };
          gt3_rec_addscale(m, v, 1, (int32_t)GT3_RM_ALL, src, w);
        }
        do {
          EDGE(k / 2).vert[k & 1] = v;
          k = gt3_next_vert_edge(m, k);
        } while (!EDGE(k / 2).crease);
      }
    }
  }
  EDGE(i / 2).crease |= what;
}

/* a crease on the faces' border (selType 0); the masks of the destination are those of Sel() on edges, which the intro's build does not keep */
static void gt3_crease(gt3_mesh *m, int32_t sel_type, uint32_t dmask1, uint32_t dmask0, int32_t what)
{
  gt3_rec_begin(m, GT3_IM_ADDSCL);
  for (int32_t i = 0; i < m->n_edge * 2; i++) {
    int sel;
    if (sel_type == 0)
      sel = gt3_is_border_edge(m, i, 0);
    else if (sel_type == 1)
      sel = EDGE(i / 2).select;
    else
      sel = GET_FACE(i).select;
    if (sel) gt3_single_crease(m, i, dmask1, dmask0, what);
  }
  gt3_rec_end(m);
}

static void gt3_calcnormals(gt3_mesh *m, int32_t type, int32_t calcwhat)
{
  const int32_t msk = type ? 0 : GT3_GMF_NORMAL;
  uint32_t *data = gt3_rec_begin(m, GT3_IM_NORMAL);
  for (int32_t i = 0; i < m->n_vert; i++) VERT(i).temp = -1;
  for (int32_t i = 0; i < m->n_edge * 2; i++) VERT(gt3_vert_id(m, i)).temp = i;

  *data++ = (uint32_t)calcwhat;
  if (calcwhat) {
    for (int32_t i = 0; i < m->n_vert; i++) {
      int32_t count = 0, e, ee, pe;
      if (VERT(i).temp == -1) continue;
      e = ee = gt3_find_crease(m, VERT(i).temp, msk);
      int ok = 0;
      do {
        if (GET_FACE(e).select) ok = 1;
        data[++count + 1] = (uint32_t)(gt3_vert_id(m, e) * m->vert_size);
        data[++count + 1] = (uint32_t)(gt3_vert_id(m, e ^ 1) * m->vert_size);
        e = gt3_next_vert_edge(m, e);
        pe = e;
      } while (e != ee && !(EDGE(pe / 2).crease & msk));
      data[++count + 1] = (uint32_t)(gt3_vert_id(m, e) * m->vert_size);
      data[++count + 1] = (uint32_t)(gt3_vert_id(m, e ^ 1) * m->vert_size);
      if (ok) {
        m->rec_op_count++;
        *data = (uint32_t)(count / 2);
        data[1] = (uint32_t)(i * m->vert_size);
        data += count + 2;
      }
    }
  }
  gt3_rec_end_at(m, data);
}

static void gt3_triangulate(gt3_mesh *m, int32_t threshold, uint32_t dmask1, uint32_t dmask0, int32_t type)
{
  int32_t edgei[64];
  uint32_t temp[128];
  if (!type) gt3_rec_begin(m, GT3_IM_ADDSCL);
  for (int32_t i = 0; i < m->n_face; i++) {
    if (FACE(i).select) {
      int32_t e, ee, c = 0;
      e = ee = FACE(i).edge;
      do {
        edgei[c] = e;
        temp[c * 2] = (uint32_t)gt3_vert_id(m, e);
        e = gt3_next_face_edge(m, e);
        c++;
      } while (e != ee);
      if (c >= threshold) {
        if (!type) {
          int32_t v = gt3_add_new_vert(m);
          gt3_sel(&VERT(v).mask, dmask1 >> 16, dmask0 >> 16);
          const float ftemp = 1.0f / (float)c;
          for (int32_t j = 0; j < c; j++) {
            temp[j * 2 + 1] = gt3_f2u(ftemp);
            if (j == 0)
              gt3_split_bridge(m, gt3_next_vert_edge(m, edgei[0]), edgei[0], &v, &e, dmask1, dmask0);
            else
              gt3_split_face(m, gt3_prev_face_edge(m, gt3_prev_face_edge(m, edgei[j])), edgei[j], dmask1, dmask0);
          }
          gt3_rec_addscale_array(m, v, (uint32_t)c, (int32_t)GT3_RM_ALL, temp);
        } else {
          for (int32_t j = 0; j < c - 3; j++) gt3_split_face(m, gt3_prev_face_edge(m, gt3_prev_face_edge(m, edgei[j])), edgei[j], dmask1, dmask0);
        }
      }
    }
  }
  if (!type) gt3_rec_end(m);
}

/* ---- the natives ---- */

GT3_MESH_API int gt3_mesh_cube(gt3_mesh *m, int32_t tx, int32_t ty, int32_t tz, int32_t crease)
{
  int32_t tess[3] = { tx >> 16, ty >> 16, tz >> 16 };
  gt3_mat mat;
  float vc[3], vs[3];
  if (m->n_vert != 0) return 0;
  gt3_ring(m, 4, 0, 1.4142135623730950488016887242097f / 2, GT3_PI2F / 8);
  FACE(0).select = 1;

  gt3_extrude(m, 0x00010000, 0, 0);
  gt3_mat_init(&mat);
  mat.l.z = 1;
  gt3_mask2sel(m, 0x00010000);
  gt3_trans_vert(m, &mat, 0, 0);

  gt3_v_init4(&mat.l, 0.5f, 0.5f, 0.0f, 1.0f);
  gt3_all2sel(m, 1);
  gt3_trans_vert(m, &mat, 0, 0);
  gt3_mat_init(&mat);
  gt3_trans_vert(m, &mat, GT3_GMI_POS, GT3_GMI_UV0);

  /* extrude tessellation */
  vc[0] = vc[1] = vc[2] = 1;
  for (int j = 0; j < 3; j++) {
    vs[0] = vs[1] = vs[2] = 1025.0f;
    vs[j] = 0.1f;
    gt3_select_cube(m, vc, vs, 1, 0, 0);
    gt3_mat_init(&mat);
    (&mat.l.x)[j] = 1.0f;
    gt3_rec_begin(m, 0);
    for (int32_t i = 1; i < tess[j]; i++) {
      gt3_extrude(m, 0x00010000, 0x00000100, 0);
      gt3_face2vert(m);
      gt3_trans_vert(m, &mat, 0, 0);
      gt3_trans_vert(m, &mat, GT3_GMI_UV0, GT3_GMI_UV0);
    }
    gt3_rec_end(m);
  }

  /* rescale t0 0.5 .. -0.5 */
  gt3_mat_init(&mat);
  mat.i.x = 1.0f / (float)tess[0];
  mat.j.y = 1.0f / (float)tess[1];
  mat.k.z = 1.0f / (float)tess[2];
  gt3_all2sel(m, 1);
  gt3_trans_vert(m, &mat, GT3_GMI_UV0, GT3_GMI_UV0);
  gt3_v_init4(&mat.l, -0.5f, -0.5f, -0.5f, 1);
  gt3_trans_vert(m, &mat, 0, 0);

  /* mapping */
  if (crease) {
    for (int j = 0; j < 6; j++) {
      const int bm = (j & 1) ? 1 : -1;
      vc[0] = vc[1] = vc[2] = 0;
      vs[0] = vs[1] = vs[2] = 1025;
      vc[j / 2] = (float)bm * 0.5f;
      vs[j / 2] = 0.001f;
      gt3_select_cube(m, vc, vs, 1, 0, 0);
      gt3_crease(m, 0, 0, 0, 0xfe);
      gt3_face2vert(m);
      memset(&mat, 0, sizeof mat);
      mat.l.x = 0.5f;
      mat.l.y = 0.5f;
      if (j / 2 == 0) {
        mat.j.y = -1.0f;
        mat.k.x = 1.0f * (float)bm;
      } else if (j / 2 == 1) {
        mat.i.x = 1.0f;
        mat.k.y = -1.0f * (float)bm;
      } else {
        mat.i.x = -1.0f * (float)bm;
        mat.j.y = -1.0f;
      }
      gt3_trans_vert(m, &mat, GT3_GMI_POS, GT3_GMI_UV0);
    }
  } else {
    gt3_mesh_all2mask(m, 0, 0);
    gt3_mat_init(&mat);
    gt3_trans_vert(m, &mat, GT3_GMI_POS, GT3_GMI_UV0);
  }
  gt3_mesh_all2mask(m, 0, ~0u);
  return 1;
}

GT3_MESH_API void gt3_mesh_selectcube(gt3_mesh *m, const float center[3], const float size[3], int32_t flags, uint32_t dmask1, uint32_t dmask0)
{
  gt3_select_cube(m, center, size, flags >> 16, dmask1, dmask0);
}

GT3_MESH_API void gt3_mesh_subdivide(gt3_mesh *m, int32_t mask, float alpha, int32_t count)
{
  count >>= 16;
  if (mask)
    gt3_mask2sel(m, (uint32_t)mask << 8);
  else
    gt3_all2sel(m, 1);
  while (count--) gt3_subdivide(m, alpha);
}

GT3_MESH_API void gt3_mesh_transform(gt3_mesh *m, int32_t mask, const float srt[9])
{
  gt3_mat mat;
  gt3_mesh_mark_anim(m, GT3_GMA_TRANSFORM);
  gt3_mat_srt(&mat, srt);
  if (mask)
    gt3_mask2sel(m, (uint32_t)mask << 16);
  else
    gt3_all2sel(m, 1);
  gt3_trans_vert(m, &mat, 0, 0);
}

GT3_MESH_API void gt3_mesh_material(gt3_mesh *m, int32_t handle, int32_t id, int32_t mask)
{
  if (mask)
    gt3_mask2sel(m, (uint32_t)mask << 8);
  else
    gt3_all2sel(m, 1);
  gt3_set_material(m, id >> 16, handle);
}

GT3_MESH_API void gt3_mesh_calcnormals(gt3_mesh *m, int32_t mode, int32_t mask)
{
  if (mask)
    gt3_mask2sel(m, (uint32_t)mask << 8);
  else
    gt3_all2sel(m, 1);
  gt3_calcnormals(m, mode >> 16, 1);
}

GT3_MESH_API void gt3_mesh_triangulate(gt3_mesh *m, int32_t thres, int32_t mask, int32_t type)
{
  if (mask) gt3_mask2sel(m, (uint32_t)mask << 8);
  gt3_triangulate(m, thres >> 16, 0, 0, type >> 16);
}

GT3_MESH_API void gt3_mesh_perlin(gt3_mesh *m, int32_t mask, const float srt[9], const float ampl[3])
{
  gt3_mat mat, ampmat;
  gt3_mat_srt(&mat, srt);
  gt3_mesh_mark_anim(m, GT3_GMA_PERLIN);
  if (mask) gt3_mask2sel(m, (uint32_t)mask << 16);

  uint32_t *data = gt3_rec_begin(m, GT3_IM_PERLIN);
  *data++ = (uint32_t)gt3_rec_matrix(m, &mat);
  gt3_mat_init(&ampmat);
  gt3_v_init4(&ampmat.i, ampl[0], ampl[1], ampl[2], 0);
  gt3_rec_matrix(m, &ampmat);
  for (int32_t i = 0; i < m->n_vert; i++) {
    if (VERT(i).select) {
      *data++ = (uint32_t)i;
      m->rec_op_count++;
    }
  }
  gt3_rec_end_at(m, data);
}

GT3_MESH_API void gt3_mesh_selectrandom(gt3_mesh *m, uint32_t ratio, int32_t seed, uint32_t dmask1, uint32_t dmask0)
{
  gt3_rng *r = m->ctx->rng;
  seed >>= 16;
  ratio >>= 16;
  gt3_rng_seed(r, (int32_t)((uint32_t)seed + (uint32_t)seed * 31743u) ^ (int32_t)((uint32_t)seed << 23));

  int32_t i = m->n_vert;
  if (dmask0 & 0x80) i *= 2;
  i += m->n_edge;
  while (i > 0) {
    (void)gt3_rng_next(r);
    i--;
  }
  for (i = 0; i < m->n_face; i++) FACE(i).select = (gt3_rng_next(r) >> 24) <= ratio && FACE(i).material;
  gt3_sel2mask(m, dmask1, dmask0);
}

GT3_MESH_API void gt3_mesh_extrudewk(gt3_mesh *m, int32_t mask, int32_t mode, int32_t count, float distance, const float s[3], const float r[3],
                                     int32_t scalemode)
{
  int32_t i, j, grp, e, ee, mi;
  int32_t stk[4096];
  gt3_mat mat;

  gt3_mesh_mark_anim(m, GT3_GMA_EWK);
  if (mask)
    gt3_mask2sel(m, (uint32_t)mask << 8);
  else
    gt3_all2sel(m, 1);

  mode >>= 16;
  count >>= 16;
  const int local = !(scalemode >> 16);

  for (i = 0; i < m->n_face; i++) FACE(i).temp = local ? -1 : 0;

  grp = local ? 0 : 1;
  for (;;) {
    for (i = 0; i < m->n_face; i++)
      if (FACE(i).temp == -1 && FACE(i).select) break;
    if (i == m->n_face) break;

    stk[0] = i;
    j = 1;
    while (j) {
      i = stk[--j];
      FACE(i).temp = grp;
      e = ee = FACE(i).edge;
      do {
        i = gt3_face_id(m, e ^ 1);
        if (FACE(i).temp == -1 && !gt3_is_border_edge(m, e, mode)) {
          FACE(i).temp = -2;
          stk[j++] = i;
        }
        e = gt3_next_face_edge(m, e);
      } while (e != ee);
    }
    grp++;
  }

  gt3_rec_begin(m, 0);
  memset(&mat, 0, sizeof mat);
  gt3_v_init4(&mat.i, s[0], s[1], s[2], distance);
  gt3_v_init4(&mat.j, r[0], r[1], r[2], (float)count);
  mi = gt3_rec_matrix(m, &mat);

  while (count--) {
    gt3_extrude(m, 0, 0, mode);
    uint32_t *data = gt3_rec_begin(m, GT3_IM_EWK);
    m->rec_op_count = grp;
    *data++ = (uint32_t)local;
    *data++ = (uint32_t)mi;
    for (i = 0; i < m->n_vert; i++) VERT(i).temp = 0;
    for (i = 0; i < grp; i++) {
      j = 0;
      for (e = 0; e < m->n_edge * 2; e++) {
        if (GET_FACE(e).select && GET_FACE(e).temp == i && !GET_VERT(e).temp) {
          GET_VERT(e).temp = 1;
          data[++j] = (uint32_t)gt3_vert_id(m, e);
        }
      }
      data[0] = (uint32_t)j;
      data += j + 1;
    }
    gt3_rec_end_at(m, data);
  }
  gt3_rec_end(m);
}

GT3_MESH_API void gt3_mesh_beginrecord(gt3_mesh *m) { gt3_mesh_storemode(m); }

#undef VERT
#undef EDGE
#undef FACE
#undef GET_FACE
#undef GET_FACE_I
#undef GET_VERT

#endif /* GT3_MESH_IMPLEMENTATION_DONE */
#endif /* GT3_MESH_IMPLEMENTATION */
