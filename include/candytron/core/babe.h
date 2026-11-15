/* babe.h - the compact mesh format of the intro's character ("Babe", Josie) - header-only C99 (stb style)

   The decoder of genmesh.cpp (GenMesh::ReadCompact, MeshCoder::Decode) and the range coder of _types.cpp: the topology as a stream of face and vertex degrees, the vertex
   positions with a dither from the generator (sFGetRnd, so the result depends on how many numbers the script took before), skinning weights, and the bone keys.
   It builds a gt3_mesh of mesh.h. The input is checked: a damaged stream is refused, it does not run off the buffer.

   Needs mesh.h in the same translation unit with GT3_MESH_IMPLEMENTATION. In ONE .c file define GT3_BABE_IMPLEMENTATION; GT3_BABE_STATIC makes the function static. */
#ifndef GT3_BABE_H_INCLUDED
#define GT3_BABE_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#include <candytron/core/mesh.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GT3_BABE_API
#ifdef GT3_BABE_STATIC
#define GT3_BABE_API static
#else
#define GT3_BABE_API extern
#endif
#endif

/* Mesh_Babe's ReadCompact: builds `m` (new, empty) from `data`. Returns the number of bytes the mesh occupies, or 0 with the reason in `error`.
   The key stream comes in two generations of the coder: the source tree's (babe.mesh: quantisation 15, 31, 63, 127, 255) and the executable's (babe.mesh.old: 1, 3, 7,
   31, 127, thresholds 0, .01, .02, .04, .08); everything before the keys is the same. The one that ends exactly on the 'ryg' trailer is taken; `*format` (may be NULL)
   is 0 for the first, 1 for the executable's. */
