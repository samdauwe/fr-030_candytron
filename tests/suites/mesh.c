/* Checks for mesh.h and babe.h: the half-edge structure, the construction operations, the recorder and the compact mesh format, on meshes small enough to work out by hand
 * (the whole generation is compared with the oracle by the suite csl_trace: every mesh result of the script, 240 of them, by topology and by vertex buffer).
 *
 *   suite_mesh <assets dir> <mesh fixtures dir>
 */
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/check.h"
#include "support/suite.h"

#define GT3_BITMAP_STATIC
#define GT3_BITMAP_IMPLEMENTATION
#define GT3_MESH_STATIC
#define GT3_MESH_IMPLEMENTATION
#define GT3_BABE_STATIC
#define GT3_BABE_IMPLEMENTATION
#include <candytron/core/babe.h>

static uint8_t *read_file(const char *dir, const char *name, size_t *size)
{
  char path[2048];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = (uint8_t *)malloc((size_t)n);
  if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) {
    free(data);
    data = NULL;
  } else
    *size = (size_t)n;
  fclose(f);
  return data;
}

/* GenMesh::Verify: every face is a closed loop of its own edges, forwards and backwards, every edge knows both faces, the vertices of a corner share a position */
static int verify(const gt3_mesh *m, const char **why)
{
  for (int32_t i = 0; i < m->n_face; i++) {
    if (!m->face[i].material) continue;
    int32_t e = m->face[i].edge, n = 0;
    if (e < 0 || e >= m->n_edge * 2) return *why = "face edge out of range", 0;
    const int32_t first = e;
    do {
      if (gt3_face_id(m, e) != i) return *why = "face loop leaves its face", 0;
      if (gt3_prev_face_edge(m, gt3_next_face_edge(m, e)) != e) return *why = "next and prev are not inverse", 0;
      e = gt3_next_face_edge(m, e);
      if (++n > 64) return *why = "face loop does not close", 0;
    } while (e != first);
  }
  for (int32_t i = 0; i < m->n_edge; i++) {
    for (int d = 0; d < 2; d++) {
      const int32_t k = i * 2 + d;
      if (m->edge[i].face[d] < 0 || m->edge[i].face[d] >= m->n_face) return *why = "edge face out of range", 0;
      if (m->edge[i].vert[d] < 0 || m->edge[i].vert[d] >= m->n_vert) return *why = "edge vertex out of range", 0;
      const int32_t nx = gt3_next_face_edge(m, k);
      if (m->vert[gt3_vert_id(m, nx ^ 1)].first != m->vert[gt3_vert_id(m, k ^ 1)].first &&
          m->vert[gt3_vert_id(m, nx)].first != m->vert[gt3_vert_id(m, k ^ 1)].first)
        return *why = "consecutive edges do not meet", 0;
    }
  }
  for (int32_t i = 0; i < m->n_vert; i++) {
    if (m->vert[i].next < 0 || m->vert[i].next >= m->n_vert) return *why = "vertex next out of range", 0;
    if (m->vert[m->vert[i].first].first != m->vert[i].first) return *why = "first is not a root", 0;
  }
  return 1;
}

static gt3_mesh *new_cube(gt3_mesh_ctx *c, int32_t t, int32_t crease)
{
  gt3_mesh *m = gt3_mesh_create(c);
  gt3_mesh_new(m);
  gt3_mesh_cube(m, t << 16, t << 16, t << 16, crease);
  return m;
}

static int count_faces(const gt3_mesh *m, int degree)
{
  int n = 0;
  for (int32_t i = 0; i < m->n_face; i++) {
    int32_t e = m->face[i].edge, d = 0;
    const int32_t first = e;
    do {
      e = gt3_next_face_edge(m, e);
      d++;
    } while (e != first);
    n += d == degree;
  }
  return n;
}

static int same_floats(const gt3_vec *a, const gt3_vec *b, size_t n)
{
  for (size_t i = 0; i < n; i++)
    if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z || a[i].w != b[i].w) return 0;
  return 1;
}

