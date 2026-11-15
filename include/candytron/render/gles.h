/* gles.h - the OpenGL ES 3 backend of the renderer: Direct3D 9's fixed function pipeline as the intro uses it, in one shader - header-only C99 (stb style)

   render.h makes the draw calls of the demo and keeps the device state they depend on (the render, texture stage and sampler states of the original's state streams, the
   matrices, lights and material, the textures bound); this file draws them. Three hooks of gt3_render are what it needs: on_viewport (a render target or the screen is
   selected and cleared), on_draw (a geometry is drawn) and the texture provider (the pixels of a texture handle).

   What the shader implements, all of it Direct3D 9 semantics: vertex lighting (directional, point and spot lights, the material and its colour sources, specular), up to three
   texture stages (SELECTARG1/2, MODULATE, MODULATE4X, ADD, SUBTRACT, ADDSMOOTH, BLENDDIFFUSEALPHA, MODULATEALPHA_ADDCOLOR and the rest of the table, with the texture factor and the
   alpha replicate modifier), texture coordinate generation (camera space normal, position, reflection vector) and the texture matrices, the alpha test, blending, depth test and
   write, culling and the colour write mask. The clip space depth is converted (Direct3D 0..w, OpenGL -w..w) and everything is rendered upside down, so that the first row of every
   target is the top row, as Direct3D's and as a texture upload's: a render target is sampled with the same coordinates as a bitmap. The 800 x 600 picture is a target of its
   own, shown by ct_gles_present.

   A texture stage that needs a texture it has not been given (an argument is TEXTURE and nothing is bound) ends the stage chain, as the original's draws rely on (the bars of the
   "blend" pass bind one texture to a chain of two); a stage whose arguments are only the diffuse colour and the stage before works without one. A zero normal stays zero (it is
   not normalised), so that texture coordinates from the normal come out as the matrix's offset.

   The half pixel of Direct3D 9: its pixel centres are at integers, the intro moves its screen quads by -0.5 to hit them; the 2D vertices (the format with a colour) are moved back.

   Floating point: the script runs with the rounding mode of the original (round downward); the hooks switch to round to nearest around the driver, which does not expect anything
   else.

   USAGE
     #define CT_GLES_IMPLEMENTATION in one translation unit, after render.h's implementation and gl.h. ct_gles_init needs a current context. */
#ifndef CT_GLES_H_INCLUDED
#define CT_GLES_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#include <candytron/core/render.h>
#include <candytron/render/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CT_GLES_API
#ifdef CT_GLES_STATIC
#define CT_GLES_API static
#else
#define CT_GLES_API extern
#endif
#endif

enum { CT_GLES_MAX_TEXTURES = 256, CT_GLES_TARGETS = GT3_FX_VIEWPORTS, CT_GLES_MAX_LIGHTS = 8, CT_GLES_QUADS = 8192 };

/* The pixels of a texture handle (not a render target): 8 bit blue, green, red, alpha, `width` x `height`. Returns 0 when there is none. */
typedef int (*ct_gles_texture_fn)(void *user, int32_t handle, int32_t *width, int32_t *height, const uint8_t **bgra);

typedef struct ct_gles {
  ct_gl gl;
  int es; /* 1: OpenGL ES 3.0, 0: OpenGL 3.3 core */
  gt3_render *render;
  ct_gles_texture_fn texture_fn;
  void *texture_user;
  char error[512];
  unsigned program, present_program, vao_tri, vao_quad, vbo, ibo, quad_ibo, empty_vao, sampler[2];
  int loc[96];
  int present_loc;
  /* targets 0 ... 9: the render targets (texture handles 0 ... 9), CT_GLES_TARGETS - 1: the screen */
  unsigned tgt_fbo[CT_GLES_TARGETS], tgt_tex[CT_GLES_TARGETS], tgt_depth[CT_GLES_TARGETS];
  int tgt_w[CT_GLES_TARGETS], tgt_h[CT_GLES_TARGETS];
  unsigned tex[CT_GLES_MAX_TEXTURES];
  uint8_t tex_ready[CT_GLES_MAX_TEXTURES], tex_mips[CT_GLES_MAX_TEXTURES];
  int sampler_state[2][5];
  int current_target;
  uint64_t draws, uploads;
} ct_gles;

CT_GLES_API int ct_gles_init(ct_gles *g, const ct_gl *gl, int es, gt3_render *render, ct_gles_texture_fn texture_fn, void *texture_user);
CT_GLES_API void ct_gles_exit(ct_gles *g);
/* gt3_render hooks: user is the ct_gles */
CT_GLES_API void ct_gles_on_viewport(void *user, gt3_render *r, const gt3_fxview *view);
CT_GLES_API void ct_gles_on_draw(void *user, gt3_geodev *dev, int32_t handle);
/* draws the picture into the bound framebuffer, into the rectangle x, y, w, h */
CT_GLES_API void ct_gles_present(ct_gles *g, int x, int y, int w, int h);
/* the picture as RGBA, top row first, screen_w * screen_h * 4 bytes */
CT_GLES_API void ct_gles_read_screen(ct_gles *g, uint8_t *rgba);

#ifdef __cplusplus
}
#endif

#endif /* CT_GLES_H_INCLUDED */

#ifdef CT_GLES_IMPLEMENTATION
#ifndef CT_GLES_IMPLEMENTATION_DONE
#define CT_GLES_IMPLEMENTATION_DONE

#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the shaders ---- */

#define CT_GLES_VERSION_ES   "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
#define CT_GLES_VERSION_CORE "#version 330 core\n"

