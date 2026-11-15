/* render.h - the geometry the intro hands to its graphics device - header-only C99 (stb style)

   The part of the renderer that does not need a graphics API: what GenMesh::Prepare / DoPass1 / DoPass2 and the effect chain's render handlers make of meshes and
   quads, and the device's buffer management that decides which vertices go where (the oracle's sSystem_::Geo* functions, which are the original's _startintro.cpp
   with system memory in place of Direct3D buffers). The result is a stream of "geometry uploaded" and "geometry drawn" events: gt3_geodev.on_end is called at every
   GeoEnd with the filled buffer range, gt3_geodev.on_draw at every draw. A backend (OpenGL ES) takes its vertices from the same buffers the events point to.

   Handles, buffers and the discard counter follow the original exactly (a dynamic geometry is uploaded again whenever the shared dynamic buffers were discarded
   since its last upload; a static geometry once), so the stream is the original's, in number, order, sizes and bytes.

   All vertices are 32 bytes: FVF_STANDARD x, y, z, nx, ny, nz, u, v and FVF_DOUBLE x, y, z, colour, u0, v0, u1, v1.

   Needs the mesh engine of mesh.h in the same translation unit (it uses its vector and matrix helpers). The floating point is the single precision of the original's x87 unit
   in round-down mode, see csl_natives.h gt3_round_down().

   In ONE .c file define GT3_RENDER_IMPLEMENTATION before including; GT3_RENDER_STATIC makes the functions static. */
#ifndef GT3_RENDER_H_INCLUDED
#define GT3_RENDER_H_INCLUDED

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <candytron/core/mesh.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GT3_RENDER_API
#ifdef GT3_RENDER_STATIC
#define GT3_RENDER_API static
#else
#define GT3_RENDER_API extern
#endif
#endif

/* _start.hpp */
enum { GT3_GEO_TRI = 3, GT3_GEO_QUAD = 6, GT3_GEO_STATIC = 8 };
enum { GT3_FVF_STANDARD = 1, GT3_FVF_DOUBLE = 2 };
/* genmaterial.hpp */
enum {
  GT3_MPP_OFF = 0,
  GT3_MPP_STATIC = 1,
  GT3_MPP_DYNAMIC = 2,
  GT3_MPP_SPRITES = 3,
  GT3_MPP_TREES = 4,
  GT3_MPP_THICKLINES = 5,
  GT3_MPP_OUTLINES = 6,
  GT3_MPP_SHADOW = 7,
  GT3_MPP_SPIKES = 8,
  GT3_MPP_FINNS = 9
};
/* _startprv.hpp */
enum { GT3_MAX_GEOHANDLE = 256, GT3_MAX_GEOBUFFER = 128, GT3_MAX_DYNVBSIZE = 48 * 0x8000, GT3_MAX_DYNIBSIZE = 8 * 0x8000 };
/* the viewports of the effect chains: the screen, then 1 + 3 + 3 + 3 render targets */
enum { GT3_FX_VIEWPORTS = 11 };
/* sSystem->GetTransform */
enum { GT3_GT_VIEW = 1, GT3_GT_MODELVIEW = 2, GT3_GT_MODEL = 3 };
/* the device's state tables: tokens below 0x100 are render states, 0x100.. texture stage states (stage << 5 | type), 0x200.. sampler states */
enum { GT3_NSTATE = 0x300, GT3_MAX_LIGHTS = 64, GT3_MAX_STAGES = 8 };

/* the Direct3D 9 numbers of the state streams and the original's material flags (_startdx.hpp, _start.hpp) */
enum {
  GT3_D3DTSS_TCI_CAMERASPACENORMAL = 0x00010000,
  GT3_D3DTSS_TCI_CAMERASPACEPOSITION = 0x00020000,
  GT3_D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR = 0x00030000,
  GT3_D3DBLENDOP_ADD = 1,
  GT3_D3DBLEND_INVSRCALPHA = 6,
  GT3_D3DBLEND_INVSRCCOLOR = 4,
  GT3_D3DBLEND_ONE = 2,
  GT3_D3DBLEND_SRCALPHA = 5,
  GT3_D3DBLEND_SRCCOLOR = 3,
  GT3_D3DBLEND_ZERO = 1,
  GT3_D3DCMP_ALWAYS = 8,
  GT3_D3DCMP_GREATER = 5,
  GT3_D3DCMP_LESSEQUAL = 4,
  GT3_D3DCULL_CCW = 3,
  GT3_D3DCULL_CW = 2,
  GT3_D3DCULL_NONE = 1,
  GT3_D3DMAT_AMBIENT = 0x0311,
  GT3_D3DMAT_DIFFUSE = 0x0310,
  GT3_D3DMAT_EMISSIVE = 0x0313,
  GT3_D3DMAT_POWER = 0x0314,
  GT3_D3DMAT_SPECULAR = 0x0312,
  GT3_D3DMCS_COLOR1 = 1,
  GT3_D3DMCS_MATERIAL = 0,
  GT3_D3DRS_ALPHABLENDENABLE = 27,
  GT3_D3DRS_ALPHAFUNC = 25,
  GT3_D3DRS_ALPHAREF = 24,
  GT3_D3DRS_ALPHATESTENABLE = 15,
  GT3_D3DRS_AMBIENTMATERIALSOURCE = 147,
  GT3_D3DRS_BLENDOP = 171,
  GT3_D3DRS_CULLMODE = 22,
  GT3_D3DRS_DESTBLEND = 20,
  GT3_D3DRS_DIFFUSEMATERIALSOURCE = 145,
  GT3_D3DRS_EMISSIVEMATERIALSOURCE = 148,
  GT3_D3DRS_LIGHTING = 137,
  GT3_D3DRS_NORMALIZENORMALS = 143,
  GT3_D3DRS_SPECULARENABLE = 29,
  GT3_D3DRS_SPECULARMATERIALSOURCE = 146,
  GT3_D3DRS_SRCBLEND = 19,
  GT3_D3DRS_STENCILENABLE = 52,
  GT3_D3DRS_STENCILFAIL = 53,
  GT3_D3DRS_STENCILFUNC = 56,
  GT3_D3DRS_STENCILPASS = 55,
  GT3_D3DRS_STENCILREF = 57,
  GT3_D3DRS_STENCILZFAIL = 54,
  GT3_D3DRS_TEXTUREFACTOR = 60,
  GT3_D3DRS_ZFUNC = 23,
  GT3_D3DRS_ZWRITEENABLE = 14,
  GT3_D3DSAMP_1 = 0x010,
  GT3_D3DSAMP_ADDRESSU = (1 | 0x200),
  GT3_D3DSAMP_ADDRESSV = (2 | 0x200),
  GT3_D3DSAMP_MAGFILTER = (5 | 0x200),
  GT3_D3DSAMP_MINFILTER = (6 | 0x200),
  GT3_D3DSAMP_MIPFILTER = (7 | 0x200),
  GT3_D3DTADDRESS_CLAMP = 3,
  GT3_D3DTADDRESS_WRAP = 1,
  GT3_D3DTA_ALPHAREPLICATE = 0x00000020,
  GT3_D3DTA_CURRENT = 0x00000001,
  GT3_D3DTA_DIFFUSE = 0x00000000,
  GT3_D3DTA_TEXTURE = 0x00000002,
  GT3_D3DTA_TFACTOR = 0x00000003,
  GT3_D3DTEXF_LINEAR = 2,
  GT3_D3DTEXF_NONE = 0,
  GT3_D3DTEXF_POINT = 1,
  GT3_D3DTOP_ADD = 7,
  GT3_D3DTOP_ADDSMOOTH = 11,
  GT3_D3DTOP_BLENDDIFFUSEALPHA = 12,
  GT3_D3DTOP_DISABLE = 1,
  GT3_D3DTOP_MODULATE = 4,
  GT3_D3DTOP_MODULATE4X = 6,
  GT3_D3DTOP_MODULATEALPHA_ADDCOLOR = 18,
  GT3_D3DTOP_SELECTARG1 = 2,
  GT3_D3DTOP_SELECTARG2 = 3,
  GT3_D3DTOP_SUBTRACT = 10,
  GT3_D3DTSS_0 = 0x00,
  GT3_D3DTSS_1 = 0x20,
  GT3_D3DTSS_2 = 0x40,
  GT3_D3DTSS_ALPHAARG1 = (5 | 0x100),
  GT3_D3DTSS_ALPHAARG2 = (6 | 0x100),
  GT3_D3DTSS_ALPHAOP = (4 | 0x100),
  GT3_D3DTSS_COLORARG1 = (2 | 0x100),
  GT3_D3DTSS_COLORARG2 = (3 | 0x100),
  GT3_D3DTSS_COLOROP = (1 | 0x100),
  GT3_D3DTSS_TEXCOORDINDEX = (11 | 0x100),
  GT3_D3DTSS_TEXTURETRANSFORMFLAGS = (24 | 0x100),
  GT3_D3DTTFF_COUNT2 = 2,
  GT3_MBF_BLENDADD = 0x2000,
  GT3_MBF_BLENDALPHA = 0x1000,
  GT3_MBF_BLENDMUL = 0x3000,
  GT3_MBF_BLENDOFF = 0x0000,
  GT3_MBF_BLENDSMOOTH = 0x4000,
  GT3_MBF_COLOR = 0x0008,
  GT3_MBF_DOUBLESIDED = 0x0001,
  GT3_MBF_INVERTCULL = 0x0010,
  GT3_MBF_LIGHT = 0x0004,
  GT3_MBF_RENORMALIZE = 0x0002,
  GT3_MBF_ZOFF = 0x0000,
  GT3_MBF_ZON = 0x0300,
  GT3_MBF_ZONLY = 0x0800,
  GT3_MBF_ZREAD = 0x0200,
  GT3_MBF_ZWRITE = 0x0100,
  GT3_MBM_ADD = 3,
  GT3_MBM_ADDCOL = 8,
  GT3_MBM_ADDSMOOTH = 7,
  GT3_MBM_ADDSMOOTHCOL = 12,
  GT3_MBM_BLEND = 5,
  GT3_MBM_BLENDCOL = 10,
  GT3_MBM_FLAT = 1,
  GT3_MBM_MUL = 4,
  GT3_MBM_MULCOL = 9,
  GT3_MBM_SUB = 6,
  GT3_MBM_SUBCOL = 11,
  GT3_MBM_TEX = 2,
  GT3_MTF_CLAMP = 0x0008,
  GT3_MTF_ENVIMASK = 0x0800,
  GT3_MTF_FILTER = 0x0003,
  GT3_MTF_FILTERMAG = 0x0002,
  GT3_MTF_FILTERMIN = 0x0001,
  GT3_MTF_MIPMAP = 0x0004,
  GT3_MTF_UVENVI = 0x0800,
  GT3_MTF_UVMASK = 0x0f00,
  GT3_MTF_UVPOS = 0x0a00,
  GT3_MTF_UVSHIFT = 8,
  GT3_MTF_UVSPEC = 0x0900,
  GT3_MTF_UVTRANS = 0x0010,
  GT3_D3DRS_COLORWRITEENABLE = 168,
  GT3_MTF_UV1 = 0x0100,
  GT3_VCF_COLOR = 1,
};

