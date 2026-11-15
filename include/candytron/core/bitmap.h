/* bitmap.h - the bitmap natives of Candytron's GenThree (genbitmap.cpp) - header-only C99 (stb style)

   A bitmap is 256 x 256 pixels of four 16 bit lanes in a 64 bit word, 15 bit values (0..0x7fff): lane 0 (bits 0..15) is blue, lane 1 green, lane 2 red,
   lane 3 alpha, as GetColor64 packs them from a script colour (x = red, y = green, z = blue, w = alpha, 8.8 fixed point, halved). The original works on them with MMX
   (Fade64, Bitmap_Inner, the text blend) and 32 bit float; the lanes are written out here with the exact semantics of the instructions used (pmulhw, paddsw, psubusw,
   psllw, packssdw, ...), the float expressions in the order of the original's source, so that the pixels are the original's pixels.

   What the original's x87 unit adds, and the port does not have: the control word 0x143f (24 bit significand, round down). Float arithmetic here is `float` arithmetic,
   and the original's results are reproduced exactly by running it with the rounding mode set downward (gt3_round_down() of csl_natives.h): then every float operation
   rounds like the x87 at 24 bits, and the conversions the original makes with `sFtol` are `floor`s. Where the host cannot (WebAssembly) the pixels differ in the last bit
   of a few lanes (the tests allow for it).

   NOT here: Bitmap_Text's glyphs (the original asks GDI for Arial: the caller gives the coverage, see gt3_bitmap_text), the other 14 bitmap operators (Dots, Blur,
   Mask, HSCB, Rotate, Distort, Normals, Light, Bump, Cell, Wavelet, Gradient, SetSize, SetTexture): the script does not call them.

   USAGE
     #define GT3_BITMAP_IMPLEMENTATION in one translation unit.
*/
#ifndef GT3_BITMAP_H_INCLUDED
#define GT3_BITMAP_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#include <candytron/core/rand.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GT3_BITMAP_API
#ifdef GT3_BITMAP_STATIC
#define GT3_BITMAP_API static
#else
#define GT3_BITMAP_API extern
#endif
#endif

#define GT3_BITMAP_SIZE 256 /* BitmapXSize and BitmapYSize of the intro (sINTRO_X) */

typedef struct gt3_bitmap {
  int32_t width, height;
  int32_t texture; /* -1: not made a texture yet */
  uint64_t pixel[];
} gt3_bitmap;

/* A zeroed bitmap, or NULL. The one block is released with free(). */
GT3_BITMAP_API gt3_bitmap *gt3_bitmap_create(int32_t width, int32_t height);
/* GenBitmap::Copy: the pixels of `src`, no texture. */
GT3_BITMAP_API gt3_bitmap *gt3_bitmap_copy(const gt3_bitmap *src);

/* GetColor64: the script's colour (x, y, z, w: red, green, blue, alpha; a value of 0x100 is 1/256) as the 15 bit lanes. */
GT3_BITMAP_API uint64_t gt3_color64(const int32_t color[4]);
/* Fade64: c0 + (c1 - c0) * fade / 65536 in the MMX arithmetic of the original (`fade` outside 0..65536 over- and undershoots, as there). */
GT3_BITMAP_API uint64_t gt3_fade64(uint64_t c0, uint64_t c1, int32_t fade);

/* Bitmap_Flat */
GT3_BITMAP_API void gt3_bitmap_flat(gt3_bitmap *bm, const int32_t color[4]);
/* Bitmap_Merge: `mode` is 0 add, 1 sub, 2 mul, 3 diff, 4 alpha from the luminance of `src` (the script's value >> 16; 5 modes). */
GT3_BITMAP_API void gt3_bitmap_merge(gt3_bitmap *bm, const gt3_bitmap *src, int mode);
/* Bitmap_Color: `mode` is 0 mul, 1 add, 2 sub, 3 gray, 4 invert, 5 scale by the colour (the script's value >> 16; 6 modes). */
GT3_BITMAP_API void gt3_bitmap_color(gt3_bitmap *bm, int mode, const int32_t color[4]);
/* a replacement for the power function (NULL: gt3_fpow); the tests run the software version against the hardware one */
typedef double (*gt3_pow_fn)(double a, double b);

