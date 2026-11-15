/* The textures of the generation against the oracle.
 *
 *   suite_textures <assets dir> <oracle fixtures dir>
 *
 * Runs default.pac's generation (what gt3_session_init does) and compares the 8 bit textures the script hands to the device (Bitmap_MakeTexture and
 * Material_Texture) with tests/data/candytron/oracle/tex.txt: the SHA-256 of the pixels (blue, green, red, alpha) and of the whole mip chain, built with the
 * original's box filter, of every texture, the handles 10 to 37. The oracle leaves the strings blank (its GDI is not there), so does this run.
 *
 * Two runs: the first in the mode of the original (round downward, the x87 power function), which must be byte identical; the second as WebAssembly will
 * run it (rounding to nearest, the software power function), which is compared with the first, pixel by pixel.
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
#define GT3_TEXT_STATIC
#define GT3_TEXT_IMPLEMENTATION
#include <candytron/core/text.h>
#define CT_SHA256_IMPLEMENTATION
#define CT_ASSETS_IMPLEMENTATION
#include <candytron/core/assets.h>

#define MAXTEX 64

typedef struct baked {
  int32_t n;             /* the number of handles, render targets included */
  uint8_t *bgra[MAXTEX]; /* NULL for the render targets */
  int32_t width[MAXTEX], height[MAXTEX];
} baked;

static void baked_free(baked *b)
{
  for (int i = 0; i < MAXTEX; i++) free(b->bgra[i]);
  memset(b, 0, sizeof *b);
}

/* the generation, in the given mode of the floating point unit and with the given power function; the textures are copied out */
static int bake(const gt3_program *prog, gt3_pow_fn pow, gt3_text_fn text, void *text_user, int exact, baked *out)
{
  memset(out, 0, sizeof *out);
  gt3_vm *vm = (gt3_vm *)calloc(1, sizeof *vm);
  gt3_vm_init(vm, prog);
  gt3_register_natives(vm);
  gt3_session s;
  memset(&s, 0, sizeof s);
  s.pow = pow;
  s.text = text;
  s.text_user = text_user;
  const int rounding = exact ? gt3_round_down() : -1;
  const int ok = gt3_session_init(&s, vm);
  gt3_round_restore(rounding);
  if (ok) {
    out->n = s.gen.n_texture < MAXTEX ? s.gen.n_texture : MAXTEX;
    for (int32_t i = 0; i < out->n; i++) {
      const gt3_texture *t = &s.gen.texture[i];
      if (!t->bgra) continue;
      const size_t size = (size_t)t->width * (size_t)t->height * 4u;
      out->bgra[i] = (uint8_t *)malloc(size);
      memcpy(out->bgra[i], t->bgra, size);
      out->width[i] = t->width;
      out->height[i] = t->height;
    }
  }
  gt3_session_exit(&s);
  gt3_vm_exit(vm);
  free(vm);
  return ok;
}

/* the baked provider, counting the calls it answers */
static int counting_text(void *user, const char *text, int width, int height, int x, int y, int cw, int ch, uint8_t *coverage)
{
  const int found = gt3_text_baked(NULL, text, width, height, x, y, cw, ch, coverage);
  *(int *)user += found;
  return found;
}

static void hash_hex(const void *data, size_t size, char hex[65])
{
  uint8_t digest[32];
  ct_sha256 h;
  ct_sha256_init(&h);
  ct_sha256_update(&h, data, size);
  ct_sha256_final(&h, digest);
  ct_sha256_hex(digest, hex);
}

/* the oracle's hash_texture: the top level, and the chain of all the levels down to a width or height of 1 */
static void hash_texture(const uint8_t *bgra, int32_t w, int32_t h, char top[65], char chain[65], int *levels)
{
  hash_hex(bgra, (size_t)w * (size_t)h * 4u, top);
  uint8_t *a = (uint8_t *)malloc((size_t)w * (size_t)h * 4u), *b = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
  memcpy(a, bgra, (size_t)w * (size_t)h * 4u);
  uint8_t digest[32];
  ct_sha256 s;
  ct_sha256_init(&s);
  int level = 0;
  for (;;) {
    ct_sha256_update(&s, a, (size_t)w * (size_t)h * 4u);
    level++;
    if (w <= 1 || h <= 1) break;
    gt3_texture_next_level(b, a, w, h);
    w >>= 1;
    h >>= 1;
    uint8_t *t = a;
    a = b;
    b = t;
  }
  ct_sha256_final(&s, digest);
  ct_sha256_hex(digest, chain);
  *levels = level;
  free(a);
  free(b);
}

static char *read_all(const char *path)
{
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *text = (char *)malloc((size_t)n + 1);
  if (text && fread(text, 1, (size_t)n, f) == (size_t)n)
    text[n] = 0;
  else {
    free(text);
    text = NULL;
  }
  fclose(f);
  return text;
}