/* sD3DLight: 26 words */
typedef struct gt3_d3dlight {
  int32_t type;
  float diffuse[4], specular[4], ambient[4];
  float position[3], direction[3];
  float range, falloff, attenuation[3], theta, phi;
} gt3_d3dlight;

/* GenLight as the script made it: the members GenLight::Set reads */
typedef struct gt3_light_def {
  int32_t diffuse[4], ambient[4], specular[4];
  float range, rangecut, amplify, falloff, inner, outer;
  int32_t mode;
  uint32_t mask;
} gt3_light_def;

/* a line of the oracle's draw.txt format (VIEWPORT, CAMERA, M ..., S, T, LIGHT ...), for the tests */
typedef void (*gt3_trace_fn)(void *user, const char *line);

typedef struct gt3_geo_handle {
  int32_t mode;                 /* GT3_GEO_*, 0: free */
  uint32_t fvf, discard;
  int32_t ibuf, istart, icount; /* indices: buffer, byte offset, count (0: none) */
  int32_t vbuf, vstart, vcount; /* vertices: buffer (0: the dynamic one), byte offset, count (0: not loaded) */
} gt3_geo_handle;

typedef struct gt3_geo_buffer {
  uint8_t *mem;
  int32_t size, used, users;
} gt3_geo_buffer;

struct gt3_geodev;
typedef struct gt3_geodev {
  gt3_geo_handle handle[GT3_MAX_GEOHANDLE];
  gt3_geo_buffer buffer
    [GT3_MAX_GEOBUFFER]; /* 0: dynamic vertices, 1: dynamic indices, 2: the static quad indices (the backend's), 3...: static vertices, 127...: static indices */
  uint32_t discard;
  int32_t this_vertex, this_index;
  uint32_t n_geo, n_draw;
  /* called when a geometry has been filled (GeoEnd): the handle's vertex and index range are in `buffer` (gt3_geo_vertices / gt3_geo_indices) */
  void (*on_end)(void *user, struct gt3_geodev *dev, int32_t handle);
  /* called for every draw call, the handle says what (mode, fvf, vertex and index range) */
  void (*on_draw)(void *user, struct gt3_geodev *dev, int32_t handle);
  void *user;
} gt3_geodev;

GT3_RENDER_API int gt3_geodev_init(gt3_geodev *d);
GT3_RENDER_API void gt3_geodev_exit(gt3_geodev *d);
GT3_RENDER_API int32_t gt3_geo_add(gt3_geodev *d, uint32_t fvf, int32_t prim);
GT3_RENDER_API void gt3_geo_rem(gt3_geodev *d, int32_t handle);
GT3_RENDER_API void gt3_geo_flush(gt3_geodev *d, int32_t handle);
/* returns 1 when the geometry has to be uploaded (nothing is drawn), 0 when it was drawn */
GT3_RENDER_API int gt3_geo_draw(gt3_geodev *d, int32_t handle);
GT3_RENDER_API void gt3_geo_begin(gt3_geodev *d, int32_t handle, int32_t vc, int32_t ic, float **fp, uint16_t **ip);
/* vc = -1: keep the counts of GeoBegin */
GT3_RENDER_API void gt3_geo_end(gt3_geodev *d, int32_t handle, int32_t vc, int32_t ic);
GT3_RENDER_API const uint8_t *gt3_geo_vertices(const gt3_geodev *d, int32_t handle);
GT3_RENDER_API const uint16_t *gt3_geo_indices(const gt3_geodev *d, int32_t handle);

/* ---- the camera and the viewports of the effect chains ---- */

typedef struct gt3_cam {
  gt3_mat pos;
  float zoom_x, zoom_y, aspect_x, aspect_y, center_x, center_y, near_clip, far_clip;
} gt3_cam;

/* FXViewport: the screen (screen = 0, render_target = -1) or a render target (screen = -1) */
typedef struct gt3_fxview {
  int32_t screen, render_target;
  uint32_t clear_flags, clear_color;
  int32_t x0, y0, x1, y1;
  int32_t ref_count, size;
  float xscale, yscale;
} gt3_fxview;

typedef struct gt3_render {
  gt3_geodev dev;
  int32_t screen_w, screen_h;
  gt3_fxview view[GT3_FX_VIEWPORTS];
  int32_t n_view;
  /* sSystem_: LastCamera, LastMatrix, LastTransform (= LastMatrix * LastCamera) */
  gt3_mat last_camera, last_matrix, last_transform;
  gt3_mat xf_view, xf_proj, xf_world;
  int32_t dview_w, dview_h, rt_handle;
  int32_t render_counter; /* GenFXChain::Render's static RenderIDCounter */
  uint32_t n_viewports;   /* BeginViewport calls */
  /* the Direct3D state a draw call depends on */
  uint32_t rs[GT3_NSTATE], rs_emitted[GT3_NSTATE];
  uint8_t rs_known[GT3_NSTATE], rs_emitted_known[GT3_NSTATE];
  int32_t tex_bound[GT3_MAX_STAGES], tex_emitted[GT3_MAX_STAGES];
  gt3_mat tex_mat[GT3_MAX_STAGES];
  gt3_d3dlight light[GT3_MAX_LIGHTS];
  uint32_t light_mask[GT3_MAX_LIGHTS];
  int32_t light_on[GT3_MAX_LIGHTS], light_count;
  float material[17]; /* diffuse, ambient, specular, emissive (4 each), power */
  /* the host's hooks: geometry filled, geometry drawn (after the state lines of the trace), a text trace of everything the device is told */
  void (*on_end)(void *user, gt3_geodev *dev, int32_t handle);
  void (*on_draw)(void *user, gt3_geodev *dev, int32_t handle);
  /* a viewport begins: the target (r->rt_handle, -1 the screen), its size (dview_w, dview_h) and what to clear (view->clear_flags: 1 colour, 2 depth) */
  void (*on_viewport)(void *user, struct gt3_render *r, const gt3_fxview *view);
  gt3_trace_fn trace;
  void *user;
} gt3_render;

/* the render targets take the texture handles 0 ... 9, in the order FXMaster creates them */
GT3_RENDER_API int gt3_render_init(gt3_render *r, int32_t screen_w, int32_t screen_h);
GT3_RENDER_API void gt3_render_exit(gt3_render *r);

/* the start and the end of a picture (or of a pre-roll step): the transforms are reset, and the dynamic buffers are discarded for the next one (the second half of
   sSystem_::Render() of the original) */
GT3_RENDER_API void gt3_render_frame_begin(gt3_render *r);
GT3_RENDER_API void gt3_render_frame_end(gt3_render *r);

/* FXMaster_::Alloc / Ref / ResetAlloc */
GT3_RENDER_API int32_t gt3_fxm_alloc(gt3_render *r, int32_t ocount, int32_t size);
GT3_RENDER_API void gt3_fxm_ref(gt3_render *r, int32_t handle, int32_t delta);
GT3_RENDER_API void gt3_fxm_reset(gt3_render *r);