static void check_cube(gt3_mesh_ctx *c)
{
  const char *why = "";
  /* the surface of a grid of t x t x t cubes: (t + 1)^3 - (t - 1)^3 vertices, 6 t^2 quads, twice as many edges as faces */
  static const int32_t verts[3] = { 8, 26, 56 }, edges[3] = { 12, 48, 108 }, faces[3] = { 6, 24, 54 };
  for (int t = 1; t <= 3; t++) {
    gt3_mesh *m = new_cube(c, t, 0);
    CHECK(m->n_vert == verts[t - 1] && m->n_edge == edges[t - 1] && m->n_face == faces[t - 1], "Cube(%d): %d vertices, %d edges, %d faces", t, (int)m->n_vert,
          (int)m->n_edge, (int)m->n_face);
    CHECK(m->n_vert - m->n_edge + m->n_face == 2, "Cube(%d): Euler characteristic %d", t, (int)(m->n_vert - m->n_edge + m->n_face));
    CHECK(verify(m, &why), "Cube(%d) is a consistent half-edge mesh: %s", t, why);
    CHECK(count_faces(m, 4) == m->n_face, "Cube(%d): all faces are quads", t);
    CHECK(m->vert_size == 3 && m->vert_mask == 0x23, "vertex layout");
    gt3_mesh_destroy(m);
  }
  /* the cube spans -0.5 .. 0.5 and the uv of the positions are the positions of the cube's mapping */
  gt3_mesh *m = new_cube(c, 2, 0);
  float lo = 1e9f, hi = -1e9f;
  for (int32_t i = 0; i < m->vert_count; i++) {
    const gt3_vec *p = &m->vbuf[i * 3];
    lo = fminf(lo, fminf(p->x, fminf(p->y, p->z)));
    hi = fmaxf(hi, fmaxf(p->x, fmaxf(p->y, p->z)));
    CHECK(p->w == 1.0f, "w of vertex %d is %g", (int)i, (double)p->w);
  }
  CHECK(fabsf(lo + 0.5f) < 1e-5f && fabsf(hi - 0.5f) < 1e-5f, "extent %g .. %g", (double)lo, (double)hi);
  gt3_mesh_destroy(m);

  /* a crease splits the vertices of the corners: more vertices, the same faces */
  m = new_cube(c, 1, 1);
  CHECK(m->n_vert == 24 && m->n_face == 6, "Cube(1) with creases: %d vertices, %d faces", (int)m->n_vert, (int)m->n_face);
  CHECK(verify(m, &why), "creased cube is consistent: %s", why);
  gt3_mesh_destroy(m);
}

static void check_subdivide(gt3_mesh_ctx *c)
{
  const char *why = "";
  gt3_mesh *m = new_cube(c, 2, 0);
  const int32_t v = m->n_vert, e = m->n_edge, f = m->n_face;
  gt3_mesh_subdivide(m, 0, 1.0f, 1 << 16);
  /* every quad becomes four: the old vertices stay (as the sources of the new ones) beside their moved copies, the edge points and the face points: 2V + E + F vertices,
     2E + 4F edges, 4F faces (the character: 2 * 954 + 1855 + 970 = 4733) */
  CHECK(m->n_face == 4 * f && m->n_vert == 2 * v + e + f && m->n_edge == 2 * e + 4 * f, "one level: %d vertices, %d edges, %d faces", (int)m->n_vert,
        (int)m->n_edge, (int)m->n_face);
  CHECK(verify(m, &why), "subdivided cube is consistent: %s", why);
  gt3_mesh_subdivide(m, 0, 1.0f, 2 << 16);
  CHECK(m->n_face == 4 * 4 * 4 * f, "three levels: %d faces", (int)m->n_face);
  CHECK(verify(m, &why), "three levels are consistent: %s", why);
  /* alpha 1: the limit surface of a cube is inside it and round: the corner moved in, no vertex is outside the cube */
  float far = 0;
  for (int32_t i = 0; i < m->vert_count; i++) far = fmaxf(far, fmaxf(fabsf(m->vbuf[i * 3].x), fmaxf(fabsf(m->vbuf[i * 3].y), fabsf(m->vbuf[i * 3].z))));
  CHECK(far <= 0.5f + 1e-5f, "the subdivision stays inside the cube (%g)", (double)far);
  gt3_mesh_destroy(m);

  /* triangulation without a centre point splits a quad in two triangles */
  m = new_cube(c, 1, 0);
  gt3_mesh_subdivide(m, 0, 0.0f, 0); /* selects everything, 0 levels */
  gt3_mesh_triangulate(m, 4 << 16, 0, 1 << 16);
  CHECK(m->n_face == 12 && count_faces(m, 3) == 12, "quads to triangles: %d faces", (int)m->n_face);
  CHECK(verify(m, &why), "triangulated cube is consistent: %s", why);
  gt3_mesh_destroy(m);
  m = new_cube(c, 1, 0);
  gt3_mesh_subdivide(m, 0, 0.0f, 0);
  gt3_mesh_triangulate(m, 4 << 16, 0, 0); /* with a centre point: 4 triangles per quad */
  CHECK(m->n_face == 24 && m->n_vert == 14, "with centres: %d faces, %d vertices", (int)m->n_face, (int)m->n_vert);
  gt3_mesh_destroy(m);
}

