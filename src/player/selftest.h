/* selftest.h - the renderer's unit scenes (candytron --selftest), part of the player.
 *
 * One small scene per feature of the OpenGL ES 3 backend (include/candytron/render/gles.h), drawn through the same gt3_render calls the demo makes and the original's state streams
 * (gt3_state_base, gt3_state_tex), read back from the screen target and compared with the Direct3D 9 fixed function formulas computed on the CPU. The colours are 8 bit, so a pixel
 * may differ by the tolerance of its check (1 to 3 steps) and not by more.
 *
 * Covered: the half pixel (a quad covers exactly the pixels the original's -0.5 shift names), blending (alpha, add, multiply, smooth), the stage chains (modulate, add, subtract, add smooth),
 * point and bilinear sampling, texture matrices, texture coordinates from the normal, depth read and write, culling, the alpha test, vertex lighting (directional, point, spot), and a render
 * target used as a texture (the orientation of the round trip).
 *
 * Needs a current OpenGL context and the implementations of render.h and gles.h above it. Prints one line per scene and a summary; the result is the number of failed checks. */
#ifndef PLAYER_SELFTEST_H_INCLUDED
#define PLAYER_SELFTEST_H_INCLUDED

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct selftest {
  gt3_render *r;
  ct_gles g;
  uint8_t *px;
  int checks, failed, scene_failed;
} selftest;

#define ST_CLEAR_R 32
#define ST_CLEAR_G 64
#define ST_CLEAR_B 96

/* the textures: 20 is 2 x 2 (red, green / blue, yellow), 21 is white with the alpha 255, 0 / 0, 255, 22 is 1 x 1 (128, 64, 255) */
static int st_texture(void *user, int32_t handle, int32_t *w, int32_t *h, const uint8_t **bgra)
{
  static const uint8_t t20[16] = { 0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 0, 255, 255, 255 };
  static const uint8_t t21[16] = { 255, 255, 255, 255, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 255 };
  static const uint8_t t22[4] = { 255, 64, 128, 255 };
  (void)user;
  switch (handle) {
  case 20:
    *w = *h = 2;
    *bgra = t20;
    return 1;
  case 21:
    *w = *h = 2;
    *bgra = t21;
    return 1;
  case 22:
    *w = *h = 1;
    *bgra = t22;
    return 1;
  default: return 0;
  }
}

static uint32_t st_argb(int a, int r, int g, int b) { return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; }

static void st_begin(selftest *s, int rt, uint32_t clear)
{
  gt3_fxview v = s->r->view[0];
  for (int i = 1; rt >= 0 && i < s->r->n_view; i++)
    if (s->r->view[i].render_target == rt) v = s->r->view[i];
  v.clear_color = clear;
  v.clear_flags = 3;
  gt3_render_frame_begin(s->r);
  gt3_begin_viewport(s->r, &v);
  gt3_set_camera_ortho(s->r);
}

static void st_end(selftest *s)
{
  gt3_end_viewport(s->r);
  gt3_render_frame_end(s->r);
}

static void st_read(selftest *s) { ct_gles_read_screen(&s->g, s->px); }

static void st_state(selftest *s, uint32_t flags, int texmode, uint32_t t0, uint32_t t1)
{
  uint32_t buf[512], *p = buf;
  gt3_state_base(&p, flags, texmode, 0);
  gt3_state_tex(&p, 0, t0);
  gt3_state_tex(&p, 1, t1);
  gt3_state_end(&p);
  gt3_set_states(s->r, buf);
  gt3_set_texture(s->r, 0, -1, NULL);
  gt3_set_texture(s->r, 1, -1, NULL);
}