GT3_BABE_API size_t gt3_babe_read(gt3_mesh *m, const uint8_t *data, size_t size, int *format, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* GT3_BABE_H_INCLUDED */

#ifdef GT3_BABE_IMPLEMENTATION
#ifndef GT3_BABE_IMPLEMENTATION_DONE
#define GT3_BABE_IMPLEMENTATION_DONE

#include <stdio.h>
#include <string.h>

#define GT3_RANGETOP    (1u << 24)
#define GT3_RANGEBOT    (1u << 16)
#define GT3_DV_LOWRANGE 9

typedef struct gt3_rcoder {
  const uint8_t *buf;
  size_t size, bytes;
  uint32_t low, code, range;
  int overrun;
} gt3_rcoder;

static uint8_t gt3_rc_byte(gt3_rcoder *c)
{
  if (c->bytes >= c->size) {
    c->overrun = 1;
    c->bytes++;
    return 0;
  }
  return c->buf[c->bytes++];
}

static void gt3_rc_init(gt3_rcoder *c, const uint8_t *buf, size_t size)
{
  c->buf = buf;
  c->size = size;
  c->bytes = 0;
  c->low = c->code = 0;
  c->range = ~0u;
  c->overrun = 0;
  for (int i = 0; i < 4; i++) c->code = (c->code << 8) + gt3_rc_byte(c);
}

static uint32_t gt3_rc_getfreq(gt3_rcoder *c, uint32_t totfreq)
{
  if (!totfreq) {
    c->overrun = 1;
    return 0;
  }
  c->range /= totfreq;
  if (!c->range) {
    c->overrun = 1;
    return 0;
  }
  return (c->code - c->low) / c->range;
}

static void gt3_rc_decode(gt3_rcoder *c, uint32_t cumfreq, uint32_t freq)
{
  c->low += cumfreq * c->range;
  c->range *= freq;
  while ((c->low ^ (c->low + c->range)) < GT3_RANGETOP || (c->range < GT3_RANGEBOT && ((c->range = (uint32_t)(-(int32_t)c->low) & (GT3_RANGEBOT - 1)), 1))) {
    c->code = (c->code << 8) + gt3_rc_byte(c);
    c->range <<= 8;
    c->low <<= 8;
    if (c->overrun) return;
  }
}

static uint32_t gt3_rc_plain(gt3_rcoder *c, uint32_t max)
{
  const uint32_t v = gt3_rc_getfreq(c, max);
  gt3_rc_decode(c, v, 1);
  return v;
}

static uint32_t gt3_rc_gamma(gt3_rcoder *c)
{
  uint32_t v = 1;
  int guard = 0;
  do {
    v = (v * 2) + gt3_rc_plain(c, 2);
    if (c->overrun || ++guard > 30) {
      c->overrun = 1;
      return 0;
    }
  } while (gt3_rc_plain(c, 2));
  return v - 2;
}

typedef struct gt3_rmodel {
  int32_t symbols, tot;
  int32_t *freq;
} gt3_rmodel;

static void gt3_rm_init(gt3_rmodel *r, int32_t symbols)
{
  r->symbols = symbols;
  r->freq = (int32_t *)malloc((size_t)symbols * 2 * sizeof(int32_t));
  if (!r->freq) abort();
  r->tot = symbols;
  for (int32_t i = 0; i < symbols; i++) {
    r->freq[i * 2 + 0] = i;
    r->freq[i * 2 + 1] = 1;
  }
}

static void gt3_rm_exit(gt3_rmodel *r)
{
  free(r->freq);
  r->freq = NULL;
}

static void gt3_rm_update(gt3_rmodel *r, int32_t symbol)
{
  r->freq[symbol * 2 + 1]++;
  r->tot++;
  for (int32_t i = symbol + 1; i < r->symbols; i++) r->freq[i * 2]++;
  if ((uint32_t)r->tot >= GT3_RANGEBOT) {
    int32_t j = 0;
    for (int32_t i = 0; i < r->symbols; i++) {
      r->freq[i * 2 + 0] = j;
      r->freq[i * 2 + 1] = (r->freq[i * 2 + 1] + 1) / 2;
      j += r->freq[i * 2 + 1];
    }
    r->tot = j;
  }
}

static int32_t gt3_rm_decode(gt3_rmodel *r, gt3_rcoder *c)
{
  const int32_t val = (int32_t)gt3_rc_getfreq(c, (uint32_t)r->tot);
  int32_t i;
  for (i = 0; i < r->symbols; i++) {
    const int32_t j = val - r->freq[i * 2 + 0];
    if (j >= 0 && j < r->freq[i * 2 + 1]) break;
  }
  if (i == r->symbols) {
    c->overrun = 1;
    return 0;
  }
  gt3_rc_decode(c, (uint32_t)r->freq[i * 2 + 0], (uint32_t)r->freq[i * 2 + 1]);
  gt3_rm_update(r, i);
  return i;
}

/* ---- the topology: faces and vertices as degrees, each one pointing at its neighbours ---- */

typedef struct gt3_me {
  int32_t degree, temp;
  int32_t *elem; /* the neighbours (indices in the other array), -1: not known yet */
} gt3_me;

typedef struct gt3_mcoder {
  gt3_me *face, *vert;
  int32_t face_ctr, vert_ctr, face_cap, vert_cap;
  gt3_mesh *mesh;
  int32_t face_count;
  gt3_rcoder coder;
  gt3_rmodel dv, dvflag;
  int32_t av_list[256], av_pos;
  int error;
} gt3_mcoder;

static void gt3_me_init(gt3_me *e, int32_t deg)
{
  e->degree = deg;
  e->elem = (int32_t *)malloc((size_t)deg * sizeof(int32_t));
  if (!e->elem) abort();
  for (int32_t i = 0; i < deg; i++) e->elem[i] = -1;
}

static int32_t gt3_me_find(const gt3_me *e, int32_t target, int adj, int *error)
{
  for (int32_t i = 0; i < e->degree; i++)
    if (e->elem[i] == target) return (i + adj + e->degree) % e->degree;
  *error = 1;
  return 0;
}

static int32_t gt3_mc_decode_face(gt3_mcoder *mc)
{
  int32_t d = gt3_rm_decode(&mc->dv, &mc->coder);
  if (!d) return -1;
  if (++d == GT3_DV_LOWRANGE) d += (int32_t)gt3_rc_gamma(&mc->coder);
  const int ok = !gt3_rm_decode(&mc->dvflag, &mc->coder);
  if (mc->coder.overrun || d < 2 || d > 64 || mc->face_ctr >= mc->face_cap) {
    mc->error = 1;
    return -1;
  }
  mc->face_count += ok;
  const int32_t f = mc->face_ctr++;
  gt3_me_init(&mc->face[f], d);
  mc->face[f].temp = ok;
  return f;
}

static void gt3_mc_add_face_to_vertex(gt3_mcoder *mc, int32_t f, int32_t i, int32_t v, int32_t j)
{
  gt3_me *fe = &mc->face[f], *ve = &mc->vert[v];
  fe->elem[i] = v;
  ve->elem[j] = f;
  for (int dir = -1; dir <= 1; dir += 2) {
    const int32_t fp = ve->elem[(j + dir + ve->degree) % ve->degree];
    const int32_t in = (i - dir + fe->degree) % fe->degree;
    if (fp >= 0 && fe->elem[in] < 0) {
      const gt3_me *pe = &mc->face[fp];
      fe->elem[in] = pe->elem[gt3_me_find(pe, v, dir, &mc->error)];
    }
  }
}

static void gt3_mc_activate_vd(gt3_mcoder *mc, int32_t f, int32_t i)
{
  int32_t val = gt3_rm_decode(&mc->dv, &mc->coder), v, j;
  if (val) { /* add */
    if (++val == GT3_DV_LOWRANGE) val += (int32_t)gt3_rc_gamma(&mc->coder);
    if (mc->coder.overrun || val < 2 || val > 64 || mc->vert_ctr >= mc->vert_cap || mc->av_pos >= 256) {
      mc->error = 1;
      return;
    }
    v = mc->vert_ctr++;
    gt3_me_init(&mc->vert[v], val);
    const int32_t vi = gt3_add_new_vert(mc->mesh);
    mc->vert[v].temp = vi;
    mc->av_list[mc->av_pos++] = v;
    if (gt3_rm_decode(&mc->dvflag, &mc->coder)) {
      j = (int32_t)gt3_rc_gamma(&mc->coder);
      if (j < 0 || j >= mc->mesh->n_vert) {
        mc->error = 1;
        return;
      }
      mc->mesh->vert[vi].next = mc->mesh->vert[j].next;
      mc->mesh->vert[vi].first = mc->mesh->vert[j].first;
      mc->mesh->vert[j].next = vi;
    }
    j = 0;
  } else { /* split */
    const uint32_t a = gt3_rc_gamma(&mc->coder);
    if (a >= (uint32_t)mc->av_pos) {
      mc->error = 1;
      return;
    }
    v = mc->av_list[a];
    j = (int32_t)gt3_rc_plain(&mc->coder, (uint32_t)mc->vert[v].degree);
  }
  if (mc->coder.overrun) {
    mc->error = 1;
    return;
  }
  gt3_mc_add_face_to_vertex(mc, f, i, v, j);
}

static int32_t gt3_mc_force_fv(gt3_mcoder *mc, int32_t f, int32_t j, int dir)
{
  const gt3_me *fe = &mc->face[f];
  int32_t v = fe->elem[0], i = 0;
  for (;;) {
    if (v < 0 || mc->error) return 0;
    i = (i + dir + fe->degree) % fe->degree;
    const gt3_me *ve = &mc->vert[v];
    const int32_t vt = ve->elem[(j - dir + ve->degree) % ve->degree];
    v = fe->elem[i];
    if (!i || vt < 0 || v < 0) break;
    j = gt3_me_find(&mc->vert[v], vt, -dir, &mc->error);
    gt3_mc_add_face_to_vertex(mc, f, i, v, j);
  }
  return i;
}

/* the topology, as half edges */
static int gt3_mc_decode(gt3_mcoder *mc, gt3_mesh *m, const uint8_t *p, size_t size, size_t *used)
{
  if (size < 2) return 0;
  const int32_t nverts = (int32_t)(p[0] | (p[1] << 8));
  gt3_rc_init(&mc->coder, p + 2, size - 2);
  gt3_rm_init(&mc->dv, GT3_DV_LOWRANGE);
  gt3_rm_init(&mc->dvflag, 2);
  mc->face_cap = 16384;
  mc->vert_cap = nverts;
  mc->face = (gt3_me *)calloc((size_t)mc->face_cap, sizeof(gt3_me));
  mc->vert = (gt3_me *)calloc((size_t)(nverts ? nverts : 1), sizeof(gt3_me));
  if (!mc->face || !mc->vert) abort();
  mc->mesh = m;
  m->n_face = 0;

  for (;;) {
    int32_t f = gt3_mc_decode_face(mc);
    if (f < 0) break;
    for (int32_t i = 0; i < mc->face[f].degree && !mc->error; i++) gt3_mc_activate_vd(mc, f, i);

    while (mc->av_pos > 0 && !mc->error) {
      int32_t j = 64, vi = 0;
      for (int32_t i = 0; i < mc->av_pos; i++) {
        const gt3_me *v = &mc->vert[mc->av_list[i]];
        int32_t k = 0;
        for (int32_t d = 0; d < v->degree; d++)
          if (v->elem[d] < 0) k++;
        if (k < j) {
          j = k;
          vi = i;
        }
      }
      const int32_t v = mc->av_list[vi];
      for (j = 0; j < mc->vert[v].degree && !mc->error; j++) {
        if (mc->vert[v].elem[j] < 0) {
          f = gt3_mc_decode_face(mc);
          if (f < 0) {
            mc->error = 1;
            break;
          }
          gt3_mc_add_face_to_vertex(mc, f, 0, v, j);
          int32_t k = gt3_mc_force_fv(mc, f, j, 1);
          const int32_t i = gt3_mc_force_fv(mc, f, j, -1);
          if (k) {
            while (k <= i && !mc->error) gt3_mc_activate_vd(mc, f, k++);
          }
        }
      }
      mc->av_list[vi] = mc->av_list[--mc->av_pos];
    }
    if (mc->error || mc->coder.overrun) break;
  }

  gt3_rm_exit(&mc->dv);
  gt3_rm_exit(&mc->dvflag);
  if (mc->error || mc->coder.overrun) return 0;
  *used = 2 + mc->coder.bytes;

  GT3_ENSURE(m, face, n_face, cap_face, mc->face_count);
  m->n_face = mc->face_count;
  int32_t fc = 0;
  for (int32_t fi = 0; fi < mc->face_ctr; fi++) {
    const gt3_me *f = &mc->face[fi];
    int32_t edges[66];
    if (f->temp) {
      if (fc >= m->n_face) return 0;
      gt3_mface_init(&m->face[fc]);
      m->face[fc].material = 1;
      const int32_t d = f->degree;
      for (int32_t j = 0; j < d; j++)
        if (f->elem[j] < 0) return 0;
      int32_t f0 = mc->vert[f->elem[d - 1]].temp, f1;
      for (int32_t j = 0; j < d; j++) {
        f1 = f0;
        f0 = mc->vert[f->elem[j]].temp;
        edges[j + 1] = gt3_add_edge(m, f1, f0, fc);
      }
      edges[0] = edges[d];
      edges[d + 1] = edges[1];
      for (int32_t j = 0; j < d; j++) {
        const int32_t k = edges[j + 1];
        m->edge[k / 2].next[k & 1] = edges[j + 2];
        m->edge[k / 2].prev[k & 1] = edges[j];
      }
      fc++;
    }
  }
  return 1;
}

static void gt3_mc_free(gt3_mcoder *mc)
{
  for (int32_t i = 0; i < mc->face_ctr; i++) free(mc->face[i].elem);
  for (int32_t i = 0; i < mc->vert_ctr; i++) free(mc->vert[i].elem);
  free(mc->face);
  free(mc->vert);
}

/* ---- the whole mesh ---- */

static float gt3_babe_fget(const uint8_t *ptr)
{
  const uint32_t v = (uint32_t)(ptr[0] | (ptr[1] << 8));
  const uint32_t vd = (v & 32768u) << 16 | ((((v >> 10) & 31u) + 128u - 16u) << 23) | ((v & 1023u) << 13);
  float f;
  memcpy(&f, &vd, 4);
  return f;
}

static float gt3_sfgetrnd(gt3_rng *r) { return ((float)(gt3_rng_next(r) & 0x3fffffffu) * 1.0f) / 1073741824.0f; }

static void gt3_babe_fail(char *error, size_t n, const char *what)
{
  if (error && n) snprintf(error, n, "%s", what);
}

/* the key coder: per curve the header (minimum, maximum), then one symbol per key from a range model whose size depends on the curve's range. Returns 1 when the stream
   decodes and the 'ryg' trailer follows it; `*used` is then the number of bytes from `p` (the curve headers) to the end of the trailer. */
static int gt3_babe_keys(gt3_mesh *m, const uint8_t *p, const uint8_t *end, int set, size_t *used)
{
  static const int32_t anim_quantize[2][5] = { { 15, 31, 63, 127, 255 }, { 1, 3, 7, 31, 127 } };
  static const float anim_threshold[2][5] = { { 0.0f, 0.016f, 0.03f, 0.06f, 0.10f }, { 0.0f, 0.01f, 0.02f, 0.04f, 0.08f } };
  const uint8_t *start = p;
  gt3_rcoder coder;
  gt3_rmodel vmodel[5];
  if ((size_t)(end - p) < (size_t)m->curve_count * 4u) return 0;
  gt3_rc_init(&coder, p + (size_t)m->curve_count * 4u, (size_t)(end - p) - (size_t)m->curve_count * 4u);
  for (int i = 0; i < 5; i++) gt3_rm_init(&vmodel[i], anim_quantize[set][i] * 2 + 2);
  for (int32_t i = 0; i < m->curve_count && !coder.overrun; i++) {
    const float fmin = gt3_babe_fget(p + 0), fmax = gt3_babe_fget(p + 2);
    int32_t sc = 0, nsc = 0;
    p += 4;
    for (int j = 0; j < 5; j++) {
      if (fmax >= anim_threshold[set][j]) {
        sc = anim_quantize[set][j];
        nsc = j;
      }
    }
    float *fp = &m->key_buf[i * m->key_count];
    *fp++ = fmin;
    int32_t k = 0;
    for (int32_t j = 1; j < m->key_count; j++) {
      k += gt3_rm_decode(&vmodel[nsc], &coder);
      if (k & (sc + 1))
        k |= ~sc;
      else
        k &= sc;
      *fp++ = (float)k * fmax / (float)sc + fmin;
    }
  }
  p += coder.bytes;
  for (int i = 0; i < 5; i++) gt3_rm_exit(&vmodel[i]);
  if (coder.overrun || end - p < 3 || p[0] != 'r' || p[1] != 'y' || p[2] != 'g') return 0;
  *used = (size_t)(p + 3 - start);
  return 1;
}

GT3_BABE_API size_t gt3_babe_read(gt3_mesh *m, const uint8_t *data, size_t size, int *format, char *error, size_t error_size)
{
  static const int32_t vert_quantize[3] = { 319, 639, 191 };
  gt3_mcoder mc;
  gt3_rcoder coder;
  gt3_rmodel vmodel[5];
  size_t used = 0;
  const uint8_t *p = data, *end = data + size;
  int32_t vl[4];
  int models = 0;

  memset(&mc, 0, sizeof mc);
  if (size < 6) {
    gt3_babe_fail(error, error_size, "the mesh is too short");
    return 0;
  }
  m->key_count = (int32_t)(p[0] | (p[1] << 8));
  m->curve_count = p[2];
  const int32_t bc = p[3];
  const uint32_t sgmf = (uint32_t)(p[4] | (p[5] << 8));
  p += 6;
  if (bc < 1 || m->key_count < 1) {
    gt3_babe_fail(error, error_size, "no bones or no keys");
    return 0;
  }

  gt3_mesh_init(m, 0x23, 64 * 1024);
  m->bone = (gt3_mbone *)calloc((size_t)bc, sizeof *m->bone);
  m->n_bone = bc;
  m->curve = (gt3_mcurve *)calloc((size_t)(m->curve_count ? m->curve_count : 1), sizeof *m->curve);
  m->n_curve = m->curve_count;
  m->key_buf = (float *)calloc((size_t)m->key_count * (size_t)(m->curve_count ? m->curve_count : 1), sizeof(float));
  if (!m->bone || !m->curve || !m->key_buf) abort();

  if (!gt3_mc_decode(&mc, m, p, (size_t)(end - p), &used)) {
    gt3_babe_fail(error, error_size, "the topology is damaged");
    gt3_mc_free(&mc);
    return 0;
  }
  gt3_mc_free(&mc);
  p += used;

  /* skinning */
  const int32_t nv = m->n_vert;
  if ((size_t)(end - p) < (size_t)nv * 2u) goto damaged;
  for (int32_t i = 0; i < nv; i++) {
    m->vert[i].temp = 1 + *p++;
    if (m->vert[i].temp > 4) goto damaged;
  }
  for (int i = 0; i < 4; i++) vl[i] = 0;
  for (int32_t i = 0; i < nv; i++)
    for (int32_t j = 0; j < m->vert[i].temp; j++) {
      if (p >= end) goto damaged;
      vl[j] = (vl[j] + *p++) & 0xff;
      m->vert[i].matrix[j] = (uint8_t)vl[j];
    }
  for (int32_t i = 0; i < nv; i++) {
    if (m->vert[i].temp > 0) {
      int32_t k = 0xff;
      for (int32_t j = 0; j < m->vert[i].temp - 1; j++) {
        if (p >= end) goto damaged;
        k -= *p;
        m->vert[i].weight[j] = *p++;
      }
      m->vert[i].weight[m->vert[i].temp - 1] = (uint8_t)k;
    }
  }

  /* vertex positions */
  float vdim[6];
  if ((size_t)(end - p) < 24) goto damaged;
  memcpy(vdim, p, 24);
  p += 24;
  gt3_vec *vp = m->vbuf;
  gt3_rc_init(&coder, p, (size_t)(end - p));
  for (int i = 0; i < 3; i++) {
    gt3_rm_init(&vmodel[i], vert_quantize[i] + 1);
    vl[i] = 0;
  }
  gt3_rm_init(&vmodel[3], 2);
  models = 4;

  for (int32_t i = 0; i < nv; i++) {
    gt3_vec v0;
    if (sgmf & GT3_GMF_POS) {
      if (m->vert[i].first == i) {
        if (gt3_rm_decode(&vmodel[3], &coder)) {
          const int32_t k = (int32_t)gt3_rc_plain(&coder, (uint32_t)i);
          if (k >= i) goto damaged;
          v0 = m->vbuf[k * m->vert_size];
          v0.x = -v0.x;
        } else {
          for (int j = 0; j < 3; j++) {
            vl[j] += gt3_rm_decode(&vmodel[j], &coder);
            if (vl[j] > vert_quantize[j]) vl[j] -= vert_quantize[j] + 1;
            (&v0.x)[j] = ((float)vl[j] + gt3_sfgetrnd(m->ctx->rng) - 0.5f) * vdim[j + 3] + vdim[j];
          }
        }
        v0.w = 1;
      } else
        v0 = m->vbuf[m->vert[i].first * m->vert_size];
    } else
      gt3_v_init4(&v0, 0, 0, 0, 1);
    if (coder.overrun) goto damaged;

    *vp++ = v0;
    vp += 2;
    gt3_v_init4(&vp[-1], v0.x * 0.1f, v0.y * 0.1f, 0, 0);
  }
  p += coder.bytes;
  for (int i = 0; i < models; i++) gt3_rm_exit(&vmodel[i]);
  models = 0;

  /* keys */
  {
    float fmin[9], fmax[9];
    const size_t keys_fixed = (size_t)bc + 36u + (size_t)bc * 18u + (size_t)m->curve_count * 2u;
    if ((size_t)(end - p) < keys_fixed) goto damaged;

    for (int32_t i = 0; i < bc; i++) m->bone[i].parent = i - 1 - *p++;
    for (int j = 0; j < 9; j++) {
      fmin[j] = gt3_babe_fget(p);
      fmax[j] = gt3_babe_fget(p + 2);
      p += 4;
    }
    for (int j = 0; j < 9; j++) {
      for (int32_t i = 0; i < bc; i++) m->bone[i].base_srt[j] = (float)(*p++) * (fmax[j] - fmin[j]) / 255 + fmin[j];
      for (int32_t i = 0; i < bc; i++) m->bone[i].trans_srt[j] = (float)(*p++) * (fmax[j] - fmin[j]) / 255 + fmin[j];
    }
    for (int32_t i = 0; i < m->n_curve; i++) m->curve[i].curve = *p++;
    vl[0] = 0;
    for (int32_t i = 0; i < m->n_curve; i++) {
      vl[0] = (vl[0] + *p++) & 0xff;
      m->curve[i].matrix = vl[0];
      if (vl[0] >= bc || m->curve[i].curve >= 9) goto damaged;
    }
    for (int32_t i = 0; i < bc; i++)
      if (m->bone[i].parent < 0 && i > 0) goto damaged;
  }

  /* the key stream: the executable's tables or the source tree's, whichever ends on the trailer */
  for (int set = 0; set < 2; set++) {
    size_t used_keys = 0;
    if (gt3_babe_keys(m, p, end, set, &used_keys)) {
      if (format) *format = set;
      return (size_t)(p - data) + used_keys;
    }
  }
  gt3_babe_fail(error, error_size, "the key stream matches neither coder, or the 'ryg' trailer is not where it ends");
  return 0;

damaged:
  for (int i = 0; i < models; i++) gt3_rm_exit(&vmodel[i]);
  gt3_babe_fail(error, error_size, "the mesh is damaged");
  return 0;
}

#endif /* GT3_BABE_IMPLEMENTATION_DONE */
#endif /* GT3_BABE_IMPLEMENTATION */
