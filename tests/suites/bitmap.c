/* Checks for rand.h and bitmap.h: the generator, the 15 bit colour lanes, the fade, the operators, the power function, the Perlin tables and the mip chain, on
 * values worked out by hand (the whole generation is compared with the oracle by the suites csl_trace and textures). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/check.h"
#include "support/suite.h"

#define GT3_BITMAP_STATIC
#define GT3_BITMAP_IMPLEMENTATION
#include <candytron/core/bitmap.h>
#include <candytron/core/rand.h>

#define GT3_LANE_OF(v, k) ((uint16_t)((v) >> (16 * (k))))

/* rand.h: sGetRnd's first numbers from the original's seed (worked out with the original's formula in Python) */
static void check_rng(void)
{
  gt3_rng r;
  gt3_rng_init(&r);
  CHECK(gt3_rng_next(&r) == 0xdbdea24du, "first number");
  CHECK(gt3_rng_next(&r) == 0x3b3237ebu, "second number");
  CHECK(gt3_rng_next(&r) == 0x478bf349u, "third number");
  CHECK(r.state == 0xf7cd27b7u, "state after three numbers: %08x", (unsigned)r.state);
  gt3_rng a, b;
  gt3_rng_init(&a);
  gt3_rng_init(&b);
  gt3_rng_seed(&a, 7);
  gt3_rng_seed(&b, 7);
  CHECK(a.state == b.state, "the same seed gives the same state");
  gt3_rng_seed(&b, 8);
  CHECK(a.state != b.state, "another seed gives another state");
  gt3_rng_init(&a);
  for (int i = 0; i < 1000; i++) CHECK(gt3_rng_below(&a, 17) < 17, "below stays below");
}

/* the script's colour: 16.16 fixed point 0..255 in the low bits, halved to 15 bit lanes (blue, green, red, alpha) */
static void check_color(void)
{
  const int32_t c[4] = { 0xff00, 0x8000, 0x0000, 0xffff }; /* red, green, blue, alpha as the script gives them */
  const uint64_t v = gt3_color64(c);
  CHECK(((v >> 0) & 0xffff) == 0x0000, "blue lane");
  CHECK(((v >> 16) & 0xffff) == 0x4000, "green lane");
  CHECK(((v >> 32) & 0xffff) == 0x7f80, "red lane");
  CHECK(((v >> 48) & 0xffff) == 0x7fff, "alpha lane clamps at 0x7fff (%04x)", (unsigned)((v >> 48) & 0xffff));
  const int32_t neg[4] = { -5, 0x20000, 0, 0 };
  const uint64_t n = gt3_color64(neg);
  CHECK(((n >> 32) & 0xffff) == 0, "a negative value clamps at 0");
  CHECK(((n >> 16) & 0xffff) == 0x7fff, "a large value clamps at 0x7fff");
}

/* the fade: c0 * (1 - f) + c1 * f in 15 bit lanes, with the two rounding losses of the MMX code */
static void check_fade(void)
{
  const uint64_t a = 0x0000000040000000ull | 0x7fff000000000000ull;
  const uint64_t b = 0x1000200030004000ull;
  /* a fade of 1.0 (65536) is all of c1, within the loss of the two products */
  uint64_t r = gt3_fade64(a, b, 65536);
  for (int k = 0; k < 4; k++) {
    const int d = (int)GT3_LANE_OF(r, k) - (int)GT3_LANE_OF(b, k);
    CHECK(d >= -2 && d <= 0, "fade 1.0 lane %d: %04x for %04x", k, (unsigned)GT3_LANE_OF(r, k), (unsigned)GT3_LANE_OF(b, k));
  }
  /* a fade of 0 is c0, with the same loss (the fade by 0 of Bitmap_Text still rounds) */
  r = gt3_fade64(a, b, 0);
  for (int k = 0; k < 4; k++) {
    const int d = (int)GT3_LANE_OF(r, k) - (int)GT3_LANE_OF(a, k);
    CHECK(d >= -2 && d <= 0, "fade 0 lane %d: %04x for %04x", k, (unsigned)GT3_LANE_OF(r, k), (unsigned)GT3_LANE_OF(a, k));
  }
  /* half and half of 0x4000 and 0x2000 */
  const uint64_t x = 0x4000ull, y = 0x2000ull;
  r = gt3_fade64(x, y, 32768);
  CHECK(GT3_LANE_OF(r, 0) >= 0x2ffc && GT3_LANE_OF(r, 0) <= 0x3000, "half: %04x", (unsigned)GT3_LANE_OF(r, 0));
  /* an overshoot (the script's alpha above 1) saturates the weights: c0 * -1 + c1 * 1 in 15 bit lanes, wrapped in the 16 bit lane (2 * (-0x2000 + 0xfff)) */
  r = gt3_fade64(x, y, 3 * 65536);
  CHECK(GT3_LANE_OF(r, 0) == 0xdffe, "overshoot: %04x", (unsigned)GT3_LANE_OF(r, 0));
}