/* sSystem_::BeginViewport, SetCamera, SetCameraOrtho, SetMatrix */
GT3_RENDER_API void gt3_begin_viewport(gt3_render *r, const gt3_fxview *v);
GT3_RENDER_API void gt3_set_camera(gt3_render *r, const gt3_cam *c);
GT3_RENDER_API void gt3_set_camera_ortho(gt3_render *r);
GT3_RENDER_API void gt3_set_matrix(gt3_render *r, const gt3_mat *m);
GT3_RENDER_API void gt3_end_viewport(gt3_render *r);
/* sSystem->GetTransform: 0 unit, GT3_GT_VIEW, GT3_GT_MODELVIEW, GT3_GT_MODEL */
GT3_RENDER_API void gt3_get_transform(const gt3_render *r, int mode, gt3_mat *m);
/* the trace line of RH_BeginViewport's debug hook */
GT3_RENDER_API void gt3_trace_camin(gt3_render *r, const float pos[3], const float rot[3], int32_t mode);

/* the Direct3D state: StateBase / StateTex / StateEnd write a stream of (token, value) pairs ending in ~0, SetStates plays one */
GT3_RENDER_API void gt3_state_base(uint32_t **datap, uint32_t flags, int32_t texmode, uint32_t color);
GT3_RENDER_API void gt3_state_tex(uint32_t **datap, int nr, uint32_t flags);
GT3_RENDER_API void gt3_state_end(uint32_t **datap);
GT3_RENDER_API void gt3_set_state(gt3_render *r, uint32_t token, uint32_t value);
GT3_RENDER_API void gt3_set_states(gt3_render *r, const uint32_t *stream);
/* handle -1: no texture. With a matrix the stage's texture matrix is set (and traced). */
GT3_RENDER_API void gt3_set_texture(gt3_render *r, int stage, int32_t handle, const gt3_mat *m);
GT3_RENDER_API void gt3_set_matlight(gt3_render *r, const float material[17]);
GT3_RENDER_API int32_t gt3_add_light(gt3_render *r, const gt3_d3dlight *l, uint32_t mask);
GT3_RENDER_API void gt3_enable_lights(gt3_render *r, uint32_t mask);
GT3_RENDER_API void gt3_clear_lights(gt3_render *r);
/* GenLight::Set: the light of a scene in the world matrix `mat` */
GT3_RENDER_API void gt3_light_set(gt3_render *r, const gt3_light_def *l, const gt3_mat *mat);

/* GenFXChain's camera: RH_BeginViewport (mode >> 2: 0 rotate, 1 look at) */
GT3_RENDER_API void gt3_cam_from_chain(gt3_cam *c, const float pos[3], const float rot[3], int32_t mode, float zoom_x, float zoom_y);
/* RotateFromChain of genfx.cpp */
GT3_RENDER_API void gt3_rotate_from_chain(float dest[2], float x, float y, const float zoom[2], int32_t rotate, const float center[2], int invert);

/* ---- meshes: GenMesh::Prepare and DoPass2 ---- */

/* what the renderer needs to know of a material slot's pass: returns 0 when the pass does not exist, else the program (GT3_MPP_*) and the pass's Size, Aspect, Enlarge */
typedef int (*gt3_pass_fn)(void *user, int32_t material, int pass, int32_t *programm, float *size, float *aspect, float *enlarge);

/* GenMesh::Prepare: DoPass1 of every pass of every material slot (a slot without a material has the default material: one pass, GT3_MPP_STATIC). Returns 1, 0 when out of memory
   or when a pass needs more than GT3_MPASS_MAXBATCH batches. */
GT3_RENDER_API int gt3_mesh_prepare(gt3_mesh *m, gt3_pass_fn pass, void *user);
/* DoPass2 of one pass: uploads what has to be uploaded and draws; `set_pass` is MatPass::Set(), called before each batch. */
typedef void (*gt3_set_pass_fn)(void *user, int32_t material, int pass);
GT3_RENDER_API void gt3_mesh_draw_pass(gt3_render *r, gt3_mesh *m, int32_t mtrl, int pass, int32_t programm, float size, float aspect, float enlarge,
                                       gt3_set_pass_fn set_pass, void *set_pass_user);

#ifdef __cplusplus
}
#endif

#endif /* GT3_RENDER_H_INCLUDED */

#ifdef GT3_RENDER_IMPLEMENTATION
#ifndef GT3_RENDER_IMPLEMENTATION_DONE
#define GT3_RENDER_IMPLEMENTATION_DONE

#include <string.h>

/* ---- the geometry device (the oracle's sSystem_, _startintro.cpp) ---- */

static int gt3_geobuf_alloc(gt3_geo_buffer *b, int32_t count, int shift, int32_t *first)
{
  count = count << shift; /* all vertices are 32 bytes */
  if (b->used + count > b->size) return 0;
  *first = b->used;
  b->used += count;
  b->users++;
  return 1;
}

static int gt3_geobuf_create(gt3_geodev *d, int32_t i, int vertex)
{
  gt3_geo_buffer *b = &d->buffer[i];
  const int32_t size = vertex ? GT3_MAX_DYNVBSIZE : GT3_MAX_DYNIBSIZE;
  free(b->mem);
  b->mem = (uint8_t *)calloc(1, (size_t)size);
  b->size = b->mem ? size : 0;
  b->used = b->users = 0;
  return b->mem != NULL;
}

GT3_RENDER_API int gt3_geodev_init(gt3_geodev *d)
{
  memset(d, 0, sizeof *d);
  int ok = gt3_geobuf_create(d, 0, 1) && gt3_geobuf_create(d, 1, 0);
  d->this_vertex = 3;
  ok = ok && gt3_geobuf_create(d, d->this_vertex, 1);
  d->this_index = GT3_MAX_GEOBUFFER - 1;
  ok = ok && gt3_geobuf_create(d, d->this_index, 0);
  d->discard = 1;
  return ok;
}

GT3_RENDER_API void gt3_geodev_exit(gt3_geodev *d)
{
  for (int i = 0; i < GT3_MAX_GEOBUFFER; i++) free(d->buffer[i].mem);
  memset(d, 0, sizeof *d);
}

GT3_RENDER_API int32_t gt3_geo_add(gt3_geodev *d, uint32_t fvf, int32_t prim)
{
  for (int32_t i = 3; i < GT3_MAX_GEOHANDLE; i++) {
    gt3_geo_handle *h = &d->handle[i];
    if (h->mode == 0) {
      memset(h, 0, sizeof *h);
      h->mode = prim;
      h->fvf = fvf;
      return i;
    }
  }
  return 0;
}

GT3_RENDER_API void gt3_geo_rem(gt3_geodev *d, int32_t handle)
{
  if (handle >= 1 && handle < GT3_MAX_GEOHANDLE) d->handle[handle].mode = 0;
}

GT3_RENDER_API void gt3_geo_flush(gt3_geodev *d, int32_t handle) { d->handle[handle].discard = 0; }

GT3_RENDER_API int gt3_geo_draw(gt3_geodev *d, int32_t handle)
{
  gt3_geo_handle *h = &d->handle[handle];
  if (h->vcount == 0) return 1;
  if (h->vbuf == 0 && h->discard != d->discard) return 1;
  d->n_draw++;
  if (d->on_draw) d->on_draw(d->user, d, handle);
  return 0;
}

GT3_RENDER_API void gt3_geo_begin(gt3_geodev *d, int32_t handle, int32_t vc, int32_t ic, float **fp, uint16_t **ip)
{
  gt3_geo_handle *h = &d->handle[handle];
  h->vcount = vc;
  h->icount = ic;
  if (h->mode & GT3_GEO_STATIC) {
    while (!gt3_geobuf_alloc(&d->buffer[d->this_vertex], vc, 5, &h->vstart)) {
      d->this_vertex++;
      if (d->this_vertex >= d->this_index || !gt3_geobuf_create(d, d->this_vertex, 1)) abort();
    }
    h->vbuf = d->this_vertex;
    *fp = (float *)(void *)(d->buffer[h->vbuf].mem + h->vstart);
    if (ic) {
      while (!gt3_geobuf_alloc(&d->buffer[d->this_index], ic, 1, &h->istart)) {
        d->this_index--;
        if (d->this_vertex >= d->this_index || !gt3_geobuf_create(d, d->this_index, 0)) abort();
      }
      h->ibuf = d->this_index;
      *ip = (uint16_t *)(void *)(d->buffer[h->ibuf].mem + h->istart);
    }
  } else {
    for (;;) {
      int ok = gt3_geobuf_alloc(&d->buffer[0], vc, 5, &h->vstart);
      h->vbuf = 0;
      if (ic) {
        h->ibuf = 1;
        ok &= gt3_geobuf_alloc(&d->buffer[1], ic, 1, &h->istart);
      }
      if (ok) break;
      d->buffer[0].used = 0;
      d->buffer[1].used = 0;
      d->discard++;
    }
    h->discard = d->discard;
    *fp = (float *)(void *)(d->buffer[0].mem + h->vstart);
    if (ic) *ip = (uint16_t *)(void *)(d->buffer[1].mem + h->istart);
  }
}

GT3_RENDER_API void gt3_geo_end(gt3_geodev *d, int32_t handle, int32_t vc, int32_t ic)
{
  gt3_geo_handle *h = &d->handle[handle];
  if (vc != -1) {
    h->vcount = vc;
    h->icount = ic;
  }
  d->n_geo++;
  if (d->on_end) d->on_end(d->user, d, handle);
}

GT3_RENDER_API const uint8_t *gt3_geo_vertices(const gt3_geodev *d, int32_t handle)
{
  const gt3_geo_handle *h = &d->handle[handle];
  return d->buffer[h->vbuf].mem + h->vstart;
}