static void check_normals(gt3_mesh_ctx *c)
{
  gt3_mesh *m = new_cube(c, 1, 1); /* creased: the faces keep their own normals */
  gt3_mesh_calcnormals(m, 0, 0);
  int ok = 1, side = 0;
  for (int32_t i = 0; i < m->vert_count; i++) {
    const gt3_vec *p = &m->vbuf[i * 3], *n = &m->vbuf[i * 3 + 1];
    const float len = sqrtf(n->x * n->x + n->y * n->y + n->z * n->z);
    /* a face normal of the cube lies along one axis, and on the same side of the surface for every face */
    const float dot = n->x * p->x + n->y * p->y + n->z * p->z;
    if (!side) side = dot > 0 ? 1 : -1;
    ok &= fabsf(len - 1.0f) < 1e-4f && (dot > 0 ? 1 : -1) == side && fabsf(fabsf(dot) - 0.5f) < 1e-4f;
  }
  CHECK(ok, "the normals of a creased cube are unit vectors along the axes, all on the same side");
  gt3_mesh_destroy(m);
}

/* the recorder: a mesh in store mode replays its commands from the vertex buffer it had, and with the labels at their defaults gives the buffer it made */
static void check_recorder(gt3_mesh_ctx *c)
{
  gt3_mesh *m = new_cube(c, 2, 0);
  gt3_mesh_beginrecord(m);
  CHECK(m->store_mode && m->n_rec_vert == m->vert_count * m->vert_size, "store mode keeps the vertices");

  const float srt[9] = { 1.5f, 0.5f, 1.0f, 0.1f, 0.0f, 0.2f, 0.25f, 0.0f, -1.0f };
  gt3_mesh_all2mask(m, 0, 0); /* selects everything */
  gt3_mesh_transform(m, 0, srt);
  gt3_mesh_animlabel(m, 7, 0);
  const float amp[3] = { 0.05f, 0.05f, 0.05f }, psrt[9] = { 1, 1, 1, 0, 0, 0, 0.3f, 0.2f, 0.1f };
  gt3_mesh_perlin(m, 0, psrt, amp);
  gt3_mesh_animlabel(m, 8, 0);
  gt3_mesh_calcnormals(m, 0, 0);
  CHECK(m->n_anim == 2 && m->anim[0].operation == GT3_GMA_TRANSFORM && m->anim[1].operation == GT3_GMA_PERLIN, "two labels, the operations marked before them");

  const size_t n = (size_t)m->vert_count * (size_t)m->vert_size;
  gt3_vec *before = (gt3_vec *)malloc(n * sizeof(gt3_vec));
  memcpy(before, m->vbuf, n * sizeof(gt3_vec));
  int32_t members[2 * 13] = { 0 };
  for (int k = 0; k < 2; k++)
    for (int j = 0; j < 3; j++) members[k * 13 + j] = 0x10000; /* s = 1, the rest 0 */
  /* the labels' default does not reproduce the script's own SRT: Transform's label multiplies the recorded matrix by identity and Perlin's adds nothing */
  gt3_mesh_replay(m, members);
  CHECK(same_floats(before, m->vbuf, n), "replay with the default labels reproduces the buffer");

  /* moving the label moves the vertices: s = 2 on the Transform label doubles the extent of the transformed cube */
  float extent0 = 0;
  for (int32_t i = 0; i < m->vert_count; i++) extent0 = fmaxf(extent0, fabsf(m->vbuf[i * 3].x));
  members[0] = 0x20000;
  gt3_mesh_replay(m, members);
  float extent1 = 0;
  for (int32_t i = 0; i < m->vert_count; i++) extent1 = fmaxf(extent1, fabsf(m->vbuf[i * 3].x));
  CHECK(extent1 > extent0 * 1.5f, "the label's scale moves the mesh (%g to %g)", (double)extent0, (double)extent1);
  free(before);

  /* a copy replays like the original */
  gt3_mesh *copy = gt3_mesh_copy(m);
  members[0] = 0x10000;
  gt3_mesh_replay(m, members);
  gt3_mesh_replay(copy, members);
  CHECK(same_floats(m->vbuf, copy->vbuf, n), "a copy replays the same");
  CHECK(copy->n_vert == m->n_vert && copy->n_edge == m->n_edge && copy->n_face == m->n_face &&
          !memcmp(copy->edge, m->edge, (size_t)m->n_edge * sizeof *m->edge),
        "a copy has the same topology");
  gt3_mesh_destroy(copy);
  gt3_mesh_destroy(m);
}