/* a 2D quad in the format with a colour, in pixels, the corners as the original lists them (top left, top right, bottom right, bottom left), with its -0.5 */
static void st_quad(selftest *s, float x0, float y0, float x1, float y1, float z, uint32_t col, float u0, float v0, float u1, float v1)
{
  gt3_geodev *d = &s->r->dev;
  const float xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 }, us[4] = { u0, u1, u1, u0 }, vs[4] = { v0, v0, v1, v1 };
  float *f;
  uint16_t *ip = NULL;
  const int32_t h = gt3_geo_add(d, GT3_FVF_DOUBLE, GT3_GEO_QUAD);
  gt3_geo_begin(d, h, 4, 0, &f, &ip);
  for (int i = 0; i < 4; i++) {
    f[i * 8 + 0] = xs[i] - 0.5f;
    f[i * 8 + 1] = ys[i] - 0.5f;
    f[i * 8 + 2] = z;
    memcpy(&f[i * 8 + 3], &col, 4);
    f[i * 8 + 4] = us[i];
    f[i * 8 + 5] = vs[i];
    f[i * 8 + 6] = us[i];
    f[i * 8 + 7] = vs[i];
  }
  gt3_geo_end(d, h, -1, 0);
  gt3_geo_draw(d, h);
  gt3_geo_rem(d, h);
}

/* a lit quad of two triangles with one normal, in the standard format (position, normal, texture coordinates), in pixels */
static void st_mesh(selftest *s, float x0, float y0, float x1, float y1, float z, const float n[3])
{
  gt3_geodev *d = &s->r->dev;
  const float xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 }, us[4] = { 0, 1, 1, 0 }, vs[4] = { 0, 0, 1, 1 };
  static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
  float *f;
  uint16_t *ip = NULL;
  const int32_t h = gt3_geo_add(d, GT3_FVF_STANDARD, GT3_GEO_TRI);
  gt3_geo_begin(d, h, 4, 6, &f, &ip);
  for (int i = 0; i < 4; i++) {
    f[i * 8 + 0] = xs[i];
    f[i * 8 + 1] = ys[i];
    f[i * 8 + 2] = z;
    f[i * 8 + 3] = n[0];
    f[i * 8 + 4] = n[1];
    f[i * 8 + 5] = n[2];
    f[i * 8 + 6] = us[i];
    f[i * 8 + 7] = vs[i];
  }
  memcpy(ip, idx, sizeof idx);
  gt3_geo_end(d, h, -1, 0);
  gt3_geo_draw(d, h);
  gt3_geo_rem(d, h);
}

static void st_check(selftest *s, const char *what, int x, int y, double r, double g, double b, int tolerance)
{
  const uint8_t *p = s->px + ((size_t)y * (size_t)s->r->screen_w + (size_t)x) * 4u;
  s->checks++;
  if (fabs(p[0] - r) > tolerance || fabs(p[1] - g) > tolerance || fabs(p[2] - b) > tolerance) {
    s->failed++;
    s->scene_failed++;
    printf("selftest:   FAIL %s: pixel (%d,%d) is %d,%d,%d, expected %.1f,%.1f,%.1f (+-%d)\n", what, x, y, p[0], p[1], p[2], r, g, b, tolerance);
  }
}

static void st_check_clear(selftest *s, const char *what, int x, int y) { st_check(s, what, x, y, ST_CLEAR_R, ST_CLEAR_G, ST_CLEAR_B, 1); }

static void st_scene_begin(selftest *s, const char *name)
{
  s->scene_failed = 0;
  printf("selftest: %s\n", name);
}

static uint32_t st_clear_color(void) { return st_argb(255, ST_CLEAR_R, ST_CLEAR_G, ST_CLEAR_B); }

/* a flat quad covers exactly the pixels the -0.5 of the original names */
static void st_scene_flat(selftest *s)
{
  st_scene_begin(s, "flat quad: the half pixel");
  st_begin(s, -1, st_clear_color());
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF, GT3_MBM_FLAT, 0, 0);
  st_quad(s, 100, 100, 110, 106, 0.5f, st_argb(255, 200, 100, 50), 0, 0, 1, 1);
  st_end(s);
  st_read(s);
  st_check(s, "top left", 100, 100, 200, 100, 50, 1);
  st_check(s, "bottom right", 109, 105, 200, 100, 50, 1);
  st_check_clear(s, "left of it", 99, 100);
  st_check_clear(s, "right of it", 110, 100);
  st_check_clear(s, "above it", 100, 99);
  st_check_clear(s, "below it", 100, 106);
}