GT3_RENDER_API const uint16_t *gt3_geo_indices(const gt3_geodev *d, int32_t handle)
{
  const gt3_geo_handle *h = &d->handle[handle];
  return h->ibuf ? (const uint16_t *)(const void *)(d->buffer[h->ibuf].mem + h->istart) : NULL;
}

static void gt3_v_scale3_from(gt3_vec *d, const gt3_vec *a, float s)
{
  d->x = a->x * s;
  d->y = a->y * s;
  d->z = a->z * s;
}

/* ---- the Direct3D state and the text trace (the oracle's sSystem_: the original's code for the state streams, its own bookkeeping for the rest) ---- */

GT3_RENDER_API void gt3_state_base(uint32_t **datap, uint32_t flags, int32_t texmode, uint32_t color)
{
  int i;
  uint32_t *data;

  static const int16_t states[] = {
    GT3_D3DRS_CULLMODE,
    GT3_D3DCULL_CCW,  // 0
    GT3_D3DRS_NORMALIZENORMALS,
    0,                // 1
    GT3_D3DRS_LIGHTING,
    0,                // 2

    GT3_D3DRS_ZWRITEENABLE,
    0,                  // 3
    GT3_D3DRS_ZFUNC,
    GT3_D3DCMP_ALWAYS,  // 4
    GT3_D3DRS_COLORWRITEENABLE,
    15,                 // 5

    GT3_D3DRS_ALPHABLENDENABLE,
    1,                 // 6
    GT3_D3DRS_SRCBLEND,
    GT3_D3DBLEND_ONE,  // 7
    GT3_D3DRS_DESTBLEND,
    GT3_D3DBLEND_ONE,  // 8

    GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP,
    GT3_D3DTOP_MODULATE,  // 9   0
    GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP,
    GT3_D3DTOP_MODULATE,  // 10  1
    GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP,
    GT3_D3DTOP_DISABLE,   // 11  2
    GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP,
    GT3_D3DTOP_DISABLE,   // 12  3
    GT3_D3DTSS_2 | GT3_D3DTSS_COLOROP,
    GT3_D3DTOP_DISABLE,   // 13  4
    GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG1,
    GT3_D3DTA_TEXTURE,    // 14  5
    GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2,
    GT3_D3DTA_DIFFUSE,    // 15  6
    GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG1,
    GT3_D3DTA_TEXTURE,    // 16  7
    GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG2,
    GT3_D3DTA_CURRENT,    // 17  8
    GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG1,
    GT3_D3DTA_TEXTURE,    // 18  9
    GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG2,
    GT3_D3DTA_DIFFUSE,    // 19  10

    GT3_D3DRS_ALPHATESTENABLE,
    0,
    GT3_D3DRS_ALPHAFUNC,
    GT3_D3DCMP_GREATER,
    GT3_D3DRS_ALPHAREF,
    4,
    GT3_D3DRS_STENCILENABLE,
    0,

    GT3_D3DRS_DIFFUSEMATERIALSOURCE,
    GT3_D3DMCS_COLOR1,
    GT3_D3DRS_AMBIENTMATERIALSOURCE,
    GT3_D3DMCS_MATERIAL,
    GT3_D3DRS_SPECULARMATERIALSOURCE,
    GT3_D3DMCS_MATERIAL,
    GT3_D3DRS_EMISSIVEMATERIALSOURCE,
    GT3_D3DMCS_MATERIAL,
    GT3_D3DRS_SPECULARENABLE,
    0,

    GT3_D3DRS_BLENDOP,
    GT3_D3DBLENDOP_ADD,
  };

  data = *datap;
  i = 0;
  do {
    data[i] = states[i];
    i++;
  } while (i < 30 * 2);

  if (flags & GT3_MBF_INVERTCULL) data[0 * 2 + 1] = GT3_D3DCULL_CW;
  if (flags & GT3_MBF_DOUBLESIDED) data[0 * 2 + 1] = GT3_D3DCULL_NONE;
  if (flags & GT3_MBF_RENORMALIZE) data[1 * 2 + 1] = 1;
  if (flags & GT3_MBF_LIGHT) data[2 * 2 + 1] = 1;
  if (flags & GT3_MBF_ZWRITE) data[3 * 2 + 1] = 1;
  if (flags & GT3_MBF_ZREAD) data[4 * 2 + 1] = GT3_D3DCMP_LESSEQUAL;
  if (flags & GT3_MBF_ZONLY) data[5 * 2 + 1] = 0;

  switch ((flags >> 12) & 7) {
  default:
  case GT3_MBF_BLENDOFF >> 12: data[6 * 2 + 1] = 0; break;
  case GT3_MBF_BLENDADD >> 12: break;
  case GT3_MBF_BLENDMUL >> 12:
    data[7 * 2 + 1] = GT3_D3DBLEND_ZERO;
    data[8 * 2 + 1] = GT3_D3DBLEND_SRCCOLOR;
    break;
  case GT3_MBF_BLENDALPHA >> 12:
    data[7 * 2 + 1] = GT3_D3DBLEND_SRCALPHA;
    data[8 * 2 + 1] = GT3_D3DBLEND_INVSRCALPHA;
    break;
  case GT3_MBF_BLENDSMOOTH >> 12: data[8 * 2 + 1] = GT3_D3DBLEND_INVSRCCOLOR; break;
  }

  data += 9 * 2 + 1;


  /*
  switch(texmode)
  {
  default:
  case GT3_MBM_FLAT:
    data[1*2] = GT3_D3DTOP_SELECTARG2;
    data[0*2] = GT3_D3DTOP_SELECTARG2;
    break;
  case GT3_MBM_TEX:
	case GT3_MBM_MULCOL:
    break;
  case GT3_MBM_ADD:
    data[2*2] = GT3_D3DTOP_ADD;
    break;
  case GT3_MBM_MUL:
    data[2*2] = GT3_D3DTOP_MODULATE;
    break;
	case GT3_MBM_BLEND:
    data[0*2] = GT3_D3DTOP_SELECTARG1;
    data[1*2] = GT3_D3DTOP_SELECTARG1;
    data[2*2] = GT3_D3DTOP_BLENDDIFFUSEALPHA;
		break;
  case GT3_MBM_SUB:
    data[2*2] = GT3_D3DTOP_SUBTRACT;
    break;
  case GT3_MBM_ADDSMOOTH:
    data[2*2] = GT3_D3DTOP_ADDSMOOTH;
    break;
	case GT3_MBM_ADDCOL:
    data[0*2] = GT3_D3DTOP_ADD;
		break;
	case GT3_MBM_BLENDCOL:
    data[5*2] = GT3_D3DTA_DIFFUSE;
    data[6*2] = GT3_D3DTA_TEXTURE;
    data[1*2] = GT3_D3DTOP_SELECTARG1;
    data[0*2] = GT3_D3DTOP_BLENDDIFFUSEALPHA;
		break;
	case GT3_MBM_SUBCOL:
    data[0*2] = GT3_D3DTOP_SUBTRACT;
		break;
  case GT3_MBM_ADDSMOOTHCOL:
    data[0*2] = GT3_D3DTOP_ADDSMOOTH;
    break;
  }
*/
  data += (30 - 9) * 2 - 1;


  switch (texmode) {
  case GT3_MBM_FLAT:
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_SELECTARG2;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_DISABLE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_SELECTARG2;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_DISABLE;
    break;
  case GT3_MBM_TEX:
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_DISABLE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_DISABLE;
    break;
  case GT3_MBM_ADD:
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_ADD;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_CURRENT;
    *data++ = GT3_D3DTSS_2 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_DISABLE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_SELECTARG2;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_CURRENT;
    *data++ = GT3_D3DTSS_2 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_DISABLE;
    break;
  case GT3_MBM_MUL:
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_CURRENT;
    *data++ = GT3_D3DTSS_2 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_DISABLE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_SELECTARG2;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_CURRENT;
    *data++ = GT3_D3DTSS_2 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_DISABLE;
    break;
  case GT3_MBM_BLEND:
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_SELECTARG1;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_BLENDDIFFUSEALPHA;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_CURRENT;
    *data++ = GT3_D3DTSS_2 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_DISABLE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_SELECTARG1;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_SELECTARG2;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_CURRENT;
    *data++ = GT3_D3DTSS_2 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_DISABLE;
    break;
  case GT3_MBM_SUB:
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_SUBTRACT;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_CURRENT;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_2 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_DISABLE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_SELECTARG2;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_CURRENT;
    *data++ = GT3_D3DTSS_2 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_DISABLE;
    break;
  case GT3_MBM_ADDSMOOTH:
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_ADDSMOOTH;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_CURRENT;
    *data++ = GT3_D3DTSS_2 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_DISABLE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_SELECTARG2;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_CURRENT;
    *data++ = GT3_D3DTSS_2 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_DISABLE;
    break;
  case GT3_MBM_ADDCOL:
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_ADD;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_DISABLE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_DISABLE;
    break;
  case GT3_MBM_MULCOL:
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_DISABLE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_DISABLE;
    break;
  case GT3_MBM_BLENDCOL:
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_BLENDDIFFUSEALPHA;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_DISABLE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_SELECTARG1;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_DISABLE;
    break;
  case GT3_MBM_SUBCOL:
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_SUBTRACT;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_DISABLE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_DISABLE;
    break;
  case GT3_MBM_ADDSMOOTHCOL:
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_ADDSMOOTH;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_COLOROP;
    *data++ = GT3_D3DTOP_DISABLE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_MODULATE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG1;
    *data++ = GT3_D3DTA_TEXTURE;
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_ALPHAARG2;
    *data++ = GT3_D3DTA_DIFFUSE;
    *data++ = GT3_D3DTSS_1 | GT3_D3DTSS_ALPHAOP;
    *data++ = GT3_D3DTOP_DISABLE;
    break;
  }


  if (flags & GT3_MBF_COLOR) {
    *data++ = GT3_D3DTSS_0 | GT3_D3DTSS_COLORARG2;
    *data++ = GT3_D3DTA_TFACTOR;
    *data++ = GT3_D3DRS_TEXTUREFACTOR;
    *data++ = color;
  }

  *datap = data;
}