static uint64_t px(uint16_t b, uint16_t g, uint16_t r, uint16_t a) { return (uint64_t)b | ((uint64_t)g << 16) | ((uint64_t)r << 32) | ((uint64_t)a << 48); }

static void check_operators(void)
{
  gt3_bitmap *d = gt3_bitmap_create(2, 2), *s = gt3_bitmap_create(2, 2);
  CHECK(d && s, "bitmaps");
  if (!d || !s) return;
  const int32_t zero[4] = { 0, 0, 0, 0 };

  /* add saturates at 0x7fff (a signed saturation: paddsw), sub at 0 (psubusw) */
  d->pixel[0] = px(0x7000, 0x1000, 0x0001, 0x7fff);
  s->pixel[0] = px(0x7000, 0x2000, 0x0002, 0x0001);
  gt3_bitmap *t = gt3_bitmap_copy(d);
  gt3_bitmap_merge(t, s, 0);
  CHECK(t->pixel[0] == px(0x7fff, 0x3000, 0x0003, 0x7fff), "add: %016llx", (unsigned long long)t->pixel[0]);
  free(t);
  t = gt3_bitmap_copy(d);
  gt3_bitmap_merge(t, s, 1);
  CHECK(t->pixel[0] == px(0x0000, 0x0000, 0x0000, 0x7ffe), "sub: %016llx", (unsigned long long)t->pixel[0]);
  free(t);

  /* mul: (a * b >> 16) << 1 per lane: 0x4000 * 0x4000 -> 0x2000 */
  d->pixel[1] = px(0x4000, 0x7fff, 0x0000, 0x2000);
  s->pixel[1] = px(0x4000, 0x4000, 0x7fff, 0x4000);
  t = gt3_bitmap_copy(d);
  gt3_bitmap_merge(t, s, 2);
  CHECK(GT3_LANE_OF(t->pixel[1], 0) == 0x2000 && GT3_LANE_OF(t->pixel[1], 1) == 0x3ffe && GT3_LANE_OF(t->pixel[1], 2) == 0 &&
          GT3_LANE_OF(t->pixel[1], 3) == 0x1000,
        "mul: %016llx", (unsigned long long)t->pixel[1]);
  free(t);

  /* invert flips the 15 bits of every lane */
  t = gt3_bitmap_copy(d);
  gt3_bitmap_color(t, 4, zero);
  CHECK(t->pixel[1] == px(0x3fff, 0x0000, 0x7fff, 0x5fff), "invert: %016llx", (unsigned long long)t->pixel[1]);
  free(t);

  /* gray: the same luminance in blue, green and red, alpha at the maximum */
  d->pixel[2] = px(0x2000, 0x2000, 0x2000, 0x1234);
  t = gt3_bitmap_copy(d);
  gt3_bitmap_color(t, 3, zero);
  CHECK(GT3_LANE_OF(t->pixel[2], 0) == GT3_LANE_OF(t->pixel[2], 1) && GT3_LANE_OF(t->pixel[2], 1) == GT3_LANE_OF(t->pixel[2], 2), "gray lanes equal");
  CHECK(GT3_LANE_OF(t->pixel[2], 3) == (0x7fff | GT3_LANE_OF(t->pixel[2], 0)), "gray alpha: %04x", (unsigned)GT3_LANE_OF(t->pixel[2], 3));
  free(t);

  /* flat */
  const int32_t col[4] = { 0xff00, 0x8000, 0x4000, 0xff00 };
  gt3_bitmap_flat(d, col);
  CHECK(d->pixel[3] == px(0x2000, 0x4000, 0x7f80, 0x7f80), "flat: %016llx", (unsigned long long)d->pixel[3]);
  free(d);
  free(s);
}

/* the conversion of the texture: the 15 bit lane shifted right by 7 is the 8 bit channel; the mip level is the original's per byte (a >> 2) sum */
static void check_texture(void)
{
  gt3_bitmap *bm = gt3_bitmap_create(2, 1);
  bm->pixel[0] = px(0x7fff, 0x4000, 0x0080, 0x007f);
  bm->pixel[1] = px(0, 0, 0, 0);
  uint8_t *bgra = (uint8_t *)malloc((size_t)bm->width * (size_t)bm->height * 4u);
  gt3_bitmap_to_bgra(bm, bgra);
  CHECK(bgra[0] == 0xff && bgra[1] == 0x80 && bgra[2] == 0x01 && bgra[3] == 0x00, "bgra: %02x %02x %02x %02x", bgra[0], bgra[1], bgra[2], bgra[3]);
  free(bgra);
  free(bm);

  /* 2x2: four pixels of byte values 4, 8, 12, 16 -> 1 + 2 + 3 + 4; 255 four times -> 63 * 4 */
  uint8_t src[2 * 2 * 4], dst[4];
  for (int i = 0; i < 4; i++) memset(src + i * 4, 4 * (i + 1), 4);
  gt3_texture_next_level(dst, src, 2, 2);
  CHECK(dst[0] == 10 && dst[3] == 10, "mip of 4, 8, 12, 16: %d", dst[0]);
  memset(src, 255, sizeof src);
  gt3_texture_next_level(dst, src, 2, 2);
  CHECK(dst[0] == 252, "mip of 255 x 4: %d (the original truncates each term)", dst[0]);
  /* 4x2 gives 2x1 with the right pairing of the pixels */
  uint8_t wide[4 * 2 * 4], half[2 * 1 * 4];
  for (int i = 0; i < 8; i++) memset(wide + i * 4, i == 2 || i == 3 || i == 6 || i == 7 ? 40 : 8, 4);
  gt3_texture_next_level(half, wide, 4, 2);
  CHECK(half[0] == 8 && half[4] == 40, "mip of 4x2: %d %d", half[0], half[4]);
}