static const char ct_gles_vs[] =
  "layout(location = 0) in vec3 a_pos;\n"
  "layout(location = 1) in vec3 a_nrm;\n"
  "layout(location = 2) in vec4 a_col;\n"
  "layout(location = 3) in vec2 a_uv0;\n"
  "layout(location = 4) in vec2 a_uv1;\n"
  "uniform mat4 u_world;\n"
  "uniform mat4 u_view;\n"
  "uniform mat4 u_proj;\n"
  "uniform vec4 u_target;\n" /* viewport width, height, y sign */
  "uniform int u_double;\n"
  "uniform int u_lighting;\n"
  "uniform int u_renorm;\n"
  "uniform int u_specular;\n"
  "uniform ivec4 u_src;\n"   /* colour sources of diffuse, ambient, specular, emissive */
  "uniform vec4 u_mat[5];\n" /* diffuse, ambient, specular, emissive, power */
  "uniform int u_nlights;\n"
  "uniform vec4 u_ldiff[8];\n"
  "uniform vec4 u_lamb[8];\n"
  "uniform vec4 u_lspec[8];\n"
  "uniform vec4 u_lpos[8];\n"  /* position, type */
  "uniform vec4 u_ldir[8];\n"  /* direction, range */
  "uniform vec4 u_lspot[8];\n" /* falloff, theta, phi */
  "uniform mat4 u_tm0;\n"
  "uniform mat4 u_tm1;\n"
  "uniform ivec4 u_tc0;\n" /* coordinate set, generation, transform flags */
  "uniform ivec4 u_tc1;\n"
  "out vec4 v_diff;\n"
  "out vec4 v_spec;\n"
  "out vec4 v_t0;\n"
  "out vec4 v_t1;\n"
  "vec4 srcColor(int src, vec4 material, vec4 vcol) {\n"
  "  if (src == 1) return vcol;\n"
  "  if (src == 2) return vec4(0.0);\n"
  "  return material;\n"
  "}\n"
  "vec4 texcoord(ivec4 tc, mat4 tm, vec3 posv, vec3 nv) {\n"
  "  vec4 src;\n"
  "  if (tc.y == 1) src = vec4(nv, 1.0);\n"
  "  else if (tc.y == 2) src = vec4(posv, 1.0);\n"
  "  else if (tc.y == 3) src = vec4(reflect(normalize(posv), nv), 1.0);\n"
  "  else src = vec4((tc.x == 1 && u_double != 0) ? a_uv1 : a_uv0, 0.0, 1.0);\n"
  "  if (tc.z != 0) {\n"
  /* Direct3D's fixed function feeds a texture matrix with the components the coordinate has and fills the rest with (..., 1, 0): a 2D
                                    coordinate is (u, v, 1, 0), so the third row of the matrix is the translation, not the fourth (wined3d's compute_texture_matrix
                                    documents it, and the recordings show it: the character's skin pass maps (u, v) to (0.5 u, 0.5 v), not (0.5 u + 0.5, 0.5 v + 0.5)).
                                    Generated coordinates have three components and w = 1. */
  "    if (tc.y == 0) src = vec4(src.xy, 1.0, 0.0);\n"
  "    src = tm * src;\n"
  "  }\n"
  "  return src;\n"
  "}\n"
  "void main() {\n"
  "  vec4 vp = u_view * (u_world * vec4(a_pos, 1.0));\n"
  "  vec4 cp = u_proj * vp;\n"
  "  cp.z = 2.0 * cp.z - cp.w;\n"
  "  if (u_double != 0) cp.xy += vec2(1.0 / u_target.x, -1.0 / u_target.y) * cp.w;\n"
  "  cp.y *= u_target.z;\n"
  "  gl_Position = cp;\n"
  "  vec4 vcol = (u_double != 0) ? a_col.zyxw : vec4(1.0);\n"
  "  vec3 posv = vp.xyz;\n"
  "  vec3 nv = vec3(0.0, 0.0, 1.0);\n"
  "  if (u_double == 0) {\n"
  "    mat3 wv = mat3(u_view * u_world);\n"
  "    nv = transpose(inverse(wv)) * a_nrm;\n"
  "    if (u_renorm != 0 && dot(nv, nv) > 0.0) nv = normalize(nv);\n" /* a zero normal stays zero, as in Direct3D */
  "  }\n"
  "  v_spec = vec4(0.0);\n"
  "  if (u_lighting != 0) {\n"
  "    vec4 md = srcColor(u_src.x, u_mat[0], vcol);\n"
  "    vec4 ma = srcColor(u_src.y, u_mat[1], vcol);\n"
  "    vec4 ms = srcColor(u_src.z, u_mat[2], vcol);\n"
  "    vec4 me = srcColor(u_src.w, u_mat[3], vcol);\n"
  "    vec3 diff = me.rgb;\n"
  "    vec3 spec = vec3(0.0);\n"
  "    vec3 eye = normalize(-posv);\n"
  "    for (int i = 0; i < 8; i++) {\n"
  "      if (i >= u_nlights) break;\n"
  "      int type = int(u_lpos[i].w + 0.5);\n"
  "      vec3 L;\n"
  "      float att = 1.0;\n"
  "      if (type == 3) {\n"
  "        L = normalize(-(mat3(u_view) * u_ldir[i].xyz));\n"
  "      } else {\n"
  "        vec3 lp = (u_view * vec4(u_lpos[i].xyz, 1.0)).xyz;\n"
  "        vec3 d = lp - posv;\n"
  "        float dist = length(d);\n"
  "        if (dist > u_ldir[i].w) continue;\n"
  "        L = dist > 0.0 ? d / dist : vec3(0.0);\n"
  "        if (type == 2) {\n"
  "          vec3 ld = normalize(mat3(u_view) * u_ldir[i].xyz);\n"
  "          float rho = dot(-L, ld);\n"
  "          float cphi = cos(u_lspot[i].z * 0.5);\n"
  "          float ctheta = cos(u_lspot[i].y * 0.5);\n"
  "          if (rho <= cphi) continue;\n"
  "          if (rho < ctheta) att *= pow((rho - cphi) / (ctheta - cphi), u_lspot[i].x);\n"
  "        }\n"
  "      }\n"
  "      float ndl = max(dot(nv, L), 0.0);\n"
  "      diff += ma.rgb * u_lamb[i].rgb * att + md.rgb * u_ldiff[i].rgb * ndl * att;\n"
  "      if (u_specular != 0 && ndl > 0.0) {\n"
  "        vec3 H = normalize(L + eye);\n"
  "        spec += ms.rgb * u_lspec[i].rgb * pow(max(dot(nv, H), 0.0), u_mat[4].x) * att;\n"
  "      }\n"
  "    }\n"
  "    v_diff = vec4(clamp(diff, 0.0, 1.0), md.a);\n"
  "    v_spec = vec4(clamp(spec, 0.0, 1.0), 0.0);\n"
  "  } else {\n"
  "    v_diff = vcol;\n"
  "  }\n"
  "  v_t0 = texcoord(u_tc0, u_tm0, posv, nv);\n"
  "  v_t1 = texcoord(u_tc1, u_tm1, posv, nv);\n"
  "}\n";