/* Bitmap_GlowRect: all values as the script gives them (centre, radius and the flat part in units of the bitmap size; alpha and power are floats). */
GT3_BITMAP_API void gt3_bitmap_glowrect(gt3_bitmap *bm, float cx, float cy, float rx, float ry, float sx, float sy, const int32_t color[4], float alpha,
                                        float power, gt3_pow_fn fpow);

/* The tables of sInitPerlin (_types.cpp): made from the generator, with seed 1, before the script runs. */
typedef struct gt3_perlin {
  float gradient3d[16][4]; /* sPerlinGradient3D (the meshes' Perlin3D) */
  float gradient[256][2];  /* sPerlinRandom */
  uint8_t permute[512];    /* sPerlinPermute */
} gt3_perlin;
GT3_BITMAP_API void gt3_perlin_init(gt3_perlin *p, gt3_rng *rng);

/* Bitmap_Perlin (a new bitmap): freq, oct, seed and mode are the script's 16.16 values (the function shifts them), fadeoff, amp and gamma are floats. */
GT3_BITMAP_API gt3_bitmap *gt3_bitmap_perlin(const gt3_perlin *p, int32_t freq, int32_t oct, float fadeoff, int32_t seed, int32_t mode, float amp, float gamma,
                                             const int32_t col0[4], const int32_t col1[4], gt3_pow_fn fpow);

/* Bitmap_Text's blend: every pixel is faded towards the colour by its coverage (0..255, `width * height` bytes); NULL is no coverage anywhere, which is not a no-op:
   the fade by 0 still rounds the pixel (as in the original, whose font was blank there). */
GT3_BITMAP_API void gt3_bitmap_text(gt3_bitmap *bm, const int32_t color[4], const uint8_t *coverage);

/* Bitmap_MakeTexture's conversion: 4 bytes per pixel, blue, green, red, alpha (lane >> 7). `out` has width * height * 4 bytes. */
GT3_BITMAP_API void gt3_bitmap_to_bgra(const gt3_bitmap *bm, uint8_t *out);
/* The box filter of the original's UpdateTexture: one mip level (width / 2 x height / 2) of a 32 bit image. */
GT3_BITMAP_API void gt3_texture_next_level(uint8_t *dst, const uint8_t *src, int32_t width, int32_t height);

/* sFPow of the intro (an x87 sequence: 0 for a base of 0, not a number below 0), and the arithmetic shift as a function (the original's `>> 16` of a signed int). */
GT3_BITMAP_API double gt3_fpow(double a, double b);
GT3_BITMAP_API double gt3_fpow_soft(double a, double b);
static inline int32_t gt3_sar(int32_t v, int n) { return v >= 0 ? (int32_t)((uint32_t)v >> n) : ~(int32_t)((uint32_t)~v >> n); }

#ifdef __cplusplus
}
#endif

#endif /* GT3_BITMAP_H_INCLUDED */

#ifdef GT3_BITMAP_IMPLEMENTATION
#ifndef GT3_BITMAP_IMPLEMENTATION_DONE
#define GT3_BITMAP_IMPLEMENTATION_DONE

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- the lanes ---- */

#define GT3_LANE(v, k) ((uint16_t)((v) >> (16 * (k))))
#define GT3_PACK(l0, l1, l2, l3) \
  ((uint64_t)(uint16_t)(l0) | ((uint64_t)(uint16_t)(l1) << 16) | ((uint64_t)(uint16_t)(l2) << 32) | ((uint64_t)(uint16_t)(l3) << 48))

static int16_t gt3_sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)); }

/* pmulhw: the high word of the signed 32 bit product (a floor division by 65536) */
static int16_t gt3_mulhw(int16_t a, int16_t b)
{
  const int32_t p = (int32_t)a * (int32_t)b;
  int32_t q = p / 65536;
  if (p % 65536 < 0) q--;
  return (int16_t)q;
}

/* ---- construction ---- */

GT3_BITMAP_API gt3_bitmap *gt3_bitmap_create(int32_t width, int32_t height)
{
  if (width < 1 || height < 1) return NULL;
  gt3_bitmap *bm = (gt3_bitmap *)calloc(1, sizeof *bm + (size_t)width * (size_t)height * sizeof(uint64_t));
  if (!bm) return NULL;
  bm->width = width;
  bm->height = height;
  bm->texture = -1;
  return bm;
}