/* sFPow: 0 for a base of 0, a NaN below 0, and the hardware and software versions within a few units of the 24 bit significand */
static void check_pow(void)
{
  CHECK(gt3_fpow(0.0, 2.5) == 0.0 && gt3_fpow_soft(0.0, 2.5) == 0.0, "0 ^ x is 0");
  CHECK(isnan(gt3_fpow(-1.0, 2.0)) && isnan(gt3_fpow_soft(-1.0, 2.0)), "negative base is not a number, even for an integer power");
  CHECK(gt3_fpow(2.0, 10.0) == 1024.0 && gt3_fpow_soft(2.0, 10.0) == 1024.0, "2 ^ 10");
  CHECK(gt3_fpow(1.0, 123.0) == 1.0 && gt3_fpow_soft(1.0, 123.0) == 1.0, "1 ^ x");
  double worst = 0;
  for (int i = 1; i <= 2000; i++) {
    const double a = i / 2000.0 * 1.5, e = 0.37 + (i % 97) * 1.9;
    const double h = gt3_fpow(a, e), so = gt3_fpow_soft(a, e), r = pow(a, e);
    if (r > 1e-30) {
      double d1 = fabs(h - r) / r, d2 = fabs(so - r) / r, d3 = fabs(h - so) / r;
      if (d1 > worst) worst = d1;
      if (d2 > worst) worst = d2;
      if (d3 > worst) worst = d3;
    }
    CHECK(h <= r * (1 + 1e-7) && so <= r * (1 + 1e-7), "rounds down (%g ^ %g: %.10g %.10g, exact %.10g)", a, e, h, so, r);
  }
  CHECK(worst < 2e-6, "the three agree within 2e-6 (worst %.3g)", worst);
}

/* sInitPerlin: the permutation table is a permutation of 0..255, twice */
static void check_perlin(void)
{
  gt3_rng rng;
  gt3_rng_init(&rng);
  gt3_perlin p;
  gt3_perlin_init(&p, &rng);
  int seen[256] = { 0 };
  for (int i = 0; i < 256; i++) seen[p.permute[i]]++;
  int ok = 1;
  for (int i = 0; i < 256; i++) ok &= seen[i] == 1;
  CHECK(ok, "permute is a permutation");
  ok = 1;
  for (int i = 0; i < 256; i++) ok &= p.permute[i] == p.permute[i + 256];
  CHECK(ok, "permute repeats");
  /* the gradients are inside the unit disc */
  for (int i = 0; i < 256; i++) {
    const float l = p.gradient[i][0] * p.gradient[i][0] + p.gradient[i][1] * p.gradient[i][1];
    CHECK(l <= 1.0001f, "gradient %d has length^2 %g", i, l);
  }
  /* the same generator gives the same tables */
  gt3_perlin q;
  gt3_rng_init(&rng);
  gt3_perlin_init(&q, &rng);
  CHECK(!memcmp(&p, &q, sizeof p), "deterministic");
}

/* Bitmap_Text with no coverage: every pixel is faded by 0 towards the colour: rounded, not copied */
static void check_text(void)
{
  gt3_bitmap *bm = gt3_bitmap_create(2, 2);
  const int32_t col[4] = { 0xff00, 0xff00, 0xff00, 0xff00 };
  for (int i = 0; i < 4; i++) bm->pixel[i] = px(0x4000, 0x2000, 0x1000, 0x7fff);
  uint8_t cover[4] = { 0, 255, 0, 128 };
  gt3_bitmap_text(bm, col, cover);
  CHECK(GT3_LANE_OF(bm->pixel[1], 0) >= 0x7f7c, "full coverage gives the colour (%04x)", (unsigned)GT3_LANE_OF(bm->pixel[1], 0));
  CHECK(GT3_LANE_OF(bm->pixel[0], 0) >= 0x3ffc && GT3_LANE_OF(bm->pixel[0], 0) <= 0x4000, "no coverage keeps the pixel (%04x)",
        (unsigned)GT3_LANE_OF(bm->pixel[0], 0));
  const uint16_t mid = GT3_LANE_OF(bm->pixel[3], 0);
  CHECK(mid > 0x5000 && mid < 0x6000, "half coverage is in between (%04x)", (unsigned)mid);
  free(bm);
}

int suite_bitmap(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  check_rng();
  check_color();
  check_fade();
  check_operators();
  check_texture();
  check_pow();
  check_perlin();
  check_text();
  printf("bitmap: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