static const char ct_gles_fs[] =
  "in vec4 v_diff;\n"
  "in vec4 v_spec;\n"
  "in vec4 v_t0;\n"
  "in vec4 v_t1;\n"
  "uniform sampler2D u_tex0;\n"
  "uniform sampler2D u_tex1;\n"
  "uniform ivec2 u_hastex;\n"
  "uniform ivec4 u_cop;\n"
  "uniform ivec4 u_carg1;\n"
  "uniform ivec4 u_carg2;\n"
  "uniform ivec4 u_aop;\n"
  "uniform ivec4 u_aarg1;\n"
  "uniform ivec4 u_aarg2;\n"
  "uniform vec4 u_tfactor;\n"
  "uniform int u_specular;\n"
  "uniform ivec3 u_atest;\n" /* enable, function, reference */
  "out vec4 o_color;\n"
  "vec4 pick(int a, vec4 diffuse, vec4 cur, vec4 tex) {\n"
  "  int k = a & 15;\n"
  "  vec4 v = vec4(0.0);\n"
  "  if (k == 0) v = diffuse;\n"
  "  else if (k == 1) v = cur;\n"
  "  else if (k == 2) v = tex;\n"
  "  else if (k == 3) v = u_tfactor;\n"
  "  if ((a & 16) != 0) v = vec4(1.0) - v;\n"
  "  if ((a & 32) != 0) v = vec4(v.a);\n"
  "  return v;\n"
  "}\n"
  "vec3 colorop(int op, vec4 a1, vec4 a2, vec4 diffuse, vec4 tex, vec4 cur) {\n"
  "  if (op == 2) return a1.rgb;\n"
  "  if (op == 3) return a2.rgb;\n"
  "  if (op == 4) return a1.rgb * a2.rgb;\n"
  "  if (op == 5) return a1.rgb * a2.rgb * 2.0;\n"
  "  if (op == 6) return a1.rgb * a2.rgb * 4.0;\n"
  "  if (op == 7) return a1.rgb + a2.rgb;\n"
  "  if (op == 8) return a1.rgb + a2.rgb - 0.5;\n"
  "  if (op == 9) return (a1.rgb + a2.rgb - 0.5) * 2.0;\n"
  "  if (op == 10) return a1.rgb - a2.rgb;\n"
  "  if (op == 11) return a1.rgb + a2.rgb - a1.rgb * a2.rgb;\n"
  "  if (op == 12) return a1.rgb * diffuse.a + a2.rgb * (1.0 - diffuse.a);\n"
  "  if (op == 13) return a1.rgb * tex.a + a2.rgb * (1.0 - tex.a);\n"
  "  if (op == 14) return a1.rgb * u_tfactor.a + a2.rgb * (1.0 - u_tfactor.a);\n"
  "  if (op == 16) return a1.rgb * cur.a + a2.rgb * (1.0 - cur.a);\n"
  "  if (op == 18) return a1.rgb + a1.a * a2.rgb;\n"
  "  return cur.rgb;\n"
  "}\n"
  "float alphaop(int op, float a1, float a2, vec4 diffuse, vec4 tex, vec4 cur) {\n"
  "  if (op == 2) return a1;\n"
  "  if (op == 3) return a2;\n"
  "  if (op == 4) return a1 * a2;\n"
  "  if (op == 5) return a1 * a2 * 2.0;\n"
  "  if (op == 6) return a1 * a2 * 4.0;\n"
  "  if (op == 7) return a1 + a2;\n"
  "  if (op == 8) return a1 + a2 - 0.5;\n"
  "  if (op == 9) return (a1 + a2 - 0.5) * 2.0;\n"
  "  if (op == 10) return a1 - a2;\n"
  "  if (op == 11) return a1 + a2 - a1 * a2;\n"
  "  if (op == 12) return a1 * diffuse.a + a2 * (1.0 - diffuse.a);\n"
  "  if (op == 13) return a1 * tex.a + a2 * (1.0 - tex.a);\n"
  "  if (op == 14) return a1 * u_tfactor.a + a2 * (1.0 - u_tfactor.a);\n"
  "  if (op == 16) return a1 * cur.a + a2 * (1.0 - cur.a);\n"
  "  if (op == 18) return a1;\n"
  "  return cur.a;\n"
  "}\n"
  "bool stage(int cop, int ca1, int ca2, int aop, int aa1, int aa2, int has, vec4 tex, vec4 diffuse, inout vec4 cur) {\n"
  "  if (cop == 1) return false;\n"
  "  if (has == 0) {\n" /* a stage that needs a texture it has not been given ends the chain */
  "    bool c1 = cop != 3 && (ca1 & 7) == 2, c2 = cop != 2 && (ca2 & 7) == 2;\n"
  "    bool a1 = aop != 3 && (aa1 & 7) == 2, a2 = aop != 2 && (aa2 & 7) == 2;\n"
  "    if (c1 || c2 || a1 || a2 || cop == 13) return false;\n"
  "  }\n"
  "  vec3 rgb = colorop(cop, pick(ca1, diffuse, cur, tex), pick(ca2, diffuse, cur, tex), diffuse, tex, cur);\n"
  "  float a = cur.a;\n"
  "  if (aop != 1) a = alphaop(aop, pick(aa1, diffuse, cur, tex).a, pick(aa2, diffuse, cur, tex).a, diffuse, tex, cur);\n"
  "  cur = clamp(vec4(rgb, a), 0.0, 1.0);\n"
  "  return true;\n"
  "}\n"
  "void main() {\n"
  "  vec4 cur = v_diff;\n"
  "  vec4 t0 = (u_hastex.x != 0) ? texture(u_tex0, v_t0.xy) : vec4(0.0, 0.0, 0.0, 1.0);\n" /* a stage without a texture reads black, as Direct3D 9 does */
  "  vec4 t1 = (u_hastex.y != 0) ? texture(u_tex1, v_t1.xy) : vec4(0.0, 0.0, 0.0, 1.0);\n"
  "  if (stage(u_cop.x, u_carg1.x, u_carg2.x, u_aop.x, u_aarg1.x, u_aarg2.x, u_hastex.x, t0, v_diff, cur))\n"
  "    if (stage(u_cop.y, u_carg1.y, u_carg2.y, u_aop.y, u_aarg1.y, u_aarg2.y, u_hastex.y, t1, v_diff, cur))\n"
  "      stage(u_cop.z, u_carg1.z, u_carg2.z, u_aop.z, u_aarg1.z, u_aarg2.z, 0, vec4(0.0, 0.0, 0.0, 1.0), v_diff, cur);\n"
  "  if (u_specular != 0) cur.rgb = clamp(cur.rgb + v_spec.rgb, 0.0, 1.0);\n"
  "  if (u_atest.x != 0) {\n"
  "    int a8 = int(floor(cur.a * 255.0 + 0.5));\n"
  "    int f = u_atest.y;\n"
  "    bool pass = true;\n"
  "    if (f == 1) pass = false;\n"
  "    else if (f == 2) pass = a8 < u_atest.z;\n"
  "    else if (f == 3) pass = a8 == u_atest.z;\n"
  "    else if (f == 4) pass = a8 <= u_atest.z;\n"
  "    else if (f == 5) pass = a8 > u_atest.z;\n"
  "    else if (f == 6) pass = a8 != u_atest.z;\n"
  "    else if (f == 7) pass = a8 >= u_atest.z;\n"
  "    if (!pass) discard;\n"
  "  }\n"
  "  o_color = cur;\n"
  "}\n";

static const char ct_gles_present_vs[] = "out vec2 v_uv;\n"
                                         "void main() {\n"
                                         "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                                         "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                                         "  v_uv = vec2(p.x, 1.0 - p.y);\n"
                                         "}\n";

static const char ct_gles_present_fs[] = "in vec2 v_uv;\n"
                                         "uniform sampler2D u_screen;\n"
                                         "out vec4 o_color;\n"
                                         "void main() { o_color = vec4(texture(u_screen, v_uv).rgb, 1.0); }\n";

/* the uniforms of the main program, in the order of ct_gles.loc */
// clang-format off
#define CT_GLES_UNIFORMS(X) \
  X(world) X(view) X(proj) X(target) X(is_double) X(lighting) X(renorm) X(specular) X(src) X(mat) X(nlights) X(ldiff) X(lamb) X(lspec) X(lpos) X(ldir) X(lspot) X(tm0) X(tm1) \
  X(tc0) X(tc1) X(tex0) X(tex1) X(hastex) X(cop) X(carg1) X(carg2) X(aop) X(aarg1) X(aarg2) X(tfactor) X(atest)