/* the four blends of the original (alpha, add, multiply, smooth) on a flat quad of the colour 200, 100, 50, alpha 128 over the clear colour */
static void st_scene_blend(selftest *s)
{
  static const struct {
    const char *name;
    uint32_t flags;
  } modes[4] = { { "alpha", GT3_MBF_BLENDALPHA }, { "add", GT3_MBF_BLENDADD }, { "multiply", GT3_MBF_BLENDMUL }, { "smooth", GT3_MBF_BLENDSMOOTH } };
  const double a = 128.0 / 255.0, sr = 200.0, sg = 100.0, sb = 50.0, dr = ST_CLEAR_R, dg = ST_CLEAR_G, db = ST_CLEAR_B;
  const double want[4][3] = {
    { sr * a + dr * (1 - a), sg * a + dg * (1 - a), sb * a + db * (1 - a) },
    { sr + dr, sg + dg, sb + db },
    { dr * sr / 255, dg * sg / 255, db * sb / 255 },
    { sr + dr * (1 - sr / 255), sg + dg * (1 - sg / 255), sb + db * (1 - sb / 255) },
  };
  st_scene_begin(s, "blending: alpha, add, multiply, smooth");
  st_begin(s, -1, st_clear_color());
  for (int i = 0; i < 4; i++) {
    st_state(s, GT3_MBF_ZOFF | modes[i].flags, GT3_MBM_FLAT, 0, 0);
    st_quad(s, 100.0f + 20.0f * (float)i, 100, 110.0f + 20.0f * (float)i, 110, 0.5f, st_argb(128, 200, 100, 50), 0, 0, 1, 1);
  }
  st_end(s);
  st_read(s);
  for (int i = 0; i < 4; i++) st_check(s, modes[i].name, 104 + 20 * i, 104, want[i][0], want[i][1], want[i][2], 2);
}

/* stage 1 of the original's modes over stage 0 (the texture 20 times the colour 204, 153, 255), with the texture 22 (128, 64, 255) in stage 1 */
static void st_scene_stages(selftest *s)
{
  static const struct {
    const char *name;
    int mode;
  } modes[4] = { { "add", GT3_MBM_ADD }, { "multiply", GT3_MBM_MUL }, { "subtract", GT3_MBM_SUB }, { "add smooth", GT3_MBM_ADDSMOOTH } };
  const double t[3] = { 128.0 / 255, 64.0 / 255, 1.0 };
  const double c[3] = { 0.8, 0.6, 1.0 };
  const double texel[2][3] = { { 1, 0, 0 }, { 0, 1, 0 } };
  st_scene_begin(s, "texture stages: add, multiply, subtract, add smooth");
  st_begin(s, -1, st_clear_color());
  for (int i = 0; i < 4; i++) {
    st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF, modes[i].mode, 0, 0);
    gt3_set_texture(s->r, 0, 20, NULL);
    gt3_set_texture(s->r, 1, 22, NULL);
    st_quad(s, 100.0f + 20.0f * (float)i, 100, 104.0f + 20.0f * (float)i, 104, 0.5f, st_argb(255, 204, 153, 255), 0, 0, 1, 1);
  }
  st_end(s);
  st_read(s);
  for (int i = 0; i < 4; i++)
    for (int k = 0; k < 2; k++) {
      double v[3];
      for (int ch = 0; ch < 3; ch++) {
        const double s0 = texel[k][ch] * c[ch];
        switch (modes[i].mode) {
        case GT3_MBM_ADD: v[ch] = s0 + t[ch]; break;
        case GT3_MBM_MUL: v[ch] = s0 * t[ch]; break;
        case GT3_MBM_SUB: v[ch] = t[ch] - s0; break;
        default: v[ch] = t[ch] + s0 - t[ch] * s0; break;
        }
        v[ch] = (v[ch] < 0 ? 0 : v[ch] > 1 ? 1 : v[ch]) * 255.0;
      }
      char what[64];
      snprintf(what, sizeof what, "%s, texel %d", modes[i].name, k);
      st_check(s, what, 100 + 20 * i + (k ? 3 : 0), 100, v[0], v[1], v[2], 2);
    }
}