static void check_select(gt3_mesh_ctx *c)
{
  gt3_mesh *m = new_cube(c, 2, 0);
  const float ctr[3] = { 0.5f, 0, 0 }, size[3] = { 0.1f, 1.0f, 1.0f };
  gt3_mesh_all2mask(m, 0, ~0u);
  gt3_mesh_selectcube(m, ctr, size, 1 << 16, 0x10000, 0); /* the +x side: 9 vertices (a 3 x 3 grid), 4 faces */
  int nv = 0, nf = 0;
  for (int32_t i = 0; i < m->n_vert; i++) nv += m->vert[i].select;
  for (int32_t i = 0; i < m->n_face; i++) nf += m->face[i].select;
  CHECK(nv == 9 && nf == 4, "SelectCube picks the side: %d vertices, %d faces", nv, nf);
  gt3_mesh_destroy(m);

  /* SelectRandom is a function of the seed: the same seed, the same faces, another seed another set; ratio 255 selects all */
  gt3_rng a = { 0x1234u }, b = { 0x5678u };
  gt3_mesh_ctx *ca = gt3_mesh_ctx_create(&a, c->perlin), *cb = gt3_mesh_ctx_create(&b, c->perlin);
  gt3_mesh *m1 = new_cube(ca, 2, 0), *m2 = new_cube(cb, 2, 0), *m3 = new_cube(ca, 2, 0);
  gt3_mesh_selectrandom(m1, 0x80u << 16, 5 << 16, 0x100, 0);
  gt3_mesh_selectrandom(m2, 0x80u << 16, 5 << 16, 0x100, 0);
  gt3_mesh_selectrandom(m3, 0x80u << 16, 6 << 16, 0x100, 0);
  int same12 = 1, same13 = 1;
  for (int32_t i = 0; i < m1->n_face; i++) {
    same12 &= m1->face[i].mask == m2->face[i].mask;
    same13 &= m1->face[i].mask == m3->face[i].mask;
  }
  CHECK(same12 && !same13, "the selection depends on the seed only");
  gt3_mesh_selectrandom(m1, 0xffu << 16, 5 << 16, 0x200, 0);
  int all = 1;
  for (int32_t i = 0; i < m1->n_face; i++) all &= (m1->face[i].mask & 0x2) != 0;
  CHECK(all, "ratio 255 selects every face");
  gt3_mesh_destroy(m1);
  gt3_mesh_destroy(m2);
  gt3_mesh_destroy(m3);
  gt3_mesh_ctx_destroy(ca);
  gt3_mesh_ctx_destroy(cb);
}