int suite_textures(int argc, char **argv)
{
  if (argc < 3) {
    printf("usage: suite_textures <assets dir> <oracle fixtures dir>\n");
    return 2;
  }
  char error[512];
  ct_assets assets;
  CHECK(ct_assets_load(&assets, argv[1], error, sizeof error), "assets: %s", error);
  if (!assets.data[CT_ASSET_SCRIPT]) return 1;
  gt3_program prog;
  CHECK(gt3_program_load(&prog, assets.data[CT_ASSET_SCRIPT], assets.size[CT_ASSET_SCRIPT], error, sizeof error), "script: %s", error);

  char path[2048];
  snprintf(path, sizeof path, "%s/tex.txt", argv[2]);
  char *fixture = read_all(path);
  CHECK(fixture != NULL, "cannot read %s", path);
  if (!fixture) return 1;

  const int probe = gt3_round_down();
  const int can_round = probe != -1;
  gt3_round_restore(probe);

  baked exact, soft;
  memset(&exact, 0, sizeof exact);
  memset(&soft, 0, sizeof soft);
  int ok = 0;

  if (can_round) {
    ok = bake(&prog, NULL, NULL, NULL, 1, &exact);
    CHECK(ok, "the generation ran without an error (exact mode)");
    int checked = 0, distinct = 0, rt = 0;
    char seen[MAXTEX][65];
    for (char *line = fixture; line && *line;) {
      char *next = strchr(line, '\n');
      if (next) *next++ = 0;
      int h = 0, w = 0, ht = 0, lv = 0;
      char top[65], chain[65];
      if (!strncmp(line, "RT ", 3)) rt++;
      if (sscanf(line, "UPD %d %dx%d sha256=%64s levels=%d mipchain=%64s", &h, &w, &ht, top, &lv, chain) == 6) {
        const int has = ok && h >= 0 && h < exact.n && exact.bgra[h] != NULL;
        CHECK(has, "texture %d exists", h);
        if (has) {
          CHECK(exact.width[h] == w && exact.height[h] == ht, "texture %d is %dx%d", h, (int)exact.width[h], (int)exact.height[h]);
          char our_top[65], our_chain[65];
          int our_levels;
          hash_texture(exact.bgra[h], exact.width[h], exact.height[h], our_top, our_chain, &our_levels);
          CHECK(!strcmp(our_top, top), "texture %d pixels %s, the oracle's %s", h, our_top, top);
          CHECK(our_levels == lv, "texture %d has %d levels, the oracle's %d", h, our_levels, lv);
          CHECK(!strcmp(our_chain, chain), "texture %d mip chain %s, the oracle's %s", h, our_chain, chain);
          int dup = 0;
          for (int k = 0; k < distinct; k++) dup |= !strcmp(seen[k], top);
          if (!dup) memcpy(seen[distinct++], top, 65);
        }
        checked++;
      }
      line = next;
    }
    CHECK(rt == GT3_RENDER_TARGETS, "%d render targets in the fixture", rt);
    CHECK(ok && exact.n == 38, "%d handles in all", (int)exact.n);
    CHECK(checked == 28, "%d textures in the fixture", checked);
    printf("textures: %d textures against the oracle (%d distinct), exact in pixels and in the mip chain\n", checked, distinct);
  } else {
    printf("textures: no control of the rounding mode here: the comparison with the oracle is left to the native build\n");
  }

  /* what WebAssembly runs: rounding to nearest, the software power function */
  const int ok_soft = bake(&prog, gt3_fpow_soft, NULL, NULL, 0, &soft);
  CHECK(ok_soft, "the generation ran without an error (nearest, software power)");
  if (ok_soft && can_round && ok) {
    long diff_bytes = 0, diff_pixels = 0, total_pixels = 0, textures_off = 0;
    int worst = 0;
    for (int h = 0; h < exact.n; h++) {
      if (!exact.bgra[h]) continue;
      CHECK(soft.bgra[h] != NULL && soft.width[h] == exact.width[h], "texture %d exists in both runs", h);
      if (!soft.bgra[h]) continue;
      const long px = (long)exact.width[h] * exact.height[h];
      long off = 0;
      for (long i = 0; i < px; i++) {
        int pixel_off = 0;
        for (int k = 0; k < 4; k++) {
          int d = (int)exact.bgra[h][i * 4 + k] - (int)soft.bgra[h][i * 4 + k];
          if (d < 0) d = -d;
          if (d) {
            diff_bytes++;
            pixel_off = 1;
            if (d > worst) worst = d;
          }
        }
        off += pixel_off;
      }
      diff_pixels += off;
      total_pixels += px;
      textures_off += off != 0;
    }
    printf("textures: nearest + software power against exact: %ld of %ld pixels differ (%.3f %%) in %ld textures, %ld bytes, largest difference %d\n",
           diff_pixels, total_pixels, 100.0 * (double)diff_pixels / (double)total_pixels, textures_off, diff_bytes, worst);
    CHECK(worst <= 1, "no byte is off by more than 1 (%d)", worst);
    CHECK(diff_pixels * 100 <= total_pixels * 1, "at most 1 %% of the pixels differ (%.3f %%)", 100.0 * (double)diff_pixels / (double)total_pixels);
  }

  /* the baked text: the 8 calls are all answered, the textures without text are the oracle's, those with text (handles 12, 16, 21, 27, 37) are not blank any more */
  if (can_round && ok) {
    baked texted;
    int answered = 0;
    const int ok_text = bake(&prog, NULL, counting_text, &answered, 1, &texted);
    CHECK(ok_text, "the generation ran without an error (baked text)");
    CHECK(answered == 8, "%d of the 8 text calls found a baked string", answered);
    static const int with_text[5] = { 12, 16, 21, 27, 37 };
    int changed = 0;
    for (int h = 10; ok_text && h < exact.n; h++) {
      int is_text = 0;
      for (int k = 0; k < 5; k++) is_text |= with_text[k] == h;
      const size_t size = (size_t)exact.width[h] * (size_t)exact.height[h] * 4u;
      const int same = texted.bgra[h] && !memcmp(texted.bgra[h], exact.bgra[h], size);
      if (is_text)
        changed += !same;
      else
        CHECK(same, "texture %d has no text and is the oracle's", h);
    }
    CHECK(changed == 5, "%d of the 5 textures with text changed", changed);
    printf("textures: baked text: %d calls answered, %d textures with text\n", answered, changed);
    baked_free(&texted);
  }

  baked_free(&exact);
  baked_free(&soft);
  free(fixture);
  gt3_program_free(&prog);
  ct_assets_free(&assets);
  printf("textures: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