/* point and bilinear sampling of the 2 x 2 texture on 4 x 4 pixels */
static void st_scene_sampling(selftest *s)
{
  st_scene_begin(s, "sampling: point, bilinear");
  st_begin(s, -1, st_clear_color());
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF, GT3_MBM_TEX, 0, 0);
  gt3_set_texture(s->r, 0, 20, NULL);
  st_quad(s, 100, 100, 104, 104, 0.5f, st_argb(255, 255, 255, 255), 0, 0, 1, 1);
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF, GT3_MBM_TEX, GT3_MTF_FILTERMAG | GT3_MTF_FILTERMIN | GT3_MTF_CLAMP, 0);
  gt3_set_texture(s->r, 0, 20, NULL);
  st_quad(s, 120, 100, 124, 104, 0.5f, st_argb(255, 255, 255, 255), 0, 0, 1, 1);
  st_end(s);
  st_read(s);
  st_check(s, "point: red", 100, 100, 255, 0, 0, 1);
  st_check(s, "point: red, second pixel", 101, 100, 255, 0, 0, 1);
  st_check(s, "point: green", 102, 100, 0, 255, 0, 1);
  st_check(s, "point: blue", 100, 102, 0, 0, 255, 1);
  st_check(s, "point: yellow", 103, 103, 255, 255, 0, 1);
  st_check(s, "bilinear: red at the edge", 120, 100, 255, 0, 0, 2);
  st_check(s, "bilinear: red and green", 121, 100, 191.25, 63.75, 0, 3);
}

/* a texture matrix on the coordinates of the model, and the coordinates from the normal. A 2D coordinate enters the matrix as (u, v, 1, 0) in Direct3D's
   fixed function (wined3d's compute_texture_matrix), so the translation of the coordinates of the model is the third row of the matrix and the fourth row
   does not move them; the generated coordinates (the normal) are (x, y, z, 1) and use the fourth row. */
static void st_scene_texcoords(selftest *s)
{
  gt3_mat shift, row4, envi;
  gt3_mat_init(&shift);
  shift.k.x = 0.5f;
  gt3_mat_init(&row4);
  row4.l.x = 0.5f;
  gt3_mat_init(&envi);
  envi.i.x = envi.j.y = envi.k.z = 0.5f;
  envi.l.x = envi.l.y = envi.l.z = 0.5f;
  st_scene_begin(s, "texture coordinates: matrix, camera space normal");
  st_begin(s, -1, st_clear_color());
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF, GT3_MBM_TEX, GT3_MTF_UVTRANS, 0);
  gt3_set_texture(s->r, 0, 20, &shift);
  st_quad(s, 100, 100, 104, 104, 0.5f, st_argb(255, 255, 255, 255), 0, 0, 1, 1);
  gt3_set_texture(s->r, 0, 20, &row4);
  st_quad(s, 140, 100, 144, 104, 0.5f, st_argb(255, 255, 255, 255), 0, 0, 1, 1);
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF, GT3_MBM_TEX, GT3_MTF_UVENVI | GT3_MTF_UVTRANS | GT3_MTF_CLAMP, 0);
  gt3_set_texture(s->r, 0, 20, &envi);
  {
    const float n[3] = { 0.8f, -0.6f, 0.0f };
    st_mesh(s, 120, 100, 124, 104, 0.5f, n);
  }
  st_end(s);
  st_read(s);
  st_check(s, "shifted: green first", 100, 100, 0, 255, 0, 1);
  st_check(s, "shifted: red wrapped", 103, 100, 255, 0, 0, 1);
  st_check(s, "shifted: blue wrapped", 103, 103, 0, 0, 255, 1);
  st_check(s, "the fourth row does not move a 2D coordinate: red first", 140, 100, 255, 0, 0, 1);
  st_check(s, "the fourth row does not move a 2D coordinate: green second", 143, 100, 0, 255, 0, 1);
  st_check(s, "normal (0.8, -0.6): green", 121, 101, 0, 255, 0, 1);
}