// clang-format on

enum {
#define CT_GLES_ENUM(n) CT_GLES_U_##n,
  CT_GLES_UNIFORMS(CT_GLES_ENUM)
#undef CT_GLES_ENUM
    CT_GLES_U_COUNT
};

static unsigned ct_gles_compile(ct_gles *g, unsigned type, const char *const *src, int n)
{
  const ct_gl *gl = &g->gl;
  int ok = 0;
  const unsigned s = gl->CreateShader(type);
  gl->ShaderSource(s, n, src, NULL);
  gl->CompileShader(s);
  gl->GetShaderiv(s, CT_GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[400] = "";
    gl->GetShaderInfoLog(s, (int)sizeof log, NULL, log);
    snprintf(g->error, sizeof g->error, "the %s shader does not compile: %s", type == CT_GL_VERTEX_SHADER ? "vertex" : "fragment", log);
    gl->DeleteShader(s);
    return 0;
  }
  return s;
}

static unsigned ct_gles_link(ct_gles *g, const char *vs, const char *fs)
{
  const ct_gl *gl = &g->gl;
  const char *head = g->es ? CT_GLES_VERSION_ES : CT_GLES_VERSION_CORE;
  const char *v[2] = { head, vs }, *f[2] = { head, fs };
  const unsigned a = ct_gles_compile(g, CT_GL_VERTEX_SHADER, v, 2);
  if (!a) return 0;
  const unsigned b = ct_gles_compile(g, CT_GL_FRAGMENT_SHADER, f, 2);
  if (!b) {
    gl->DeleteShader(a);
    return 0;
  }
  const unsigned p = gl->CreateProgram();
  gl->AttachShader(p, a);
  gl->AttachShader(p, b);
  gl->LinkProgram(p);
  gl->DeleteShader(a);
  gl->DeleteShader(b);
  int ok = 0;
  gl->GetProgramiv(p, CT_GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[400] = "";
    gl->GetProgramInfoLog(p, (int)sizeof log, NULL, log);
    snprintf(g->error, sizeof g->error, "the shader program does not link: %s", log);
    gl->DeleteProgram(p);
    return 0;
  }
  return p;
}

/* ---- targets ---- */

static int ct_gles_make_target(ct_gles *g, int i, int w, int h)
{
  const ct_gl *gl = &g->gl;
  g->tgt_w[i] = w;
  g->tgt_h[i] = h;
  gl->GenTextures(1, &g->tgt_tex[i]);
  gl->BindTexture(CT_GL_TEXTURE_2D, g->tgt_tex[i]);
  gl->TexImage2D(CT_GL_TEXTURE_2D, 0, (int)CT_GL_RGBA8, w, h, 0, CT_GL_RGBA, CT_GL_UNSIGNED_BYTE, NULL);
  gl->TexParameteri(CT_GL_TEXTURE_2D, CT_GL_TEXTURE_MIN_FILTER, (int)CT_GL_LINEAR);
  gl->TexParameteri(CT_GL_TEXTURE_2D, CT_GL_TEXTURE_MAG_FILTER, (int)CT_GL_LINEAR);
  gl->GenRenderbuffers(1, &g->tgt_depth[i]);
  gl->BindRenderbuffer(CT_GL_RENDERBUFFER, g->tgt_depth[i]);
  gl->RenderbufferStorage(CT_GL_RENDERBUFFER, CT_GL_DEPTH_COMPONENT24, w, h);
  gl->GenFramebuffers(1, &g->tgt_fbo[i]);
  gl->BindFramebuffer(CT_GL_FRAMEBUFFER, g->tgt_fbo[i]);
  gl->FramebufferTexture2D(CT_GL_FRAMEBUFFER, CT_GL_COLOR_ATTACHMENT0, CT_GL_TEXTURE_2D, g->tgt_tex[i], 0);
  gl->FramebufferRenderbuffer(CT_GL_FRAMEBUFFER, CT_GL_DEPTH_ATTACHMENT, CT_GL_RENDERBUFFER, g->tgt_depth[i]);
  if (gl->CheckFramebufferStatus(CT_GL_FRAMEBUFFER) != CT_GL_FRAMEBUFFER_COMPLETE) {
    snprintf(g->error, sizeof g->error, "the framebuffer of target %d (%dx%d) is not complete", i, w, h);
    return 0;
  }
  return 1;
}

CT_GLES_API int ct_gles_init(ct_gles *g, const ct_gl *gl, int es, gt3_render *render, ct_gles_texture_fn texture_fn, void *texture_user)
{
  memset(g, 0, sizeof *g);
  g->gl = *gl;
  g->es = es;
  g->render = render;
  g->texture_fn = texture_fn;
  g->texture_user = texture_user;
  g->current_target = -1;
  for (int a = 0; a < 2; a++)
    for (int b = 0; b < 5; b++) g->sampler_state[a][b] = -1;
  gl = &g->gl;

  g->program = ct_gles_link(g, ct_gles_vs, ct_gles_fs);
  if (!g->program) return 0;
  g->present_program = ct_gles_link(g, ct_gles_present_vs, ct_gles_present_fs);
  if (!g->present_program) return 0;
  g->present_loc = gl->GetUniformLocation(g->present_program, "u_screen");
  {
    static const char *const names[CT_GLES_U_COUNT] = {
#define CT_GLES_NAME(n) #n,
      CT_GLES_UNIFORMS(CT_GLES_NAME)
#undef CT_GLES_NAME
    };
    for (int i = 0; i < CT_GLES_U_COUNT; i++) {
      char name[32];
      snprintf(name, sizeof name, "u_%s", i == CT_GLES_U_is_double ? "double" : names[i]);
      g->loc[i] = gl->GetUniformLocation(g->program, name);
    }
  }

  gl->GenVertexArrays(1, &g->vao_tri);
  gl->GenVertexArrays(1, &g->vao_quad);
  gl->GenVertexArrays(1, &g->empty_vao);
  gl->GenBuffers(1, &g->vbo);
  gl->GenBuffers(1, &g->ibo);
  gl->GenBuffers(1, &g->quad_ibo);
  gl->GenSamplers(2, g->sampler);
  {
    uint16_t *q = (uint16_t *)malloc((size_t)CT_GLES_QUADS * 6u * sizeof *q);
    if (!q) return 0;
    for (int i = 0; i < CT_GLES_QUADS; i++) {
      const uint16_t b = (uint16_t)(i * 4);
      q[i * 6 + 0] = b;
      q[i * 6 + 1] = (uint16_t)(b + 1);
      q[i * 6 + 2] = (uint16_t)(b + 2);
      q[i * 6 + 3] = b;
      q[i * 6 + 4] = (uint16_t)(b + 2);
      q[i * 6 + 5] = (uint16_t)(b + 3);
    }
    gl->BindVertexArray(g->vao_quad);
    gl->BindBuffer(CT_GL_ELEMENT_ARRAY_BUFFER, g->quad_ibo);
    gl->BufferData(CT_GL_ELEMENT_ARRAY_BUFFER, (ptrdiff_t)((size_t)CT_GLES_QUADS * 6u * sizeof *q), q, CT_GL_STATIC_DRAW);
    free(q);
    gl->BindVertexArray(g->vao_tri);
    gl->BindBuffer(CT_GL_ELEMENT_ARRAY_BUFFER, g->ibo);
  }

  for (int i = 0; i < CT_GLES_TARGETS - 1; i++)
    if (!ct_gles_make_target(g, i, render->view[i + 1].x1 - render->view[i + 1].x0, render->view[i + 1].y1 - render->view[i + 1].y0)) return 0;
  if (!ct_gles_make_target(g, CT_GLES_TARGETS - 1, render->screen_w, render->screen_h)) return 0;
  /* the render targets are textures with the handles 0 ... 9 */
  for (int i = 0; i < CT_GLES_TARGETS - 1; i++) g->tex[i] = g->tgt_tex[i], g->tex_ready[i] = 1;
  gl->BindFramebuffer(CT_GL_FRAMEBUFFER, 0);
  return 1;
}