GT3_BITMAP_API gt3_bitmap *gt3_bitmap_copy(const gt3_bitmap *src)
{
  gt3_bitmap *bm = gt3_bitmap_create(src->width, src->height);
  if (bm) memcpy(bm->pixel, src->pixel, (size_t)src->width * (size_t)src->height * sizeof(uint64_t));
  return bm;
}

/* ---- colour and fade ---- */

static int32_t gt3_range7fff(int32_t a) { return a < 0 ? 0 : (a > 0x7fff ? 0x7fff : a); }

GT3_BITMAP_API uint64_t gt3_color64(const int32_t i[4])
{
  return ((uint64_t)gt3_range7fff(i[2] / 2) << 0) | ((uint64_t)gt3_range7fff(i[1] / 2) << 16) | ((uint64_t)gt3_range7fff(i[0] / 2) << 32) |
         ((uint64_t)gt3_range7fff(i[3] / 2) << 48);
}

GT3_BITMAP_API uint64_t gt3_fade64(uint64_t c0, uint64_t c1, int32_t fade)
{
  const int32_t f = gt3_sar(fade, 1);
  const int16_t w1 = gt3_sat16(f);          /* the weight of c1 */
  const int16_t w0 = gt3_sat16(0x8000 - f); /* the weight of c0 */
  uint16_t lane[4];
  for (int k = 0; k < 4; k++) {
    const int16_t a = (int16_t)GT3_LANE(c0, k), b = (int16_t)GT3_LANE(c1, k);
    lane[k] = (uint16_t)((uint16_t)(gt3_mulhw(a, w0) + gt3_mulhw(b, w1)) << 1);
  }
  return GT3_PACK(lane[0], lane[1], lane[2], lane[3]);
}

/* ---- Bitmap_Inner (the MMX loops) ---- */

static uint16_t gt3_adds(uint16_t a, uint16_t b) /* paddsw */ { return (uint16_t)gt3_sat16((int32_t)(int16_t)a + (int32_t)(int16_t)b); }
static uint16_t gt3_subus(uint16_t a, uint16_t b) { return a > b ? (uint16_t)(a - b) : 0; }                               /* psubusw */
static uint16_t gt3_mulc(uint16_t a, uint16_t b) { return (uint16_t)((uint16_t)gt3_mulhw((int16_t)a, (int16_t)b) << 1); } /* pmulhw, psllw 1 */

/* the sum (d0 + d1) >> 1 + (d1 + d2) >> 1 of the luminance loops, in 16 bit lane arithmetic */
static uint16_t gt3_luma(uint64_t v)
{
  const uint16_t d0 = GT3_LANE(v, 0), d1 = GT3_LANE(v, 1), d2 = GT3_LANE(v, 2);
  const uint16_t a = (uint16_t)(d0 + d1) >> 1, b = (uint16_t)(d2 + d1) >> 1;
  return (uint16_t)(a + b);
}