/* the depth test with and without writing, in both orders */
static void st_scene_depth(selftest *s)
{
  st_scene_begin(s, "depth: read and write");
  st_begin(s, -1, st_clear_color());
  st_state(s, GT3_MBF_ZON | GT3_MBF_BLENDOFF, GT3_MBM_FLAT, 0, 0);
  st_quad(s, 100, 100, 110, 110, 0.3f, st_argb(255, 255, 0, 0), 0, 0, 1, 1);
  st_quad(s, 105, 105, 115, 115, 0.6f, st_argb(255, 0, 0, 255), 0, 0, 1, 1);
  st_quad(s, 130, 100, 140, 110, 0.6f, st_argb(255, 0, 0, 255), 0, 0, 1, 1);
  st_quad(s, 135, 105, 145, 115, 0.3f, st_argb(255, 255, 0, 0), 0, 0, 1, 1);
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF, GT3_MBM_FLAT, 0, 0);
  st_quad(s, 160, 100, 170, 110, 0.3f, st_argb(255, 255, 0, 0), 0, 0, 1, 1);
  st_quad(s, 165, 105, 175, 115, 0.6f, st_argb(255, 0, 0, 255), 0, 0, 1, 1);
  st_end(s);
  st_read(s);
  st_check(s, "near first: near wins", 107, 107, 255, 0, 0, 1);
  st_check(s, "near first: far beside it", 112, 112, 0, 0, 255, 1);
  st_check(s, "far first: near wins", 137, 107, 255, 0, 0, 1);
  st_check(s, "far first: far where it is alone", 132, 102, 0, 0, 255, 1);
  st_check(s, "no depth: the later one wins", 167, 107, 0, 0, 255, 1);
}

/* the culling: the original's quads wind clockwise on the screen and are kept by default */
static void st_scene_cull(selftest *s)
{
  st_scene_begin(s, "culling: default, inverted, double sided");
  st_begin(s, -1, st_clear_color());
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF, GT3_MBM_FLAT, 0, 0);
  st_quad(s, 100, 100, 110, 110, 0.5f, st_argb(255, 255, 255, 255), 0, 0, 1, 1);
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF | GT3_MBF_INVERTCULL, GT3_MBM_FLAT, 0, 0);
  st_quad(s, 120, 100, 130, 110, 0.5f, st_argb(255, 255, 255, 255), 0, 0, 1, 1);
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF | GT3_MBF_DOUBLESIDED, GT3_MBM_FLAT, 0, 0);
  st_quad(s, 140, 100, 150, 110, 0.5f, st_argb(255, 255, 255, 255), 0, 0, 1, 1);
  st_end(s);
  st_read(s);
  st_check(s, "default: drawn", 105, 105, 255, 255, 255, 1);
  st_check_clear(s, "inverted: culled", 125, 105);
  st_check(s, "double sided: drawn", 145, 105, 255, 255, 255, 1);
}