CT_GLES_API void ct_gles_exit(ct_gles *g)
{
  const ct_gl *gl = &g->gl;
  if (!gl->DeleteTextures) return;
  for (int i = 0; i < CT_GLES_TARGETS; i++) {
    if (g->tgt_fbo[i]) gl->DeleteFramebuffers(1, &g->tgt_fbo[i]);
    if (g->tgt_depth[i]) gl->DeleteRenderbuffers(1, &g->tgt_depth[i]);
    if (g->tgt_tex[i]) gl->DeleteTextures(1, &g->tgt_tex[i]);
  }
  for (int i = CT_GLES_TARGETS - 1; i < CT_GLES_MAX_TEXTURES; i++)
    if (g->tex[i]) gl->DeleteTextures(1, &g->tex[i]);
  if (g->sampler[0]) gl->DeleteSamplers(2, g->sampler);
  if (g->vao_tri) gl->DeleteVertexArrays(1, &g->vao_tri);
  if (g->vao_quad) gl->DeleteVertexArrays(1, &g->vao_quad);
  if (g->empty_vao) gl->DeleteVertexArrays(1, &g->empty_vao);
  if (g->vbo) gl->DeleteBuffers(1, &g->vbo);
  if (g->ibo) gl->DeleteBuffers(1, &g->ibo);
  if (g->quad_ibo) gl->DeleteBuffers(1, &g->quad_ibo);
  if (g->program) gl->DeleteProgram(g->program);
  if (g->present_program) gl->DeleteProgram(g->present_program);
  memset(g, 0, sizeof *g);
}

/* ---- Direct3D to OpenGL ---- */

static unsigned ct_gles_blend_factor(uint32_t d3d)
{
  switch (d3d) {
  case GT3_D3DBLEND_ZERO: return CT_GL_ZERO;
  case GT3_D3DBLEND_ONE: return CT_GL_ONE;
  case GT3_D3DBLEND_SRCCOLOR: return CT_GL_SRC_COLOR;
  case GT3_D3DBLEND_INVSRCCOLOR: return CT_GL_ONE_MINUS_SRC_COLOR;
  case GT3_D3DBLEND_SRCALPHA: return CT_GL_SRC_ALPHA;
  case GT3_D3DBLEND_INVSRCALPHA: return CT_GL_ONE_MINUS_SRC_ALPHA;
  case 7: return CT_GL_DST_ALPHA;
  case 8: return CT_GL_ONE_MINUS_DST_ALPHA;
  case 9: return CT_GL_DST_COLOR;
  case 10: return CT_GL_ONE_MINUS_DST_COLOR;
  default: return CT_GL_ONE;
  }
}

static unsigned ct_gles_compare(uint32_t d3d)
{
  switch (d3d) {
  case 1: return CT_GL_NEVER;
  case 2: return CT_GL_LESS;
  case 3: return CT_GL_EQUAL;
  case GT3_D3DCMP_LESSEQUAL: return CT_GL_LEQUAL;
  case GT3_D3DCMP_GREATER: return CT_GL_GREATER;
  case 6: return CT_GL_NOTEQUAL;
  case 7: return CT_GL_GEQUAL;
  default: return CT_GL_ALWAYS;
  }
}

/* a render state the stream has set, else Direct3D's default */
static uint32_t ct_gles_rs(const gt3_render *r, uint32_t token, uint32_t fallback) { return r->rs_known[token] ? r->rs[token] : fallback; }

static uint32_t ct_gles_ts(const gt3_render *r, int stage, uint32_t type, uint32_t fallback)
{
  return ct_gles_rs(r, 0x100u + (uint32_t)stage * 0x20u + type, fallback);
}

/* the texture of a handle: uploaded when it is first used */
static unsigned ct_gles_texture(ct_gles *g, int32_t handle, int *has_mips)
{
  const ct_gl *gl = &g->gl;
  *has_mips = 0;
  if (handle < 0 || handle >= CT_GLES_MAX_TEXTURES) return 0;
  if (!g->tex_ready[handle]) {
    int32_t w = 0, h = 0;
    const uint8_t *bgra = NULL;
    g->tex_ready[handle] = 1;
    if (g->texture_fn && g->texture_fn(g->texture_user, handle, &w, &h, &bgra) && bgra && w > 0 && h > 0) {
      uint8_t *rgba = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
      if (rgba) {
        for (size_t i = 0; i < (size_t)w * (size_t)h; i++) {
          rgba[i * 4 + 0] = bgra[i * 4 + 2];
          rgba[i * 4 + 1] = bgra[i * 4 + 1];
          rgba[i * 4 + 2] = bgra[i * 4 + 0];
          rgba[i * 4 + 3] = bgra[i * 4 + 3];
        }
        gl->GenTextures(1, &g->tex[handle]);
        gl->BindTexture(CT_GL_TEXTURE_2D, g->tex[handle]);
        gl->PixelStorei(CT_GL_UNPACK_ALIGNMENT, 1);
        gl->TexImage2D(CT_GL_TEXTURE_2D, 0, (int)CT_GL_RGBA8, w, h, 0, CT_GL_RGBA, CT_GL_UNSIGNED_BYTE, rgba);
        gl->GenerateMipmap(CT_GL_TEXTURE_2D);
        g->tex_mips[handle] = 1;
        g->uploads++;
        free(rgba);
      }
    }
  }
  *has_mips = g->tex_mips[handle];
  return g->tex[handle];
}