static uint64_t gt3_pixel_op(int mode, uint64_t d, uint64_t s, uint64_t c)
{
  uint16_t r[4];
  switch (mode) {
  case 0: /* add */
    for (int k = 0; k < 4; k++) r[k] = gt3_adds(GT3_LANE(d, k), GT3_LANE(s, k));
    break;
  case 1: /* sub */
    for (int k = 0; k < 4; k++) r[k] = gt3_subus(GT3_LANE(d, k), GT3_LANE(s, k));
    break;
  case 2: /* mul */
    for (int k = 0; k < 4; k++) r[k] = gt3_mulc(GT3_LANE(d, k), GT3_LANE(s, k));
    break;
  case 3: /* diff */
    for (int k = 0; k < 4; k++) r[k] = (uint16_t)((uint16_t)(GT3_LANE(d, k) - GT3_LANE(s, k) + 0x7fff) >> 1);
    break;
  case 4: { /* alpha: the luminance of the source into the alpha lane, the colour of the destination kept */
    /* the alpha lane takes bits 1..16 of (luma sum of the source) shifted up by 47: bit 0 of lane 1 of the sum lands in its top bit */
    const uint64_t sum = (uint64_t)gt3_luma(s) |
                         ((uint64_t)(uint16_t)(((uint16_t)(GT3_LANE(s, 1) + GT3_LANE(s, 2)) >> 1) + ((uint16_t)(GT3_LANE(s, 3) + GT3_LANE(s, 2)) >> 1)) << 16);
    return (d & 0x0000ffffffffffffull) | ((sum << 47) & 0xffff000000000000ull);
  }
  case 5: /* mul colour */
    for (int k = 0; k < 4; k++) r[k] = gt3_mulc(GT3_LANE(d, k), GT3_LANE(c, k));
    break;
  case 6: /* add colour */
    for (int k = 0; k < 4; k++) r[k] = gt3_adds(GT3_LANE(d, k), GT3_LANE(c, k));
    break;
  case 7: /* sub colour */
    for (int k = 0; k < 4; k++) r[k] = gt3_subus(GT3_LANE(d, k), GT3_LANE(c, k));
    break;
  case 8: { /* gray: the luminance in the three colour lanes, alpha at maximum */
    const uint16_t g = (uint16_t)(gt3_luma(d) >> 1);
    return GT3_PACK(g, g, g, 0x7fff | g);
  }
  case 9: /* invert */ return d ^ 0x7fff7fff7fff7fffull;
  default: /* 10: scale by the colour: the 32 bit product shifted right by 11 (logical), packed with signed saturation */
    for (int k = 0; k < 4; k++) {
      const uint32_t p = (uint32_t)((int32_t)(int16_t)GT3_LANE(d, k) * (int32_t)(int16_t)GT3_LANE(c, k));
      const uint32_t q = p >> 11;
      r[k] = (uint16_t)gt3_sat16(q > 32767u ? 32767 : (int32_t)q);
    }
    break;
  }
  return GT3_PACK(r[0], r[1], r[2], r[3]);
}

/* ---- the operators ---- */

GT3_BITMAP_API void gt3_bitmap_flat(gt3_bitmap *bm, const int32_t color[4])
{
  const uint64_t c = gt3_color64(color);
  const size_t n = (size_t)bm->width * (size_t)bm->height;
  for (size_t i = 0; i < n; i++) bm->pixel[i] = c;
}

GT3_BITMAP_API void gt3_bitmap_merge(gt3_bitmap *bm, const gt3_bitmap *src, int mode)
{
  const size_t n = (size_t)bm->width * (size_t)bm->height;
  for (size_t i = 0; i < n; i++) bm->pixel[i] = gt3_pixel_op(mode, bm->pixel[i], src->pixel[i], 0);
}

GT3_BITMAP_API void gt3_bitmap_color(gt3_bitmap *bm, int mode, const int32_t color[4])
{
  const uint64_t c = gt3_color64(color);
  const size_t n = (size_t)bm->width * (size_t)bm->height;
  for (size_t i = 0; i < n; i++) bm->pixel[i] = gt3_pixel_op(mode + 5, bm->pixel[i], 0, c);
}

/* Where the host has the x87, the original's instructions are used themselves (bit exact to the reference of the tests, on the machines tried); anywhere else (Wasm, ARM)
   and with GT3_FPOW_PORTABLE the software version below stands in. */
#if !defined(GT3_FPOW_PORTABLE) && !defined(__EMSCRIPTEN__) && (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#define GT3_FPOW_X87 1
#endif

/* The significand of v cut to 24 bits, rounding down: what the x87 does to the sum in faddp under the original's control word */
#if LDBL_MANT_DIG == 64
typedef long double gt3_pow_t;
#define GT3_POW_LOG2(x)     log2l(x)
#define GT3_POW_EXP2(x)     exp2l(x)
#define GT3_POW_FREXP(x, e) frexpl(x, e)
#define GT3_POW_LDEXP(x, e) ldexpl(x, e)
#define GT3_POW_FLOOR(x)    floorl(x)
#define GT3_POW_TRUNC(x)    truncl(x)
#else
typedef double gt3_pow_t;
#define GT3_POW_LOG2(x)     log2(x)
#define GT3_POW_EXP2(x)     exp2(x)
#define GT3_POW_FREXP(x, e) frexp(x, e)
#define GT3_POW_LDEXP(x, e) ldexp(x, e)
#define GT3_POW_FLOOR(x)    floor(x)
#define GT3_POW_TRUNC(x)    trunc(x)
#endif

