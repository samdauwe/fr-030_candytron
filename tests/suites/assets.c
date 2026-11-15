/* Checks for assets.h and the production data.
 *
 *   suite_assets <assets dir> <fixtures dir>
 *
 * The files are the production's: size, SHA-256 and the outer structure of each, the facts about them that the script machine, the mesh
 * decoder and the synthesizer depend on (the packed script's streams, the song's header, the mesh's trailer), and the refusal of a file that is almost right. The fixture
 * assets.sha256 (also checked by CMake's own SHA-256 in the test assets_sha256) must say what the library says.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "support/check.h"
#include "support/suite.h"

#define CT_SHA256_IMPLEMENTATION
#define CT_ASSETS_IMPLEMENTATION
#include <candytron/core/assets.h>

static int write_file(const char *path, const void *data, size_t size)
{
  FILE *f = fopen(path, "wb");
  if (!f) return 0;
  const int ok = fwrite(data, 1, size, f) == size;
  return fclose(f) == 0 && ok;
}

static uint8_t *read_file(const char *dir, const char *name, size_t *size)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *buffer = (uint8_t *)malloc((size_t)n + 1);
  if (!buffer || fread(buffer, 1, (size_t)n, f) != (size_t)n) {
    free(buffer);
    buffer = NULL;
  } else
    *size = (size_t)n;
  fclose(f);
  return buffer;
}

int suite_assets(int argc, char **argv)
{
  if (argc < 3) {
    printf("usage: suite_assets <assets dir> <fixtures dir>\n");
    return 2;
  }
  const char *assets_dir = argv[1], *fixtures_dir = argv[2];
  char error[512];

  CHECK(ct_asset_describe(CT_ASSET_COUNT) == NULL && ct_asset_describe((ct_asset_id)-1) == NULL, "ids out of range have no description");

  /* every file, as the library knows it */
  uint8_t *file[CT_ASSET_COUNT] = { NULL };
  size_t size[CT_ASSET_COUNT] = { 0 };
  for (int i = 0; i < CT_ASSET_COUNT; i++) {
    const ct_asset_info *info = ct_asset_describe((ct_asset_id)i);
    file[i] = read_file(assets_dir, info->file, &size[i]);
    CHECK(file[i] != NULL, "cannot read %s/%s", assets_dir, info->file);
    if (!file[i]) goto done;
    error[0] = 0;
    CHECK(ct_asset_check((ct_asset_id)i, file[i], size[i], error, sizeof error), "%s: %s", info->file, error);
  }

  /* the fixture and the library's table say the same */
  {
    char path[1024], line[512];
    snprintf(path, sizeof path, "%s/assets.sha256", fixtures_dir);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL, "cannot read %s", path);
    int seen = 0;
    while (f && fgets(line, sizeof line, f)) {
      char hex[65], name[256];
      if (sscanf(line, "%64s %255s", hex, name) != 2) continue;
      for (int i = 0; i < CT_ASSET_COUNT; i++) {
        const ct_asset_info *info = ct_asset_describe((ct_asset_id)i);
        if (strcmp(info->file, name)) continue;
        seen++;
        CHECK(!strcmp(info->sha256, hex), "%s: the fixture and assets.h disagree", name);
      }
    }
    if (f) fclose(f);
    CHECK(seen == CT_ASSET_COUNT, "the fixture lists %d of %d assets", seen, CT_ASSET_COUNT);
  }

  /* what the script machine relies on: the packed script's streams (cslrt.cpp PackedImport) */
  {
    ct_pac_header h;
    CHECK(ct_pac_parse_header(file[CT_ASSET_SCRIPT], size[CT_ASSET_SCRIPT], &h, error, sizeof error), "pac header: %s", error);
    CHECK(h.code_offset == 28 && h.imm_offset == 23736 && h.func_offset == 64868 && h.func_count == 58 && h.roots_offset == 65332 && h.vars_offset == 65332 &&
            h.strings_offset == 74028,
          "pac offsets %u %u %u %u %u %u %u", h.code_offset, h.imm_offset, h.func_offset, h.func_count, h.roots_offset, h.vars_offset, h.strings_offset);
    CHECK(h.code_bytes == 23708 && h.imm_words == 10283 && h.var_refs == 4348 && h.string_bytes == 253, "pac streams %u %u %u %u", h.code_bytes, h.imm_words,
          h.var_refs, h.string_bytes);

    /* the structure is checked on its own, whatever the hash says */
    uint8_t *copy = (uint8_t *)malloc(size[CT_ASSET_SCRIPT]);
    ct_pac_header bad;
    memcpy(copy, file[CT_ASSET_SCRIPT], size[CT_ASSET_SCRIPT]);
    copy[12]++; /* func_count 59: the function table no longer ends where the roots start */
    CHECK(!ct_pac_parse_header(copy, size[CT_ASSET_SCRIPT], &bad, error, sizeof error) && strstr(error, "header offsets"),
          "a wrong function count is refused: %s", error);
    memcpy(copy, file[CT_ASSET_SCRIPT], size[CT_ASSET_SCRIPT]);
    CHECK(!ct_pac_parse_header(copy, 27, &bad, error, sizeof error) && strstr(error, "28-byte header"), "a script shorter than its header is refused");
    free(copy);
  }
  {
    ct_v2m_header h;
    CHECK(ct_v2m_parse_header(file[CT_ASSET_SONG], size[CT_ASSET_SONG], &h, error, sizeof error), "v2m header: %s", error);
    CHECK(h.timediv == 480 && h.maxtime == 206898 && h.globals == 1, "v2m header %u %u %u", h.timediv, h.maxtime, h.globals);
    CHECK(!ct_v2m_parse_header(file[CT_ASSET_SONG], 11, &h, error, sizeof error), "a song shorter than its header is refused");
    CHECK(!ct_v2m_parse_header(file[CT_ASSET_SONG], 20, &h, error, sizeof error), "a song that ends inside its global events is refused");
  }
  CHECK(ct_mesh_check_trailer(file[CT_ASSET_MESH], size[CT_ASSET_MESH], error, sizeof error), "mesh trailer: %s", error);
  CHECK(!ct_mesh_check_trailer(file[CT_ASSET_MESH], size[CT_ASSET_MESH] - 1, error, sizeof error), "a mesh that does not end with 'ryg' is refused");

  /* almost right is wrong: one flipped bit, one byte too few, one byte too many */
  for (int i = 0; i < CT_ASSET_COUNT; i++) {
    const ct_asset_info *info = ct_asset_describe((ct_asset_id)i);
    uint8_t *copy = (uint8_t *)malloc(size[i] + 1);
    memcpy(copy, file[i], size[i]);
    copy[size[i] / 2] ^= 1;
    CHECK(!ct_asset_check((ct_asset_id)i, copy, size[i], error, sizeof error) && strstr(error, "SHA-256"), "%s: a flipped bit is refused: %s", info->file,
          error);
    memcpy(copy, file[i], size[i]);
    CHECK(!ct_asset_check((ct_asset_id)i, copy, size[i] - 1, error, sizeof error) && strstr(error, "bytes, expected"), "%s: a short file is refused",
          info->file);
    copy[size[i]] = 0;
    CHECK(!ct_asset_check((ct_asset_id)i, copy, size[i] + 1, error, sizeof error) && strstr(error, "bytes, expected"), "%s: a long file is refused",
          info->file);
    free(copy);
  }

  /* the directory loader */
  {
    ct_assets a;
    CHECK(ct_assets_load(&a, assets_dir, error, sizeof error), "load: %s", error);
    for (int i = 0; i < CT_ASSET_COUNT; i++) CHECK(a.size[i] == ct_asset_describe((ct_asset_id)i)->size && a.data[i], "asset %d loaded", i);
    ct_assets_free(&a);
    CHECK(a.data[0] == NULL && a.size[0] == 0, "free clears");

    CHECK(!ct_assets_load(&a, CANDYTRON_TEST_WORK_DIR "/no-such-directory", error, sizeof error) && strstr(error, "cannot read"), "a missing directory: %s",
          error);
    CHECK(a.data[0] == NULL && a.data[1] == NULL && a.data[2] == NULL, "a failed load keeps nothing");

    /* a directory with the right files but one changed byte in the second */
    char dir[1024], path[2048];
    snprintf(dir, sizeof dir, "%s/tampered_assets", CANDYTRON_TEST_WORK_DIR);
    mkdir(CANDYTRON_TEST_WORK_DIR, 0777);
    mkdir(dir, 0777);
    int written = 1;
    for (int i = 0; i < CT_ASSET_COUNT; i++) {
      snprintf(path, sizeof path, "%s/%s", dir, ct_asset_describe((ct_asset_id)i)->file);
      if (i == CT_ASSET_SONG) file[i][100] ^= 0x80;
      written &= write_file(path, file[i], size[i]);
      if (i == CT_ASSET_SONG) file[i][100] ^= 0x80;
    }
    CHECK(written, "cannot write the tampered copy in %s", dir);
    CHECK(!ct_assets_load(&a, dir, error, sizeof error) && strstr(error, "josie.v2m") && strstr(error, "SHA-256"), "a tampered song is refused by name: %s",
          error);
    CHECK(a.data[CT_ASSET_SCRIPT] == NULL, "a failed load keeps nothing (the script that was fine is freed too)");
    for (int i = 0; i < CT_ASSET_COUNT; i++) {
      snprintf(path, sizeof path, "%s/%s", dir, ct_asset_describe((ct_asset_id)i)->file);
      remove(path);
    }
    rmdir(dir);
  }

done:
  for (int i = 0; i < CT_ASSET_COUNT; i++) free(file[i]);
  printf("assets: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