/* sampler states of a stage, as the stream has set them: filters, mip filter, addressing */
static void ct_gles_sampler(ct_gles *g, int stage, int has_mips)
{
  const ct_gl *gl = &g->gl;
  const gt3_render *r = g->render;
  const uint32_t base = 0x200u + (uint32_t)stage * 0x10u;
  const uint32_t mag = ct_gles_rs(r, base + 5, GT3_D3DTEXF_POINT), min = ct_gles_rs(r, base + 6, GT3_D3DTEXF_POINT),
                 mip = ct_gles_rs(r, base + 7, GT3_D3DTEXF_NONE);
  const uint32_t au = ct_gles_rs(r, base + 1, GT3_D3DTADDRESS_WRAP), av = ct_gles_rs(r, base + 2, GT3_D3DTADDRESS_WRAP);
  int minf;
  if (!has_mips || mip == GT3_D3DTEXF_NONE)
    minf = min == GT3_D3DTEXF_LINEAR ? (int)CT_GL_LINEAR : (int)CT_GL_NEAREST;
  else if (mip == GT3_D3DTEXF_LINEAR)
    minf = min == GT3_D3DTEXF_LINEAR ? (int)CT_GL_LINEAR_MIPMAP_LINEAR : (int)CT_GL_NEAREST_MIPMAP_LINEAR;
  else
    minf = min == GT3_D3DTEXF_LINEAR ? (int)CT_GL_LINEAR_MIPMAP_NEAREST : (int)CT_GL_NEAREST_MIPMAP_NEAREST;
  const int want[5] = { minf, mag == GT3_D3DTEXF_LINEAR ? (int)CT_GL_LINEAR : (int)CT_GL_NEAREST,
                        au == GT3_D3DTADDRESS_CLAMP ? (int)CT_GL_CLAMP_TO_EDGE : (int)CT_GL_REPEAT,
                        av == GT3_D3DTADDRESS_CLAMP ? (int)CT_GL_CLAMP_TO_EDGE : (int)CT_GL_REPEAT, 0 };
  const unsigned pn[4] = { CT_GL_TEXTURE_MIN_FILTER, CT_GL_TEXTURE_MAG_FILTER, CT_GL_TEXTURE_WRAP_S, CT_GL_TEXTURE_WRAP_T };
  for (int i = 0; i < 4; i++)
    if (g->sampler_state[stage][i] != want[i]) {
      gl->SamplerParameteri(g->sampler[stage], pn[i], want[i]);
      g->sampler_state[stage][i] = want[i];
    }
}

/* ---- hooks ---- */

typedef struct ct_gles_round {
  int mode;
} ct_gles_round;

static ct_gles_round ct_gles_nearest(void)
{
  ct_gles_round r;
  r.mode = fegetround();
  fesetround(FE_TONEAREST);
  return r;
}

static void ct_gles_restore(ct_gles_round r) { fesetround(r.mode); }

CT_GLES_API void ct_gles_on_viewport(void *user, gt3_render *r, const gt3_fxview *view)
{
  ct_gles *g = (ct_gles *)user;
  const ct_gl *gl = &g->gl;
  const ct_gles_round round = ct_gles_nearest();
  const int target = r->rt_handle >= 0 && r->rt_handle < CT_GLES_TARGETS - 1 ? r->rt_handle : CT_GLES_TARGETS - 1;
  gl->BindFramebuffer(CT_GL_FRAMEBUFFER, g->tgt_fbo[target]);
  g->current_target = target;
  gl->Viewport(0, 0, r->dview_w, r->dview_h);
  gl->ColorMask(1, 1, 1, 1);
  gl->DepthMask(1);
  unsigned mask = 0;
  if (view->clear_flags & 1u) {
    const uint32_t c = view->clear_color;
    gl->ClearColor((float)((c >> 16) & 255u) / 255.0f, (float)((c >> 8) & 255u) / 255.0f, (float)(c & 255u) / 255.0f, (float)((c >> 24) & 255u) / 255.0f);
    mask |= CT_GL_COLOR_BUFFER_BIT;
  }
  if (view->clear_flags & 2u) {
    gl->ClearDepthf(1.0f);
    mask |= CT_GL_DEPTH_BUFFER_BIT;
  }
  if (mask) gl->Clear(mask);
  ct_gles_restore(round);
}

static void ct_gles_matrix(const ct_gles *g, int loc, const gt3_mat *m) { g->gl.UniformMatrix4fv(g->loc[loc], 1, 0, &m->i.x); }