/* sFPow of the intro, the x87 sequence  fyl2x; fprem; f2xm1; faddp; fscale: 2^(b * log2 a) with the integer part of the exponent applied by fscale and the fraction
   by f2xm1, the sum 1 + (2^f - 1) rounded down to 24 bits (the control word's precision), the result stored as a double. The base 0 gives 0, a base below 0 gives
   a NaN (fyl2x rejects it), even for an integer power. */
GT3_BITMAP_API double gt3_fpow_soft(double a, double b)
{
  if (a == 0.0) return 0.0;
  if (!(a > 0.0)) return NAN;
  const gt3_pow_t y = (gt3_pow_t)b * GT3_POW_LOG2((gt3_pow_t)a);
  if (!(y > -16000.0 && y < 16000.0)) return (double)pow(a, b);
  const gt3_pow_t whole = GT3_POW_TRUNC(y);
  gt3_pow_t s = GT3_POW_EXP2(y - whole);
  int e;
  const gt3_pow_t m = GT3_POW_FREXP(s, &e);
  s = GT3_POW_LDEXP(GT3_POW_FLOOR(GT3_POW_LDEXP(m, 24)), e - 24);
  return (double)GT3_POW_LDEXP(s, (int)whole);
}

GT3_BITMAP_API double gt3_fpow(double a, double b)
{
  if (a == 0.0) return 0.0;
  if (!(a > 0.0)) return NAN;
#if defined(GT3_FPOW_X87)
  /* the instructions themselves, under the control word 0x143f */
  uint16_t cw_old, cw_new = 0x143f;
  double r;
  __asm__ volatile("fnstcw %[old]\n\t"
                   "fldcw %[cw]\n\t"
                   "fldl %[b]\n\t"
                   "fldl %[a]\n\t"
                   "fyl2x\n\t"
                   "fld1\n\t"
                   "fld %%st(1)\n\t"
                   "fprem\n\t"
                   "f2xm1\n\t"
                   "faddp\n\t"
                   "fscale\n\t"
                   "fstp %%st(1)\n\t"
                   "fstpl %[r]\n\t"
                   "fldcw %[old]\n\t"
                   : [r] "=m"(r), [old] "=m"(cw_old)
                   : [a] "m"(a), [b] "m"(b), [cw] "m"(cw_new)
                   : "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)", "memory");
  return r;
#else
  return gt3_fpow_soft(a, b);
#endif
}

GT3_BITMAP_API void gt3_bitmap_glowrect(gt3_bitmap *bm, float cx, float cy, float rx, float ry, float sx, float sy, const int32_t color[4], float alpha,
                                        float power, gt3_pow_fn fpow)
{
  if (!fpow) fpow = gt3_fpow;
  if (power == 0) power = (1.0f / 65536.0f);
  power = (float)(0.25 / power);

  cx *= (float)bm->width;
  cy *= (float)bm->height;
  rx *= (float)bm->width;
  ry *= (float)bm->height;
  sx *= (float)bm->width;
  sy *= (float)bm->height;

  const float tresh = 1.0f / 65536.0f;
  if (rx < tresh) rx = tresh;
  rx = rx * rx;
  if (ry < tresh) ry = tresh;
  ry = ry * ry;

  alpha *= 65536.0f;
  const uint64_t col = gt3_color64(color);

  uint64_t *d = bm->pixel;
  for (int32_t y = 0; y < bm->height; y++) {
    float fy = (float)y - cy;
    if (fy < -sy)
      fy = fy + sy;
    else if (fy > sy)
      fy = fy - sy;
    else
      fy = 0;
    for (int32_t x = 0; x < bm->width; x++) {
      float fx = (float)x - cx;
      if (fx < -sx)
        fx = fx + sx;
      else if (fx > sx)
        fx = fx - sx;
      else
        fx = 0;
      const float a = 1 - (fx * fx / rx + fy * fy / ry);
      if (a > 0) {
        /* sFtol(sFPow(a, power) * alpha): a floor */
        const double v = fpow((double)a, (double)power) * (double)alpha;
        const int32_t f = (v >= -2147483648.0 && v < 2147483648.0) ? (int32_t)floor(v) : (int32_t)0x80000000u;
        *d = gt3_fade64(*d, col, f);
      }
      d++;
    }
  }
}