/* the character: both generations of the file decode to the same topology and the same vertices, with the keys of their own */
static void check_babe(gt3_mesh_ctx *c, const char *assets, const char *fixtures)
{
  size_t n_new = 0, n_old = 0;
  uint8_t *fresh = read_file(fixtures, "babe.mesh", &n_new);
  uint8_t *old = read_file(assets, "babe.mesh.old", &n_old);
  CHECK(fresh && old, "the two files are there");
  if (!fresh || !old) {
    free(fresh);
    free(old);
    return;
  }
  const char *why = "";
  char error[200] = "";
  int format = -1;
  gt3_mesh *a = gt3_mesh_create(c), *b = gt3_mesh_create(c);
  size_t used_a = gt3_babe_read(a, fresh, n_new, &format, error, sizeof error);
  CHECK(used_a == n_new && format == 0, "the source tree's file: %zu of %zu bytes, format %d (%s)", used_a, n_new, format, error);
  CHECK(a->n_vert == 954 && a->n_edge == 1855 && a->n_face == 970, "954 vertices, 1855 edges, 970 faces: %d %d %d", (int)a->n_vert, (int)a->n_edge,
        (int)a->n_face);
  CHECK(a->n_bone == 89 && a->n_curve == 165 && a->key_count == 40, "89 bones, 165 curves, 40 keys");
  CHECK(verify(a, &why), "the character is a consistent mesh: %s", why);
  CHECK(a->n_vert - a->n_edge + a->n_face == 69, "Euler characteristic: %d", (int)(a->n_vert - a->n_edge + a->n_face));
  CHECK(count_faces(a, 3) == 205 && count_faces(a, 4) == 730 && count_faces(a, 5) == 35, "205 triangles, 730 quads, 35 pentagons");

  int old_format = -1;
  const size_t used_b = gt3_babe_read(b, old, n_old, &old_format, error, sizeof error);
  CHECK(used_b == n_old && old_format == 1, "the executable's file: %zu of %zu bytes, format %d (%s)", used_b, n_old, old_format, error);
  CHECK(a->n_vert == b->n_vert && a->n_edge == b->n_edge && a->n_face == b->n_face && !memcmp(a->vert, b->vert, (size_t)a->n_vert * sizeof *a->vert) &&
          !memcmp(a->edge, b->edge, (size_t)a->n_edge * sizeof *a->edge) && !memcmp(a->face, b->face, (size_t)a->n_face * sizeof *a->face),
        "both files give the same topology and skinning");
  CHECK(a->vert_count == b->vert_count, "the same vertex count");
  /* the vertices differ by the dither only if the generator was not at the same place: here it is not (the second read used the numbers the first took) */
  float worst = 0, scale = 0;
  for (int32_t i = 0; i < a->n_curve * a->key_count; i++) {
    worst = fmaxf(worst, fabsf(a->key_buf[i] - b->key_buf[i]));
    scale = fmaxf(scale, fabsf(a->key_buf[i]));
  }
  CHECK(worst < 0.1f * scale + 1.0f && worst > 0, "the executable's keys are a coarser quantisation of the same animation (largest difference %g of %g)",
        (double)worst, (double)scale);
  gt3_mesh_destroy(b);

  /* a damaged file is refused, never read beyond its end: truncations and flipped bits (the address sanitizer watches) */
  int refused = 0, accepted = 0;
  for (size_t cut = 0; cut < n_new; cut += 97) {
    gt3_mesh *x = gt3_mesh_create(c);
    refused += gt3_babe_read(x, fresh, cut, NULL, error, sizeof error) == 0;
    gt3_mesh_destroy(x);
  }
  CHECK(refused == (int)((n_new + 96) / 97), "every truncation is refused (%d)", refused);
  uint8_t *bent = (uint8_t *)malloc(n_new);
  for (size_t at = 0; at < n_new; at += 211) {
    for (int bit = 0; bit < 8; bit += 3) {
      memcpy(bent, fresh, n_new);
      bent[at] ^= (uint8_t)(1u << bit);
      gt3_mesh *x = gt3_mesh_create(c);
      accepted += gt3_babe_read(x, bent, n_new, NULL, error, sizeof error) != 0;
      gt3_mesh_destroy(x);
    }
  }
  CHECK(accepted >= 0, "flipped bits were survived");
  free(bent);
  gt3_mesh_destroy(a);
  free(fresh);
  free(old);
}

int suite_mesh(int argc, char **argv)
{
  if (argc < 3) {
    printf("usage: suite_mesh <assets dir> <mesh fixtures dir>\n");
    return 2;
  }
  const int mode = fegetround();
  fesetround(FE_DOWNWARD);
  gt3_rng rng;
  gt3_rng_init(&rng);
  gt3_perlin pl;
  gt3_perlin_init(&pl, &rng);
  gt3_mesh_ctx *c = gt3_mesh_ctx_create(&rng, &pl);
  CHECK(c != NULL, "context");
  if (!c) return 1;
  check_cube(c);
  check_subdivide(c);
  check_normals(c);
  check_recorder(c);
  check_select(c);
  check_babe(c, argv[1], argv[2]);
  gt3_mesh_ctx_destroy(c);
  fesetround(mode);
  printf("mesh: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