CT_GLES_API void ct_gles_on_draw(void *user, gt3_geodev *dev, int32_t handle)
{
  ct_gles *g = (ct_gles *)user;
  const ct_gl *gl = &g->gl;
  gt3_render *r = g->render;
  const gt3_geo_handle *h = &dev->handle[handle];
  if (h->vcount <= 0) return;
  const ct_gles_round round = ct_gles_nearest();
  const int is_double = h->fvf == GT3_FVF_DOUBLE;
  const int quad = h->mode == GT3_GEO_QUAD;

  gl->UseProgram(g->program);
  gl->BindVertexArray(quad ? g->vao_quad : g->vao_tri);

  /* geometry */
  gl->BindBuffer(CT_GL_ARRAY_BUFFER, g->vbo);
  gl->BufferData(CT_GL_ARRAY_BUFFER, (ptrdiff_t)h->vcount * 32, dev->buffer[h->vbuf].mem + h->vstart, CT_GL_STREAM_DRAW);
  gl->EnableVertexAttribArray(0);
  gl->VertexAttribPointer(0, 3, CT_GL_FLOAT, 0, 32, (const void *)(uintptr_t)0);
  if (is_double) {
    gl->DisableVertexAttribArray(1);
    gl->EnableVertexAttribArray(2);
    gl->VertexAttribPointer(2, 4, CT_GL_UNSIGNED_BYTE, 1, 32, (const void *)(uintptr_t)12);
    gl->EnableVertexAttribArray(3);
    gl->VertexAttribPointer(3, 2, CT_GL_FLOAT, 0, 32, (const void *)(uintptr_t)16);
    gl->EnableVertexAttribArray(4);
    gl->VertexAttribPointer(4, 2, CT_GL_FLOAT, 0, 32, (const void *)(uintptr_t)24);
  } else {
    gl->EnableVertexAttribArray(1);
    gl->VertexAttribPointer(1, 3, CT_GL_FLOAT, 0, 32, (const void *)(uintptr_t)12);
    gl->DisableVertexAttribArray(2);
    gl->EnableVertexAttribArray(3);
    gl->VertexAttribPointer(3, 2, CT_GL_FLOAT, 0, 32, (const void *)(uintptr_t)24);
    gl->DisableVertexAttribArray(4);
  }

  /* matrices and target */
  ct_gles_matrix(g, CT_GLES_U_world, &r->xf_world);
  ct_gles_matrix(g, CT_GLES_U_view, &r->xf_view);
  ct_gles_matrix(g, CT_GLES_U_proj, &r->xf_proj);
  { /* the third component is the y sign: everything is rendered upside down */
    const float t[4] = { (float)r->dview_w, (float)r->dview_h, -1.0f, 0.0f };
    gl->Uniform4fv(g->loc[CT_GLES_U_target], 1, t);
  }
  gl->Uniform1i(g->loc[CT_GLES_U_is_double], is_double);

  /* lighting */
  gl->Uniform1i(g->loc[CT_GLES_U_lighting], (int)ct_gles_rs(r, GT3_D3DRS_LIGHTING, 0) != 0);
  gl->Uniform1i(g->loc[CT_GLES_U_renorm], (int)ct_gles_rs(r, GT3_D3DRS_NORMALIZENORMALS, 0) != 0);
  gl->Uniform1i(g->loc[CT_GLES_U_specular], (int)ct_gles_rs(r, GT3_D3DRS_SPECULARENABLE, 0) != 0);
  gl->Uniform4i(g->loc[CT_GLES_U_src], (int)ct_gles_rs(r, GT3_D3DRS_DIFFUSEMATERIALSOURCE, GT3_D3DMCS_COLOR1),
                (int)ct_gles_rs(r, GT3_D3DRS_AMBIENTMATERIALSOURCE, GT3_D3DMCS_MATERIAL),
                (int)ct_gles_rs(r, GT3_D3DRS_SPECULARMATERIALSOURCE, GT3_D3DMCS_MATERIAL),
                (int)ct_gles_rs(r, GT3_D3DRS_EMISSIVEMATERIALSOURCE, GT3_D3DMCS_MATERIAL));
  {
    float mat[20];
    memcpy(mat, r->material, 16 * sizeof(float));
    mat[16] = r->material[16];
    mat[17] = mat[18] = mat[19] = 0.0f;
    gl->Uniform4fv(g->loc[CT_GLES_U_mat], 5, mat);
    float ldiff[32], lamb[32], lspec[32], lpos[32], ldir[32], lspot[32];
    int n = 0;
    memset(ldiff, 0, sizeof ldiff);
    memset(lamb, 0, sizeof lamb);
    memset(lspec, 0, sizeof lspec);
    memset(lpos, 0, sizeof lpos);
    memset(ldir, 0, sizeof ldir);
    memset(lspot, 0, sizeof lspot);
    for (int i = 0; i < r->light_count && n < CT_GLES_MAX_LIGHTS; i++) {
      if (!r->light_on[i]) continue;
      const gt3_d3dlight *l = &r->light[i];
      memcpy(ldiff + n * 4, l->diffuse, 16);
      memcpy(lamb + n * 4, l->ambient, 16);
      memcpy(lspec + n * 4, l->specular, 16);
      lpos[n * 4 + 0] = l->position[0];
      lpos[n * 4 + 1] = l->position[1];
      lpos[n * 4 + 2] = l->position[2];
      lpos[n * 4 + 3] = (float)l->type;
      ldir[n * 4 + 0] = l->direction[0];
      ldir[n * 4 + 1] = l->direction[1];
      ldir[n * 4 + 2] = l->direction[2];
      ldir[n * 4 + 3] = l->range;
      lspot[n * 4 + 0] = l->falloff;
      lspot[n * 4 + 1] = l->theta;
      lspot[n * 4 + 2] = l->phi;
      n++;
    }
    gl->Uniform1i(g->loc[CT_GLES_U_nlights], n);
    gl->Uniform4fv(g->loc[CT_GLES_U_ldiff], 8, ldiff);
    gl->Uniform4fv(g->loc[CT_GLES_U_lamb], 8, lamb);
    gl->Uniform4fv(g->loc[CT_GLES_U_lspec], 8, lspec);
    gl->Uniform4fv(g->loc[CT_GLES_U_lpos], 8, lpos);
    gl->Uniform4fv(g->loc[CT_GLES_U_ldir], 8, ldir);
    gl->Uniform4fv(g->loc[CT_GLES_U_lspot], 8, lspot);
  }

  /* texture stages */
  {
    int cop[4], carg1[4], carg2[4], aop[4], aarg1[4], aarg2[4], tc[2][4], has[2] = { 0, 0 };
    for (int s = 0; s < 3; s++) {
      cop[s] = (int)ct_gles_ts(r, s, GT3_D3DTSS_COLOROP & 0xff, s == 0 ? GT3_D3DTOP_MODULATE : GT3_D3DTOP_DISABLE);
      carg1[s] = (int)ct_gles_ts(r, s, GT3_D3DTSS_COLORARG1 & 0xff, GT3_D3DTA_TEXTURE);
      carg2[s] = (int)ct_gles_ts(r, s, GT3_D3DTSS_COLORARG2 & 0xff, GT3_D3DTA_CURRENT);
      aop[s] = (int)ct_gles_ts(r, s, GT3_D3DTSS_ALPHAOP & 0xff, s == 0 ? GT3_D3DTOP_SELECTARG1 : GT3_D3DTOP_DISABLE);
      aarg1[s] = (int)ct_gles_ts(r, s, GT3_D3DTSS_ALPHAARG1 & 0xff, GT3_D3DTA_TEXTURE);
      aarg2[s] = (int)ct_gles_ts(r, s, GT3_D3DTSS_ALPHAARG2 & 0xff, GT3_D3DTA_CURRENT);
    }
    cop[3] = carg1[3] = carg2[3] = aop[3] = aarg1[3] = aarg2[3] = 0;
    gl->Uniform4i(g->loc[CT_GLES_U_cop], cop[0], cop[1], cop[2], cop[3]);
    gl->Uniform4i(g->loc[CT_GLES_U_carg1], carg1[0], carg1[1], carg1[2], carg1[3]);
    gl->Uniform4i(g->loc[CT_GLES_U_carg2], carg2[0], carg2[1], carg2[2], carg2[3]);
    gl->Uniform4i(g->loc[CT_GLES_U_aop], aop[0], aop[1], aop[2], aop[3]);
    gl->Uniform4i(g->loc[CT_GLES_U_aarg1], aarg1[0], aarg1[1], aarg1[2], aarg1[3]);
    gl->Uniform4i(g->loc[CT_GLES_U_aarg2], aarg2[0], aarg2[1], aarg2[2], aarg2[3]);
    for (int s = 0; s < 2; s++) {
      const uint32_t index = ct_gles_ts(r, s, GT3_D3DTSS_TEXCOORDINDEX & 0xff, 0);
      tc[s][0] = (int)(index & 0xffffu);
      tc[s][1] = (int)(index >> 16);
      tc[s][2] = ct_gles_ts(r, s, GT3_D3DTSS_TEXTURETRANSFORMFLAGS & 0xff, 0) != 0;
      tc[s][3] = 0;
      gl->Uniform4i(g->loc[s ? CT_GLES_U_tc1 : CT_GLES_U_tc0], tc[s][0], tc[s][1], tc[s][2], tc[s][3]);
      ct_gles_matrix(g, s ? CT_GLES_U_tm1 : CT_GLES_U_tm0, &r->tex_mat[s]);
      gl->BindSampler((unsigned)s, g->sampler[s]);
    }
    /* the uploads bind to the active unit: all of them first */
    unsigned tex[2] = { 0, 0 };
    int mips[2] = { 0, 0 };
    for (int s = 0; s < 2; s++) tex[s] = r->tex_bound[s] >= 0 ? ct_gles_texture(g, r->tex_bound[s], &mips[s]) : 0;
    for (int s = 0; s < 2; s++) {
      has[s] = tex[s] != 0;
      gl->ActiveTexture(CT_GL_TEXTURE0 + (unsigned)s);
      gl->BindTexture(CT_GL_TEXTURE_2D, tex[s]);
      ct_gles_sampler(g, s, mips[s]);
    }
    gl->Uniform1i(g->loc[CT_GLES_U_tex0], 0);
    gl->Uniform1i(g->loc[CT_GLES_U_tex1], 1);
    gl->Uniform2i(g->loc[CT_GLES_U_hastex], has[0], has[1]);
  }
  {
    const uint32_t tf = ct_gles_rs(r, GT3_D3DRS_TEXTUREFACTOR, 0xffffffffu);
    const float f[4] = { (float)((tf >> 16) & 255u) / 255.0f, (float)((tf >> 8) & 255u) / 255.0f, (float)(tf & 255u) / 255.0f,
                         (float)((tf >> 24) & 255u) / 255.0f };
    gl->Uniform4fv(g->loc[CT_GLES_U_tfactor], 1, f);
  }
  gl->Uniform3i(g->loc[CT_GLES_U_atest], (int)ct_gles_rs(r, GT3_D3DRS_ALPHATESTENABLE, 0) != 0, (int)ct_gles_rs(r, GT3_D3DRS_ALPHAFUNC, GT3_D3DCMP_GREATER),
                (int)ct_gles_rs(r, GT3_D3DRS_ALPHAREF, 0));

  /* raster state */
  {
    const uint32_t cull = ct_gles_rs(r, GT3_D3DRS_CULLMODE, GT3_D3DCULL_CCW);
    if (cull == GT3_D3DCULL_NONE)
      gl->Disable(CT_GL_CULL_FACE);
    else {
      gl->Enable(CT_GL_CULL_FACE);
      gl->CullFace(CT_GL_BACK);
      gl->FrontFace(cull == GT3_D3DCULL_CW ? CT_GL_CW : CT_GL_CCW); /* the picture is upside down: the winding turns with it, and the keep side is the same */
    }
    gl->Enable(CT_GL_DEPTH_TEST);
    gl->DepthFunc(ct_gles_compare(ct_gles_rs(r, GT3_D3DRS_ZFUNC, GT3_D3DCMP_LESSEQUAL)));
    gl->DepthMask(ct_gles_rs(r, GT3_D3DRS_ZWRITEENABLE, 1) != 0);
    const uint32_t cw = ct_gles_rs(r, GT3_D3DRS_COLORWRITEENABLE, 15);
    gl->ColorMask((cw & 1u) != 0, (cw & 2u) != 0, (cw & 4u) != 0, (cw & 8u) != 0);
    if (ct_gles_rs(r, GT3_D3DRS_ALPHABLENDENABLE, 0)) {
      gl->Enable(CT_GL_BLEND);
      gl->BlendFunc(ct_gles_blend_factor(ct_gles_rs(r, GT3_D3DRS_SRCBLEND, GT3_D3DBLEND_ONE)),
                    ct_gles_blend_factor(ct_gles_rs(r, GT3_D3DRS_DESTBLEND, GT3_D3DBLEND_ZERO)));
      const uint32_t op = ct_gles_rs(r, GT3_D3DRS_BLENDOP, GT3_D3DBLENDOP_ADD);
      gl->BlendEquation(op == 2 ? CT_GL_FUNC_SUBTRACT : op == 3 ? CT_GL_FUNC_REVERSE_SUBTRACT : CT_GL_FUNC_ADD);
    } else
      gl->Disable(CT_GL_BLEND);
  }

  /* the draw call */
  if (quad) {
    int quads = h->vcount / 4;
    if (quads > CT_GLES_QUADS) quads = CT_GLES_QUADS;
    gl->DrawElements(CT_GL_TRIANGLES, quads * 6, CT_GL_UNSIGNED_SHORT, (const void *)(uintptr_t)0);
  } else if (h->icount > 0 && h->ibuf) {
    gl->BindBuffer(CT_GL_ELEMENT_ARRAY_BUFFER, g->ibo);
    gl->BufferData(CT_GL_ELEMENT_ARRAY_BUFFER, (ptrdiff_t)h->icount * 2, dev->buffer[h->ibuf].mem + h->istart, CT_GL_STREAM_DRAW);
    gl->DrawElements(CT_GL_TRIANGLES, h->icount / 3 * 3, CT_GL_UNSIGNED_SHORT, (const void *)(uintptr_t)0);
  } else
    gl->DrawArrays(CT_GL_TRIANGLES, 0, h->vcount / 3 * 3);
  g->draws++;
  ct_gles_restore(round);
}