/* the alpha test (greater than 4) on the texture 21, whose second texel has the alpha 0 */
static void st_scene_alphatest(selftest *s)
{
  st_scene_begin(s, "alpha test");
  st_begin(s, -1, st_clear_color());
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF, GT3_MBM_TEX, 0, 0);
  gt3_set_texture(s->r, 0, 21, NULL);
  gt3_set_state(s->r, GT3_D3DRS_ALPHATESTENABLE, 1);
  st_quad(s, 100, 100, 104, 104, 0.5f, st_argb(255, 255, 255, 255), 0, 0, 1, 1);
  gt3_set_state(s->r, GT3_D3DRS_ALPHATESTENABLE, 0);
  st_quad(s, 120, 100, 124, 104, 0.5f, st_argb(255, 255, 255, 255), 0, 0, 1, 1);
  st_end(s);
  st_read(s);
  st_check(s, "test on: opaque texel drawn", 100, 100, 255, 255, 255, 1);
  st_check_clear(s, "test on: transparent texel dropped", 103, 100);
  st_check(s, "test off: transparent texel drawn", 123, 100, 255, 255, 255, 1);
}

/* vertex lighting of a quad of the standard format: emissive + ambient * material ambient + diffuse * (N.L) of the light, per channel, clamped */
static void st_scene_lighting(selftest *s)
{
  static const float material[17] = { 1, 1, 1, 1, 0.5f, 0.5f, 0.5f, 1, 0, 0, 0, 0, 0.1f, 0, 0.05f, 0, 0 };
  const double emissive[3] = { 0.1, 0, 0.05 }, ambient[3] = { 0.2 * 0.5, 0.2 * 0.5, 0.2 * 0.5 }, diffuse[3] = { 0.5, 0.25, 1.0 };
  const float flat[3] = { 0, 0, -1 }, tilted[3] = { 0, 0.6f, -0.8f };
  gt3_d3dlight l;
  memset(&l, 0, sizeof l);
  l.type = 3;
  for (int i = 0; i < 3; i++) {
    l.diffuse[i] = (float)diffuse[i];
    l.ambient[i] = 0.2f;
  }
  l.diffuse[3] = l.ambient[3] = 1;
  l.direction[2] = 1;
  l.range = 1000;
  l.attenuation[0] = 1;
  st_scene_begin(s, "lighting: directional, point, spot");
  st_begin(s, -1, st_clear_color());
  gt3_set_matlight(s->r, material);
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF | GT3_MBF_LIGHT | GT3_MBF_RENORMALIZE, GT3_MBM_FLAT, 0, 0);
  gt3_add_light(s->r, &l, 1);
  gt3_enable_lights(s->r, 1);
  st_mesh(s, 100, 100, 104, 104, 0.5f, flat);
  st_mesh(s, 120, 100, 124, 104, 0.5f, tilted);
  gt3_clear_lights(s->r);
  /* a point light 100 pixels in front of the quads at (400, 300), a spot light of the same place that looks at them */
  l.type = 1;
  l.position[0] = 400;
  l.position[1] = 300;
  l.position[2] = -100;
  gt3_add_light(s->r, &l, 1);
  gt3_enable_lights(s->r, 1);
  st_mesh(s, 398, 298, 402, 302, 0.5f, flat);
  gt3_clear_lights(s->r);
  l.type = 2;
  l.position[0] = 500;
  l.theta = 0.2f;
  l.phi = 0.4f;
  l.falloff = 1;
  gt3_add_light(s->r, &l, 1);
  gt3_enable_lights(s->r, 1);
  st_mesh(s, 498, 298, 502, 302, 0.5f, flat);
  st_mesh(s, 100, 200, 104, 204, 0.5f, flat);
  gt3_clear_lights(s->r);
  st_end(s);
  st_read(s);
  double full[3], half[3], dark[3];
  for (int ch = 0; ch < 3; ch++) {
    full[ch] = (emissive[ch] + ambient[ch] + diffuse[ch]) > 1 ? 255.0 : (emissive[ch] + ambient[ch] + diffuse[ch]) * 255.0;
    const double t = emissive[ch] + ambient[ch] + diffuse[ch] * 0.8;
    half[ch] = (t > 1 ? 1 : t) * 255.0;
    dark[ch] =
      emissive[ch] *
      255.0; /* a light that does not reach the vertex adds nothing, its ambient colour included (Direct3D multiplies the sum by the attenuation and the spot factor) */
  }
  st_check(s, "directional, facing", 102, 102, full[0], full[1], full[2], 2);
  st_check(s, "directional, N.L = 0.8", 122, 102, half[0], half[1], half[2], 2);
  st_check(s, "point light", 400, 300, full[0], full[1], full[2], 2);
  st_check(s, "spot light, inside the cone", 500, 300, full[0], full[1], full[2], 2);
  st_check(s, "spot light, outside the cone", 102, 202, dark[0], dark[1], dark[2], 2);
}