/* ---- Perlin ---- */

static const signed char gt3_gtable[16 * 4] = { 1, 1, 0, 0, 2, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 0, 1, 0, 1, 0, 2, 0, 1, 0, 1, 0, 2, 0, 2, 0, 2, 0,
                                                0, 1, 1, 0, 0, 2, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 1, 1, 0, 0, 2, 1, 0, 0, 0, 2, 1, 0, 0, 2, 2, 0 };
static const float gt3_gvalue[3] = { 0.0f, 1.0f, -1.0f };

GT3_BITMAP_API void gt3_perlin_init(gt3_perlin *p, gt3_rng *rng)
{
  gt3_rng_seed(rng, 1);
  for (int i = 0; i < 64; i++) p->gradient3d[i / 4][i % 4] = gt3_gvalue[(int)gt3_gtable[i]];

  for (int i = 0; i < 256; i++) {
    p->gradient[i][0] = (float)gt3_rng_below(rng, 0x10000);
    p->permute[i] = (uint8_t)i;
  }
  for (int i = 0; i < 255; i++) {
    for (int j = i + 1; j < 256; j++) {
      if (p->gradient[i][0] > p->gradient[j][0]) {
        const float f = p->gradient[i][0];
        p->gradient[i][0] = p->gradient[j][0];
        p->gradient[j][0] = f;
        const uint8_t t = p->permute[i];
        p->permute[i] = p->permute[j];
        p->permute[j] = t;
      }
    }
  }
  memcpy(p->permute + 256, p->permute, 256);

  for (int i = 0; i < 256;) {
    const int32_t x = (int32_t)(gt3_rng_below(rng, 0x10000) - 0x8000u);
    const int32_t y = (int32_t)(gt3_rng_below(rng, 0x10000) - 0x8000u);
    /* the sum of squares of int: it wraps to a negative number for (-32768, -32768), which the original accepts */
    if ((int32_t)((uint32_t)x * (uint32_t)x + (uint32_t)y * (uint32_t)y) < 0x8000 * 0x8000) {
      p->gradient[i][0] = (float)x / 32768.0f;
      p->gradient[i][1] = (float)y / 32768.0f;
      i++;
    }
  }
}

static float gt3_noise2(const gt3_perlin *p, int32_t x, int32_t y, int32_t mask, int32_t seed)
{
  mask &= 255;
  seed &= 255;
  const int32_t vx = gt3_sar(x, 16);
  float tx = (float)(x & 0xffff) / 65536.0f;
  const int32_t vy = gt3_sar(y, 16);
  float ty = (float)(y & 0xffff) / 65536.0f;
  const int v00 = p->permute[((vx + 0) & mask) + p->permute[((vy + 0) & mask) ^ seed]];
  const int v01 = p->permute[((vx + 1) & mask) + p->permute[((vy + 0) & mask) ^ seed]];
  const int v10 = p->permute[((vx + 0) & mask) + p->permute[((vy + 1) & mask) ^ seed]];
  const int v11 = p->permute[((vx + 1) & mask) + p->permute[((vy + 1) & mask) ^ seed]];
  const float f00 = p->gradient[v00][0] * (tx - 0) + p->gradient[v00][1] * (ty - 0);
  const float f01 = p->gradient[v01][0] * (tx - 1) + p->gradient[v01][1] * (ty - 0);
  const float f10 = p->gradient[v10][0] * (tx - 0) + p->gradient[v10][1] * (ty - 1);
  const float f11 = p->gradient[v11][0] * (tx - 1) + p->gradient[v11][1] * (ty - 1);
  tx = tx * tx * (3 - 2 * tx);
  ty = ty * ty * (3 - 2 * ty);
  const float fa = f00 + (f01 - f00) * tx;
  const float fb = f10 + (f11 - f10) * tx;
  return fa + (fb - fa) * ty;
}

/* a float to int32 conversion with the x86's "integer indefinite" for what does not fit (cvttss2si / fistp) */
static int32_t gt3_trunc(float f)
{
  if (!(f > -2147483904.0f && f < 2147483648.0f)) return (int32_t)0x80000000u;
  return (int32_t)f;
}