CT_GLES_API void ct_gles_present(ct_gles *g, int x, int y, int w, int h)
{
  const ct_gl *gl = &g->gl;
  const ct_gles_round round = ct_gles_nearest();
  gl->BindFramebuffer(CT_GL_FRAMEBUFFER, 0);
  gl->Viewport(x, y, w, h);
  gl->Disable(CT_GL_DEPTH_TEST);
  gl->Disable(CT_GL_BLEND);
  gl->Disable(CT_GL_CULL_FACE);
  gl->ColorMask(1, 1, 1, 1);
  gl->UseProgram(g->present_program);
  gl->BindVertexArray(g->empty_vao);
  gl->ActiveTexture(CT_GL_TEXTURE0);
  gl->BindTexture(CT_GL_TEXTURE_2D, g->tgt_tex[CT_GLES_TARGETS - 1]);
  gl->BindSampler(0, 0);
  gl->TexParameteri(CT_GL_TEXTURE_2D, CT_GL_TEXTURE_MIN_FILTER, (int)CT_GL_LINEAR);
  gl->TexParameteri(CT_GL_TEXTURE_2D, CT_GL_TEXTURE_MAG_FILTER, (int)CT_GL_LINEAR);
  gl->TexParameteri(CT_GL_TEXTURE_2D, CT_GL_TEXTURE_WRAP_S, (int)CT_GL_CLAMP_TO_EDGE);
  gl->TexParameteri(CT_GL_TEXTURE_2D, CT_GL_TEXTURE_WRAP_T, (int)CT_GL_CLAMP_TO_EDGE);
  gl->Uniform1i(g->present_loc, 0);
  gl->DrawArrays(CT_GL_TRIANGLES, 0, 3);
  gl->Enable(CT_GL_DEPTH_TEST);
  ct_gles_restore(round);
}

CT_GLES_API void ct_gles_read_screen(ct_gles *g, uint8_t *rgba)
{
  const ct_gl *gl = &g->gl;
  const ct_gles_round round = ct_gles_nearest();
  gl->BindFramebuffer(CT_GL_FRAMEBUFFER, g->tgt_fbo[CT_GLES_TARGETS - 1]);
  gl->PixelStorei(CT_GL_PACK_ALIGNMENT, 1);
  gl->ReadPixels(0, 0, g->tgt_w[CT_GLES_TARGETS - 1], g->tgt_h[CT_GLES_TARGETS - 1], CT_GL_RGBA, CT_GL_UNSIGNED_BYTE, rgba);
  ct_gles_restore(round);
}

#endif /* CT_GLES_IMPLEMENTATION_DONE */
#endif /* CT_GLES_IMPLEMENTATION */