GT3_RENDER_API void gt3_state_tex(uint32_t **datap, int nr, uint32_t flags)
{
  int tex, sam;
  uint32_t *data = *datap;

  tex = nr * GT3_D3DTSS_1;
  sam = nr * GT3_D3DSAMP_1;

  *data++ = sam + GT3_D3DSAMP_MAGFILTER;
  *data++ = flags & GT3_MTF_FILTERMAG ? GT3_D3DTEXF_LINEAR : GT3_D3DTEXF_POINT;

  *data++ = sam + GT3_D3DSAMP_MINFILTER;
  *data++ = flags & GT3_MTF_FILTERMIN ? GT3_D3DTEXF_LINEAR : GT3_D3DTEXF_POINT;

  *data++ = sam + GT3_D3DSAMP_MIPFILTER;
  *data++ = flags & GT3_MTF_MIPMAP ? GT3_D3DTEXF_LINEAR : GT3_D3DTEXF_NONE;

  *data++ = sam + GT3_D3DSAMP_ADDRESSU;
  *data++ = flags & GT3_MTF_CLAMP ? GT3_D3DTADDRESS_CLAMP : GT3_D3DTADDRESS_WRAP;

  *data++ = sam + GT3_D3DSAMP_ADDRESSV;
  *data++ = flags & GT3_MTF_CLAMP ? GT3_D3DTADDRESS_CLAMP : GT3_D3DTADDRESS_WRAP;

  *data++ = tex + GT3_D3DTSS_TEXTURETRANSFORMFLAGS;
  *data++ = ((flags & GT3_MTF_UVTRANS) || (flags & GT3_MTF_ENVIMASK)) ? 2 : 0;

  *data++ = tex + GT3_D3DTSS_TEXCOORDINDEX;
  switch (flags & GT3_MTF_UVMASK) {
  case GT3_MTF_UVENVI: *data++ = GT3_D3DTSS_TCI_CAMERASPACENORMAL; break;
  case GT3_MTF_UVSPEC: *data++ = GT3_D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR; break;
  case GT3_MTF_UVPOS: *data++ = GT3_D3DTSS_TCI_CAMERASPACEPOSITION; break;
  default: *data++ = (flags >> GT3_MTF_UVSHIFT) & 7; break;
  }
  *datap = data;
}


GT3_RENDER_API void gt3_state_end(uint32_t **datap)
{
  (*datap)[0] = ~0u;
  (*datap)[1] = ~0u;
  *datap += 2;
}

static uint32_t gt3_fbits(float f)
{
  uint32_t u;
  memcpy(&u, &f, sizeof u);
  return u;
}

static void gt3_trace(gt3_render *r, const char *fmt, ...)
{
  char line[1024];
  va_list ap;
  if (!r->trace) return;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  r->trace(r->user, line);
}

static void gt3_hexmat(char *out, const gt3_mat *m)
{
  const float *f = &m->i.x;
  for (int i = 0; i < 16; i++) out += sprintf(out, "%s%08x", i ? "," : "", (unsigned)gt3_fbits(f[i]));
}

static void gt3_trace_matrix(gt3_render *r, const char *name, const gt3_mat *m)
{
  char buf[256];
  if (!r->trace) return;
  gt3_hexmat(buf, m);
  gt3_trace(r, "M %s %s", name, buf);
}

/* the part of the state a draw call depends on, traced when it differs from what was traced before */
static void gt3_flush_state(gt3_render *r)
{
  if (!r->trace) return;
  for (int t = 0; t < GT3_NSTATE; t++)
    if (r->rs_known[t] && (!r->rs_emitted_known[t] || r->rs_emitted[t] != r->rs[t])) {
      gt3_trace(r, "S %03x %08x", t, (unsigned)r->rs[t]);
      r->rs_emitted[t] = r->rs[t];
      r->rs_emitted_known[t] = 1;
    }
  for (int s = 0; s < GT3_MAX_STAGES; s++)
    if (r->tex_bound[s] != r->tex_emitted[s]) {
      gt3_trace(r, "T %d %d", s, (int)r->tex_bound[s]);
      r->tex_emitted[s] = r->tex_bound[s];
    }
}

static void gt3_render_geo_end(void *user, gt3_geodev *dev, int32_t handle)
{
  gt3_render *r = (gt3_render *)user;
  if (r->on_end) r->on_end(r->user, dev, handle);
}

static void gt3_render_geo_draw(void *user, gt3_geodev *dev, int32_t handle)
{
  gt3_render *r = (gt3_render *)user;
  gt3_flush_state(r);
  if (r->on_draw) r->on_draw(r->user, dev, handle);
}

GT3_RENDER_API void gt3_set_state(gt3_render *r, uint32_t token, uint32_t value)
{
  if (token >= GT3_NSTATE) return;
  r->rs[token] = value;
  r->rs_known[token] = 1;
}

GT3_RENDER_API void gt3_set_states(gt3_render *r, const uint32_t *stream)
{
  for (; *stream != 0xffffffffu; stream += 2) gt3_set_state(r, stream[0], stream[1]);
}

GT3_RENDER_API void gt3_set_texture(gt3_render *r, int stage, int32_t handle, const gt3_mat *m)
{
  if (handle == -1) {
    r->tex_bound[stage] = -1;
    return;
  }
  r->tex_bound[stage] = handle;
  if (m) {
    char name[16];
    r->tex_mat[stage] = *m;
    snprintf(name, sizeof name, "tex%d", stage);
    gt3_trace_matrix(r, name, m);
  }
}

GT3_RENDER_API void gt3_set_matlight(gt3_render *r, const float material[17])
{
  char line[400], *p = line;
  memcpy(r->material, material, sizeof r->material);
  if (!r->trace) return;
  p += sprintf(p, "MATERIAL");
  for (int i = 0; i < 17; i++) p += sprintf(p, " %08x", (unsigned)gt3_fbits(material[i]));
  r->trace(r->user, line);
}

GT3_RENDER_API int32_t gt3_add_light(gt3_render *r, const gt3_d3dlight *l, uint32_t mask)
{
  if (r->light_count >= GT3_MAX_LIGHTS) return -1;
  r->light[r->light_count] = *l;
  r->light_mask[r->light_count] = mask;
  if (r->trace) {
    char line[512], *p = line;
    const uint32_t *w = (const uint32_t *)(const void *)l;
    p += sprintf(p, "LIGHT %d mask=%08x", (int)r->light_count, (unsigned)mask);
    for (unsigned k = 0; k < sizeof *l / 4; k++) p += sprintf(p, " %08x", (unsigned)w[k]);
    r->trace(r->user, line);
  }
  return r->light_count++;
}

GT3_RENDER_API void gt3_enable_lights(gt3_render *r, uint32_t mask)
{
  int32_t i = 0;
  do {
    r->light_on[i] = (r->light_mask[i] & mask) ? 1 : 0;
    i++;
  } while (i < r->light_count);
  if (r->trace) {
    char line[256];
    int n = snprintf(line, sizeof line, "LIGHTS mask=%08x n=%d on=", (unsigned)mask, (int)r->light_count);
    for (i = 0; i < r->light_count && n + 2 < (int)sizeof line; i++) line[n++] = r->light_on[i] ? '1' : '0';
    line[n] = 0;
    r->trace(r->user, line);
  }
}

GT3_RENDER_API void gt3_clear_lights(gt3_render *r)
{
  gt3_enable_lights(r, 0);
  r->light_count = 0;
}

GT3_RENDER_API void gt3_get_transform(const gt3_render *r, int mode, gt3_mat *m)
{
  switch (mode) {
  case GT3_GT_VIEW: *m = r->last_camera; break;
  case GT3_GT_MODELVIEW: *m = r->last_transform; break;
  case GT3_GT_MODEL: *m = r->last_matrix; break;
  default: gt3_mat_init(m); break;
  }
}

GT3_RENDER_API void gt3_trace_camin(gt3_render *r, const float pos[3], const float rot[3], int32_t mode)
{
  gt3_trace(r, "CAMIN mode=%d pos=%08x,%08x,%08x rot=%08x,%08x,%08x", (int)mode, (unsigned)gt3_fbits(pos[0]), (unsigned)gt3_fbits(pos[1]),
            (unsigned)gt3_fbits(pos[2]), (unsigned)gt3_fbits(rot[0]), (unsigned)gt3_fbits(rot[1]), (unsigned)gt3_fbits(rot[2]));
}