GT3_BITMAP_API gt3_bitmap *gt3_bitmap_perlin(const gt3_perlin *p, int32_t freq, int32_t oct, float fadeoff, int32_t seed, int32_t mode, float amp, float gamma,
                                             const int32_t col0[4], const int32_t col1[4], gt3_pow_fn fpow)
{
  if (!fpow) fpow = gt3_fpow;
  gt3_bitmap *bm = gt3_bitmap_create(GT3_BITMAP_SIZE, GT3_BITMAP_SIZE);
  if (!bm) return NULL;
  const float pi2 = 6.28318530717958647692528676655901f;
  freq = gt3_sar(freq, 16);
  oct = gt3_sar(oct, 16);
  seed = gt3_sar(seed, 16);
  mode = gt3_sar(mode, 16);
  const uint64_t c0 = gt3_color64(col0), c1 = gt3_color64(col1);
  const int shift = 8; /* sGetPower2(256) */
  uint64_t *tile = bm->pixel;
  for (int32_t y = 0; y < bm->width; y++) {
    for (int32_t x = 0; x < bm->height; x++) {
      float n = 0;
      float s = 1.0f;
      for (int32_t i = freq; i < freq + oct; i++) {
        const int32_t px = (int32_t)(((uint32_t)x << (16 - shift)) << i);
        const int32_t py = (int32_t)(((uint32_t)y << (16 - shift)) << i);
        float nn = gt3_noise2(p, px, py, (1 << i) - 1, seed);
        if (mode & 1) nn = (float)fabs((double)nn);
        if (mode & 2) nn = (float)(sin((double)(nn * pi2)) * 0.5f);
        n += nn * s;
        s *= fadeoff;
      }
      if (mode & 1)
        n = (float)fpow((double)(n * amp), (double)gamma);
      else
        n = (float)fpow((double)(n * amp * 0.5f + 0.5f), (double)gamma);
      const int32_t val = gt3_range7fff(gt3_trunc(n * 0x8000)) * 2;
      *tile = gt3_fade64(c0, c1, val);
      tile++;
    }
  }
  return bm;
}

/* ---- text, textures ---- */

GT3_BITMAP_API void gt3_bitmap_text(gt3_bitmap *bm, const int32_t color[4], const uint8_t *coverage)
{
  const uint64_t col = gt3_color64(color);
  const size_t n = (size_t)bm->width * (size_t)bm->height;
  for (size_t i = 0; i < n; i++) bm->pixel[i] = gt3_fade64(bm->pixel[i], col, (int32_t)((coverage ? coverage[i] : 0u) * (65536 / 255)));
}

GT3_BITMAP_API void gt3_bitmap_to_bgra(const gt3_bitmap *bm, uint8_t *out)
{
  const size_t n = (size_t)bm->width * (size_t)bm->height;
  for (size_t i = 0; i < n; i++)
    for (int k = 0; k < 4; k++) out[i * 4 + (size_t)k] = (uint8_t)(GT3_LANE(bm->pixel[i], k) >> 7);
}

GT3_BITMAP_API void gt3_texture_next_level(uint8_t *dst, const uint8_t *src, int32_t width, int32_t height)
{
  const int32_t w = width >> 1, h = height >> 1;
  for (int32_t y = 0; y < h; y++) {
    for (int32_t x = 0; x < w; x++) {
      const uint8_t *p0 = src + ((size_t)(2 * y) * (size_t)width + (size_t)(2 * x)) * 4u;
      const uint8_t *p1 = p0 + 4, *p2 = p0 + (size_t)width * 4u, *p3 = p2 + 4;
      uint8_t *d = dst + ((size_t)y * (size_t)w + (size_t)x) * 4u;
      /* ((a & 0xfc) >> 2) per byte of the four, added: the original's word arithmetic */
      for (int k = 0; k < 4; k++) d[k] = (uint8_t)((p0[k] >> 2) + (p1[k] >> 2) + (p2[k] >> 2) + (p3[k] >> 2));
    }
  }
}

#undef GT3_LANE
#undef GT3_PACK

#endif /* GT3_BITMAP_IMPLEMENTATION_DONE */
#endif /* GT3_BITMAP_IMPLEMENTATION */