/* a render target as a texture: what was drawn at the top left of the target is at the top left of the quad that shows it */
static void st_scene_target(selftest *s)
{
  st_scene_begin(s, "render target as a texture");
  st_begin(s, 5, st_argb(255, 16, 32, 48));
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF, GT3_MBM_FLAT, 0, 0);
  st_quad(s, 10, 10, 60, 60, 0.5f, st_argb(255, 255, 0, 0), 0, 0, 1, 1);
  st_quad(s, 200, 180, 240, 230, 0.5f, st_argb(255, 0, 255, 0), 0, 0, 1, 1);
  st_end(s);
  st_begin(s, -1, st_clear_color());
  st_state(s, GT3_MBF_ZOFF | GT3_MBF_BLENDOFF, GT3_MBM_TEX, 0, 0);
  gt3_set_texture(s->r, 0, 5, NULL);
  st_quad(s, 100, 100, 356, 356, 0.5f, st_argb(255, 255, 255, 255), 0, 0, 1, 1);
  st_end(s);
  st_read(s);
  st_check(s, "red at the top left", 100 + 30, 100 + 30, 255, 0, 0, 1);
  st_check(s, "red corner", 100 + 10, 100 + 10, 255, 0, 0, 1);
  st_check(s, "green at the bottom right", 100 + 220, 100 + 200, 0, 255, 0, 1);
  st_check(s, "the target's clear colour", 100 + 100, 100 + 100, 16, 32, 48, 1);
  st_check(s, "not at the bottom left", 100 + 30, 100 + 220, 16, 32, 48, 1);
  st_check_clear(s, "outside the quad", 99, 100);
}

/* the scenes; returns the number of failed checks (and 1000 for a setup that did not work) */
static int selftest_run(const ct_gl *gl, int es)
{
  selftest *s = (selftest *)calloc(1, sizeof *s);
  static void (*const scenes[])(selftest *) = { st_scene_flat,  st_scene_blend, st_scene_stages,    st_scene_sampling, st_scene_texcoords,
                                                st_scene_depth, st_scene_cull,  st_scene_alphatest, st_scene_lighting, st_scene_target };
  int result = 0;
  if (!s) return 1000;
  s->r = (gt3_render *)calloc(1, sizeof *s->r);
  s->px = (uint8_t *)malloc(800u * 600u * 4u);
  if (!s->r || !s->px || !gt3_render_init(s->r, 800, 600)) {
    printf("selftest: out of memory\n");
    result = 1000;
  } else if (!ct_gles_init(&s->g, gl, es, s->r, st_texture, s)) {
    printf("selftest: the renderer: %s\n", s->g.error);
    result = 1000;
  } else {
    s->r->on_draw = ct_gles_on_draw;
    s->r->on_viewport = ct_gles_on_viewport;
    s->r->user = &s->g;
    for (size_t i = 0; i < sizeof scenes / sizeof scenes[0]; i++) scenes[i](s);
    s->checks++;
    for (uint32_t code; (code = gl->GetError()) != 0;) {
      s->failed++;
      printf("selftest:   FAIL the driver raised GL error 0x%x\n", code);
    }
    printf("selftest: %d checks, %d failed\n", s->checks, s->failed);
    puts(s->failed ? "selftest: FAILED" : "selftest: ok");
    result = s->failed;
    ct_gles_exit(&s->g);
  }
  if (s->r) {
    gt3_render_exit(s->r);
    free(s->r);
  }
  free(s->px);
  free(s);
  return result;
}

#endif