/* sMatrix::TransR, Mul3 */
static void gt3_mat_transr(gt3_mat *m)
{
  gt3_mat_trans3(m);
  m->l.x = -m->l.x;
  m->l.y = -m->l.y;
  m->l.z = -m->l.z;
  gt3_v_rotate3_self(&m->l, m);
}

static void gt3_mat_mul3(gt3_mat *d, const gt3_mat *a, const gt3_mat *b)
{
  const gt3_vec *rows_a[3] = { &a->i, &a->j, &a->k };
  gt3_vec *rows_d[3] = { &d->i, &d->j, &d->k };
  for (int r = 0; r < 3; r++) {
    gt3_v_scale3_from(rows_d[r], &b->i, rows_a[r]->x);
    gt3_v_addscale3(rows_d[r], &b->j, rows_a[r]->y);
    gt3_v_addscale3(rows_d[r], &b->k, rows_a[r]->z);
  }
}

GT3_RENDER_API void gt3_light_set(gt3_render *r, const gt3_light_def *l, const gt3_mat *mat)
{
  gt3_mat m2, m3;
  const gt3_mat *mp = mat;
  gt3_d3dlight light;
  memset(&m3, 0, sizeof m3);
  if (l->mode & 8) { /* camera space */
    m2 = r->last_transform;
    gt3_mat_transr(&m2);
    gt3_mat_mul3(&m3, mat, &m2);
    mp = &m3;
  }
  /* an int times a float: the integer is loaded exactly, the product rounded once */
#define GT3_IXF(i, f) ((float)((long double)(i) * (long double)(f)))
  const float amp = l->amplify / 65536.0f;
  memset(&light, 0, sizeof light);
  light.type = l->mode & 7;
  light.diffuse[0] = GT3_IXF(l->diffuse[2], amp);
  light.diffuse[1] = GT3_IXF(l->diffuse[1], amp);
  light.diffuse[2] = GT3_IXF(l->diffuse[0], amp);
  light.diffuse[3] = 1.0f;
  light.specular[0] = GT3_IXF(l->specular[2], amp);
  light.specular[1] = GT3_IXF(l->specular[1], amp);
  light.specular[2] = GT3_IXF(l->specular[0], amp);
  light.specular[3] = 1.0f;
  light.ambient[0] = GT3_IXF(l->ambient[2], amp);
  light.ambient[1] = GT3_IXF(l->ambient[1], amp);
  light.ambient[2] = GT3_IXF(l->ambient[0], amp);
  light.ambient[3] = 1.0f;
#undef GT3_IXF
  light.position[0] = mp->l.x;
  light.position[1] = mp->l.y;
  light.position[2] = mp->l.z;
  light.direction[0] = mp->k.x;
  light.direction[1] = mp->k.y;
  light.direction[2] = mp->k.z;
  light.range = l->range;
  light.falloff = l->falloff;
  light.attenuation[0] = 1;
  light.attenuation[1] = 0;
  light.attenuation[2] = 0;
  light.theta = l->inner;
  light.phi = l->outer;
  gt3_add_light(r, &light, l->mask);
}

/* ---- viewports and cameras ---- */

GT3_RENDER_API int gt3_render_init(gt3_render *r, int32_t screen_w, int32_t screen_h)
{
  /* the sINTRO build's sizes: the first render target is 1024 x 1024 */
  static const int32_t counts[4] = { 1, 3, 3, 3 };
  static const int32_t size[4][2] = { { 1024, 1024 }, { 512, 256 }, { 256, 256 }, { 128, 64 } };
  memset(r, 0, sizeof *r);
  r->screen_w = screen_w;
  r->screen_h = screen_h;
  r->n_view = GT3_FX_VIEWPORTS;
  r->rt_handle = -1;
  for (int i = 0; i < GT3_MAX_STAGES; i++) r->tex_bound[i] = r->tex_emitted[i] = -1;
  gt3_mat_init(&r->last_camera);
  gt3_mat_init(&r->last_matrix);
  gt3_mat_init(&r->last_transform);
  gt3_mat_init(&r->xf_view);
  gt3_mat_init(&r->xf_proj);
  gt3_mat_init(&r->xf_world);
  gt3_fxview *v = &r->view[0];
  v->screen = 0;
  v->render_target = -1;
  v->clear_flags = 3;
  v->clear_color = 0xff000000u;
  v->x1 = screen_w;
  v->y1 = screen_h;
  v->size = -1;
  v->xscale = v->yscale = 1.0f;
  int32_t handle = 0;
  v = &r->view[1];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < counts[i]; j++, v++) {
      v->screen = -1;
      v->render_target = handle++;
      v->clear_flags = 3;
      v->clear_color = 0xff000000u;
      v->x1 = size[i][0];
      v->y1 = size[i][1];
      v->size = i;
      v->xscale = v->yscale = 1.0f;
    }
  const int ok = gt3_geodev_init(&r->dev);
  r->dev.user = r;
  r->dev.on_end = gt3_render_geo_end;
  r->dev.on_draw = gt3_render_geo_draw;
  return ok;
}

GT3_RENDER_API void gt3_render_exit(gt3_render *r)
{
  gt3_geodev_exit(&r->dev);
  memset(r, 0, sizeof *r);
}

/* ---- the frame boundaries ---- */

GT3_RENDER_API void gt3_render_frame_begin(gt3_render *r)
{
  gt3_mat_init(&r->last_camera);
  gt3_mat_init(&r->last_matrix);
  gt3_mat_init(&r->last_transform);
}

GT3_RENDER_API void gt3_render_frame_end(gt3_render *r)
{
  gt3_geodev *d = &r->dev;
  for (int i = 0; i < GT3_MAX_GEOHANDLE; i++) d->handle[i].discard = 0;
  d->discard = 1;
  d->buffer[0].used = d->buffer[0].size;
  d->buffer[1].used = d->buffer[1].size;
}

GT3_RENDER_API int32_t gt3_fxm_alloc(gt3_render *r, int32_t ocount, int32_t size)
{
  int32_t i;
  if (ocount <= 0) { /* allocate the screen */
    size = -1;
    ocount = -ocount;
  }
  for (i = r->n_view - 1; i >= 0; i--)
    if (r->view[i].size == size && !r->view[i].ref_count) {
      r->view[i].ref_count = ocount;
      break;
    }
  return i;
}

GT3_RENDER_API void gt3_fxm_ref(gt3_render *r, int32_t handle, int32_t delta)
{
  if (handle >= 0 && handle < r->n_view) r->view[handle].ref_count += delta;
}

GT3_RENDER_API void gt3_fxm_reset(gt3_render *r)
{
  for (int i = 0; i < r->n_view; i++) r->view[i].ref_count = 0;
}

GT3_RENDER_API void gt3_begin_viewport(gt3_render *r, const gt3_fxview *v)
{
  if (v->screen != -1) {
    r->dview_w = r->screen_w;
    r->dview_h = r->screen_h;
    r->rt_handle = -1;
  } else {
    /* a render target has the size of its texture: the table of FXMaster */
    r->dview_w = v->x1 - v->x0;
    r->dview_h = v->y1 - v->y0;
    r->rt_handle = v->render_target;
    for (int32_t i = 1; i < r->n_view; i++)
      if (r->view[i].render_target == v->render_target) {
        r->dview_w = r->view[i].x1;
        r->dview_h = r->view[i].y1;
        break;
      }
  }
  r->n_viewports++;
  gt3_trace(r, "VIEWPORT rt=%d size=%dx%d clear=%d color=%08x", (int)r->rt_handle, (int)r->dview_w, (int)r->dview_h, (int)v->clear_flags,
            (unsigned)v->clear_color);
  if (r->on_viewport) r->on_viewport(r->user, r, v);
}

GT3_RENDER_API void gt3_end_viewport(gt3_render *r)
{
  gt3_trace(r, "ENDVIEWPORT");
  gt3_clear_lights(r);
}

GT3_RENDER_API void gt3_set_camera(gt3_render *r, const gt3_cam *c)
{
  const float ratio = c->aspect_x / c->aspect_y;
  const float q = c->far_clip / (c->far_clip - c->near_clip);
  gt3_mat mat;
  if (r->trace) {
    char buf[256];
    gt3_hexmat(buf, &c->pos);
    gt3_trace(r, "CAMERA zoom=%08x,%08x aspect=%08x,%08x center=%08x,%08x clip=%08x,%08x pos=%s", (unsigned)gt3_fbits(c->zoom_x),
              (unsigned)gt3_fbits(c->zoom_y), (unsigned)gt3_fbits(c->aspect_x), (unsigned)gt3_fbits(c->aspect_y), (unsigned)gt3_fbits(c->center_x),
              (unsigned)gt3_fbits(c->center_y), (unsigned)gt3_fbits(c->near_clip), (unsigned)gt3_fbits(c->far_clip), buf);
  }
  r->last_camera = c->pos;
  gt3_mat_transr(&r->last_camera);
  r->xf_view = r->last_camera;
  gt3_mat_init(&mat);
  mat.i.x = c->zoom_x;
  mat.j.y = c->zoom_y * ratio;
  mat.k.x = c->center_x;
  mat.k.y = c->center_y;
  mat.k.z = q;
  mat.k.w = 1;
  mat.l.z = -(q * c->near_clip);
  mat.l.w = 0;
  r->xf_proj = mat;
  gt3_trace_matrix(r, "view", &r->xf_view);
  gt3_trace_matrix(r, "proj", &r->xf_proj);
}

GT3_RENDER_API void gt3_set_camera_ortho(gt3_render *r)
{
  gt3_mat mat;
  gt3_mat_init(&r->last_camera);
  gt3_mat_init(&r->last_matrix);
  gt3_mat_init(&r->last_transform);
  r->xf_view = r->last_transform;
  r->xf_world = r->last_transform;
  gt3_mat_init(&mat);
  mat.i.x = 2.0f / (float)r->dview_w;
  mat.j.y = -2.0f / (float)r->dview_h;
  mat.l.x = -1;
  mat.l.y = 1;
  r->xf_proj = mat;
  gt3_trace_matrix(r, "view", &r->xf_view);
  gt3_trace_matrix(r, "world", &r->xf_world);
  gt3_trace_matrix(r, "proj", &r->xf_proj);
}

GT3_RENDER_API void gt3_set_matrix(gt3_render *r, const gt3_mat *m)
{
  r->last_matrix = *m;
  gt3_mat_mul4(&r->last_transform, &r->last_matrix, &r->last_camera);
  r->xf_world = *m;
  gt3_trace_matrix(r, "world", &r->xf_world);
}

GT3_RENDER_API void gt3_cam_from_chain(gt3_cam *c, const float pos_in[3], const float rot_in[3], int32_t mode, float zoom_x, float zoom_y)
{
  gt3_vec pos, vec;
  gt3_v_init4(&pos, pos_in[0], pos_in[1], pos_in[2], 0);
  gt3_v_init4(&vec, rot_in[0], rot_in[1], rot_in[2], 0);
  gt3_mat_init(&c->pos);
  switch (mode >> 2) {
  case 0: { /* rotate */
    float a[3];
    a[0] = vec.x / GT3_PI2F;
    a[1] = vec.y / GT3_PI2F;
    a[2] = vec.z / GT3_PI2F;
    gt3_mat_euler_pi2(&c->pos, a);
    break;
  }
  case 1: { /* look at: sMatrix::InitDir */
    gt3_v_sub3(&vec, &vec, &pos);
    gt3_mat *m = &c->pos;
    gt3_v_init3(&m->j, 0, 1, 0);
    m->k = vec;
    gt3_v_unitsafe3(&m->k);
    gt3_v_cross3(&m->i, &m->j, &m->k);
    gt3_v_unitsafe3(&m->i);
    gt3_v_cross3(&m->j, &m->k, &m->i);
    gt3_v_unitsafe3(&m->j);
    m->i.w = 0.0f;
    m->j.w = 0.0f;
    m->k.w = 0.0f;
    gt3_v_init4(&m->l, 0, 0, 0, 1);
    break;
  }
  default: break;
  }
  c->pos.l = pos;
  c->pos.l.w = 1.0f;
  c->zoom_x = zoom_x / 65536.0f;
  c->zoom_y = c->zoom_x;
  c->aspect_x = 1.333f;
  c->aspect_y = zoom_y / 65536.0f;
  c->center_x = 0.0f;
  c->center_y = 0.0f;
  c->near_clip = 0.125f;
  c->far_clip = 256.0f;
}

GT3_RENDER_API void gt3_rotate_from_chain(float dest[2], float x, float y, const float zoom_in[2], int32_t rotate, const float center[2], int invert)
{
  float zoom[2], m00, m01, m10, m11;
  for (int i = 0; i < 2; i++) {
    zoom[i] = zoom_in[i];
    if (zoom[i] == 0.0f) zoom[i] = 0.01f;
    if (!invert) zoom[i] = 1.0f / zoom[i];
  }
  /* Rotate * sPI2F: the integer is loaded exactly (fild), the product is rounded once */
  const float angle = (float)((long double)rotate * (long double)GT3_PI2F) / 65536.0f;
  /* sFSin and sFCos come back as the C library's double in an x87 register: it is multiplied as it is, and the product is rounded once */
  const long double s = gt3_sin_d((double)angle), c = gt3_cos_d((double)angle);
  m00 = (float)(c * (long double)zoom[0]);
  m01 = (float)(s * (long double)zoom[1]);
  m10 = (float)(-s * (long double)zoom[0]);
  m11 = (float)(c * (long double)zoom[1]);
  if (invert) {
    const float t = m01;
    m01 = m10;
    m10 = t;
  }
  dest[0] = x * m00 + y * m01 + (center[0] + 0.5f - m00 - m01) * 0.5f;
  dest[1] = x * m10 + y * m11 + (center[1] + 0.5f - m10 - m11) * 0.5f;
}

/* ---- GenMesh::Prepare: DoPass1 ---- */

static int gt3_mesh_pass1(gt3_mesh *m, gt3_mpass *pass, int32_t mtrl, int32_t prg)
{
  int quadbuffer = 1, lines;
  int32_t vcmax = 0x7f00 / 4, icmax = 0x7fff0000;
  if (prg == GT3_MPP_STATIC || prg == GT3_MPP_DYNAMIC) {
    quadbuffer = 0;
    vcmax = 0x7f00;
    icmax = 0x17000;
  }
  lines = prg == GT3_MPP_OUTLINES || prg == GT3_MPP_THICKLINES || prg == GT3_MPP_FINNS;

  int32_t *statvb = (int32_t *)malloc(sizeof(int32_t) * 0x8000 * 4);
  uint16_t *statib = (uint16_t *)malloc(sizeof(uint16_t) * 4 * 0x8000);
  if (!statvb || !statib) {
    free(statvb);
    free(statib);
    return 0;
  }
  int ok = 1;
  pass->n_batch = 0;
  int32_t fi = 0;
  for (int32_t i = 0; i < m->n_edge; i++) m->edge[i].select = 0;

  while (fi < m->n_face) {
    int32_t ic = 0, vc = 0;
    int32_t *vp = statvb;
    uint16_t *ip = statib;
    for (int32_t i = 0; i < m->n_vert; i++) m->vert[i].select = 0;

    while (fi < m->n_face && vc < vcmax && ic < icmax) {
      if (m->face[fi].material == mtrl) {
        const int32_t e0 = m->face[fi].edge;
        int32_t e = e0;
        do {
          if (lines) {
            gt3_medge *ed = &m->edge[e >> 1];
            if (ed->select == 0) {
              ed->select = 1;
              *vp++ = gt3_vert_id(m, e);
              *vp++ = gt3_vert_id(m, e ^ 1);
              *vp++ = gt3_face_id(m, e);
              *vp++ = gt3_face_id(m, e ^ 1);
              vc += 4;
            }
          } else {
            gt3_mvert *v = &m->vert[gt3_vert_id(m, e)];
            if (!v->select) {
              v->select = 1;
              v->temp2 = vc;
              *vp++ = (int32_t)(v - m->vert);
              vc++;
            }
          }
          e = gt3_next_face_edge(m, e);
        } while (e != e0);

        if (!quadbuffer) {
          int32_t i0, i1, i2;
          e = m->face[fi].edge;
          i2 = m->vert[gt3_vert_id(m, e)].temp2;
          e = gt3_next_face_edge(m, e);
          const int32_t es = e;
          i1 = m->vert[gt3_vert_id(m, e)].temp2;
          e = gt3_next_face_edge(m, e);
          i0 = m->vert[gt3_vert_id(m, e)].temp2;
          e = gt3_next_face_edge(m, e);
          do {
            *ip++ = (uint16_t)i2;
            *ip++ = (uint16_t)i1;
            *ip++ = (uint16_t)i0;
            ic += 3;
            i1 = i0;
            i0 = m->vert[gt3_vert_id(m, e)].temp2;
            e = gt3_next_face_edge(m, e);
          } while (e != es);
        }
      }
      fi++;
    }
    if (vc > 0) {
      const int b = pass->n_batch;
      if (b >= GT3_MPASS_MAXBATCH) {
        ok = 0;
        break;
      }
      if (quadbuffer) {
        pass->icount[b] = 0;
        pass->ib[b] = NULL;
      } else {
        pass->icount[b] = ic;
        pass->ib[b] = (uint16_t *)malloc((size_t)ic * 2u);
        if (!pass->ib[b]) {
          ok = 0;
          break;
        }
        memcpy(pass->ib[b], statib, (size_t)ic * 2u);
      }
      pass->vcount[b] = vc;
      pass->vb[b] = (int32_t *)malloc((size_t)vc * 4u);
      if (!pass->vb[b]) {
        ok = 0;
        break;
      }
      memcpy(pass->vb[b], statvb, (size_t)vc * 4u);
      pass->geometry[b] = 0;
      pass->n_batch++;
    }
  }
  free(statvb);
  free(statib);
  return ok;
}

GT3_RENDER_API int gt3_mesh_prepare(gt3_mesh *m, gt3_pass_fn fn, void *user)
{
  if (m->prepared) return 1;
  if (m->n_pass_alloc < m->n_mtrl) {
    gt3_mpass *p = (gt3_mpass *)calloc((size_t)m->n_mtrl * GT3_MPASS_MAXPASS, sizeof *p);
    if (!p) return 0;
    if (m->pass) {
      memcpy(p, m->pass, (size_t)m->n_pass_alloc * GT3_MPASS_MAXPASS * sizeof *p);
      free(m->pass);
    }
    m->pass = p;
    m->n_pass_alloc = m->n_mtrl;
  }
  for (int32_t mati = 1; mati < m->n_mtrl; mati++)
    for (int pasi = 0; pasi < GT3_MPASS_MAXPASS; pasi++) {
      gt3_mpass *p = &m->pass[(size_t)mati * GT3_MPASS_MAXPASS + (size_t)pasi];
      int32_t programm = 0;
      float size = 0, aspect = 0, enlarge = 0;
      if (!fn(user, m->mtrl[mati], pasi, &programm, &size, &aspect, &enlarge)) continue;
      p->used = 1;
      p->programm = programm;
      if (!gt3_mesh_pass1(m, p, mati, programm)) return 0;
    }
  m->prepared = 1;
  return 1;
}

/* ---- DoPass2 ---- */

/* sVector::Rotate34 */
static void gt3_v_rotate34(gt3_vec *d, const gt3_mat *m, const gt3_vec *v)
{
  gt3_vec t;
  t.x = m->i.x * v->x;
  t.y = m->i.y * v->x;
  t.z = m->i.z * v->x;
  t.w = m->i.w * v->x;
  t.x += m->j.x * v->y;
  t.y += m->j.y * v->y;
  t.z += m->j.z * v->y;
  t.w += m->j.w * v->y;
  t.x += m->k.x * v->z;
  t.y += m->k.y * v->z;
  t.z += m->k.z * v->z;
  t.w += m->k.w * v->z;
  t.x += m->l.x;
  t.y += m->l.y;
  t.z += m->l.z;
  t.w += m->l.w;
  *d = t;
}

/* A thick line whose ends project to one point has no direction: Unit3 divides by zero and the corners are NaN. The x87 unit makes them the positive quiet NaN (the real
   indefinite is negative, but the products with the negated copy are positive, and of two NaNs of the same payload the positive one is kept): one bit pattern for all. */
static float gt3_nan_positive(float f)
{
  if (f == f) return f;
  const uint32_t bits = 0x7fc00000u;
  memcpy(&f, &bits, sizeof f);
  return f;
}

static void gt3_put_vertex(float *v3, float x, float y, float z, const gt3_vec *n, float u, float v)
{
  v3[0] = gt3_nan_positive(x);
  v3[1] = gt3_nan_positive(y);
  v3[2] = gt3_nan_positive(z);
  v3[3] = n->x;
  v3[4] = n->y;
  v3[5] = n->z;
  v3[6] = u;
  v3[7] = v;
}

GT3_RENDER_API void gt3_mesh_draw_pass(gt3_render *r, gt3_mesh *m, int32_t mtrl, int pass_index, int32_t prg, float size, float aspect, float enlarge,
                                       gt3_set_pass_fn set_pass, void *set_pass_user)
{
  gt3_geodev *d = &r->dev;
  gt3_mpass *pass = &m->pass[(size_t)mtrl * GT3_MPASS_MAXPASS + (size_t)pass_index];
  const int32_t vvs = m->vert_size;
  const int32_t vnr = m->vert_map[GT3_GMI_NORMAL] == -1 ? 0 : m->vert_map[GT3_GMI_NORMAL];
  const int32_t vuv = m->vert_map[GT3_GMI_UV0] == -1 ? 0 : m->vert_map[GT3_GMI_UV0];

  for (int batch = 0; batch < pass->n_batch; batch++) {
    int32_t handle = pass->geometry[batch];
    if (handle == 0) {
      switch (prg) {
      case GT3_MPP_STATIC: handle = gt3_geo_add(d, GT3_FVF_STANDARD, GT3_GEO_TRI | GT3_GEO_STATIC); break;
      case GT3_MPP_DYNAMIC: handle = gt3_geo_add(d, GT3_FVF_STANDARD, GT3_GEO_TRI); break;
      case GT3_MPP_SPRITES:
      case GT3_MPP_THICKLINES: handle = gt3_geo_add(d, GT3_FVF_STANDARD, GT3_GEO_QUAD); break;
      default: return; /* the intro does not use the others (trees, spikes, outlines, finns, shadow) */
      }
      pass->geometry[batch] = handle;
    }
    if (set_pass) set_pass(set_pass_user, m->mtrl[mtrl], pass_index);
    if (!gt3_geo_draw(d, handle)) continue;

    const uint16_t *ip = pass->ib[batch];
    const int32_t *vp = pass->vb[batch];
    const int32_t ic = pass->icount[batch];
    int32_t vc = pass->vcount[batch];
    float *v3;
    uint16_t *ip2 = NULL;
    switch (prg) {
    case GT3_MPP_STATIC:
    case GT3_MPP_DYNAMIC:
      gt3_geo_begin(d, handle, vc, ic, &v3, &ip2);
      for (int32_t i = 0; i < vc; i++) {
        const int32_t j = *vp++;
        const float *vb = &m->vbuf[(size_t)j * (size_t)vvs].x;
        v3[0] = vb[0];
        v3[1] = vb[1];
        v3[2] = vb[2];
        v3[3] = vb[vnr * 4 + 0];
        v3[4] = vb[vnr * 4 + 1];
        v3[5] = vb[vnr * 4 + 2];
        v3[6] = vb[vuv * 4 + 0];
        v3[7] = vb[vuv * 4 + 1];
        v3 += 8;
      }
      memcpy(ip2, ip, (size_t)ic * 2u);
      gt3_geo_end(d, handle, -1, 0);
      gt3_geo_draw(d, handle);
      if (prg == GT3_MPP_DYNAMIC) gt3_geo_flush(d, handle);
      break;

    case GT3_MPP_THICKLINES:
    case GT3_MPP_SPRITES: {
      const int lines = prg == GT3_MPP_THICKLINES;
      gt3_mat mat = r->last_transform, matt;
      gt3_vec vx, vy, p, p0, p1, pp0, pp1, n0, n1;
      matt = mat;
      gt3_mat_trans3(&matt);
      const float s0 = size * aspect;
      const float s1 = size / aspect;
      gt3_v_init4(&vx, 0, 0, 0, 0);
      gt3_v_init4(&vy, 0, 0, 0, 0);
      gt3_v_scale3_from(&vx, &matt.j, -s0);
      gt3_v_scale3_from(&vy, &matt.i, s1);
      if (lines) {
        gt3_v_init4(&vx, 0, 0, 0, 0);
        gt3_v_init4(&vy, 0, 0, 0, 0);
        vc = vc >> 2;
      }
      gt3_geo_begin(d, handle, vc * 4, 0, &v3, NULL);
      int32_t count = 0;
      for (int32_t i = 0; i < vc; i++) {
        if (lines) {
          const int32_t j0 = *vp++, j1 = *vp++;
          vp += 2; /* the two face ids: only the outline programs look at them */
          p0 = m->vbuf[(size_t)j0 * (size_t)vvs];
          p1 = m->vbuf[(size_t)j1 * (size_t)vvs];
          n0 = m->vbuf[(size_t)j0 * (size_t)vvs + (size_t)vnr];
          n1 = m->vbuf[(size_t)j1 * (size_t)vvs + (size_t)vnr];
          gt3_v_addscale3(&p0, &n0, enlarge);
          gt3_v_addscale3(&p1, &n1, enlarge);
          gt3_v_rotate34(&pp0, &mat, &p0);
          gt3_v_rotate34(&pp1, &mat, &p1);
          p.x = pp0.x / pp0.z - pp1.x / pp1.z;
          p.y = pp0.y / pp0.z - pp1.y / pp1.z;
          p.z = 0;
          { /* Unit3 */
            const float e = gt3_v_dot3(&p, &p);
            gt3_v_scale3(&p, 1.0f / sqrtf(e));
          }
          gt3_v_scale3_from(&vy, &matt.i, (-p.x) * s1);
          gt3_v_addscale3(&vy, &matt.j, (-p.y) * s1);
          gt3_v_scale3_from(&vx, &matt.j, p.x * s0);
          gt3_v_addscale3(&vx, &matt.i, (-p.y) * s0);
        } else {
          const int32_t j = *vp++;
          p0 = p1 = m->vbuf[(size_t)j * (size_t)vvs];
          n0 = n1 = m->vbuf[(size_t)j * (size_t)vvs + (size_t)vnr];
        }
        gt3_put_vertex(v3, p0.x - vx.x - vy.x, p0.y - vx.y - vy.y, p0.z - vx.z - vy.z, &n0, 0.0f, 0.0f);
        v3 += 8;
        gt3_put_vertex(v3, p1.x - vx.x + vy.x, p1.y - vx.y + vy.y, p1.z - vx.z + vy.z, &n1, 1.0f, 0.0f);
        v3 += 8;
        gt3_put_vertex(v3, p1.x + vx.x + vy.x, p1.y + vx.y + vy.y, p1.z + vx.z + vy.z, &n1, 1.0f, 1.0f);
        v3 += 8;
        gt3_put_vertex(v3, p0.x + vx.x - vy.x, p0.y + vx.y - vy.y, p0.z + vx.z - vy.z, &n0, 0.0f, 1.0f);
        v3 += 8;
        count += 4;
      }
      gt3_geo_end(d, handle, count, 0);
      gt3_geo_draw(d, handle);
      gt3_geo_flush(d, handle);
      break;
    }
    default: break;
    }
  }
}

#endif /* GT3_RENDER_IMPLEMENTATION_DONE */
#endif /* GT3_RENDER_IMPLEMENTATION */
