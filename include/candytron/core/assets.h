/* assets.h - the production data of fr-030: Candytron - single-header C99 library (stb style)

   The player plays three files that are, byte for byte, the ones inside the final version 1.01 executable (assets/ATTRIBUTION.md):

     default.pac      the compiled and packed CSL script (GenThree cslrt.cpp, ScriptRuntime::PackedImport)
     josie.v2m        the soundtrack, a V2 module
     babe.mesh.old    the character mesh in the compact mesh format

   This library names them, knows their size and SHA-256, loads them and checks the outer structure of each. It does not interpret
   them: the script virtual machine (csl.h), the mesh decoder (babe.h) and the synthesizer (v2.h) do. The structural checks are
   what those libraries rely on first, and they fail loudly on a file that is merely the right size.

   USAGE
     In exactly one translation unit (the SHA-256 implementation is needed too):

         #define CT_SHA256_IMPLEMENTATION
         #define CT_ASSETS_IMPLEMENTATION
         #include <candytron/core/assets.h>

   CONFIGURATION (before including the implementation)
     CT_ASSETS_STATIC    make every API function static
*/
#ifndef CT_ASSETS_H_INCLUDED
#define CT_ASSETS_H_INCLUDED

#include <candytron/core/sha256.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CT_ASSETS_API
#ifdef CT_ASSETS_STATIC
#define CT_ASSETS_API static
#else
#define CT_ASSETS_API extern
#endif
#endif

typedef enum ct_asset_id { CT_ASSET_SCRIPT, CT_ASSET_SONG, CT_ASSET_MESH, CT_ASSET_COUNT } ct_asset_id;

typedef struct ct_asset_info {
  const char *file; /* name inside the assets directory */
  const char *what;
  size_t size;
  const char *sha256; /* 64 lower-case hex digits */
} ct_asset_info;

/* The packed script: PackedImport of cslrt.cpp reads seven u32 offsets and counts, then the streams they point to. */
typedef struct ct_pac_header {
  uint32_t code_offset;    /* one byte per opcode, plus the operand bytes of CODE / USER / FUNC / STRING */
  uint32_t imm_offset;     /* the 32-bit immediates (opcode 0 takes the next one) */
  uint32_t func_offset;    /* the table of func_count (native id, unused) pairs */
  uint32_t func_count;
  uint32_t roots_offset;   /* the broker roots: unused by the player, but it ends the function table */
  uint32_t vars_offset;    /* 16-bit variable indices of the opcodes 0x30..0x3f */
  uint32_t strings_offset; /* the string pool, up to the end of the file */
  uint32_t code_bytes, imm_words, var_refs, string_bytes;
} ct_pac_header;

typedef struct ct_v2m_header {
  uint32_t timediv; /* ticks per beat */
  uint32_t maxtime; /* length in ticks */
  uint32_t globals; /* global events; each is 10 bytes, right behind the header */
} ct_v2m_header;

CT_ASSETS_API const ct_asset_info *ct_asset_describe(ct_asset_id id); /* NULL for an id out of range */

/* SHA-256 of a buffer as 64 hex digits (+ terminator) */
CT_ASSETS_API void ct_assets_sha256_hex(const void *data, size_t size, char hex[65]);

/* The size and SHA-256 of `data` are those of asset `id`, and its structure is that of its kind. Returns 1, or 0 with the reason in `error`. */
CT_ASSETS_API int ct_asset_check(ct_asset_id id, const void *data, size_t size, char *error, size_t error_size);

CT_ASSETS_API int ct_pac_parse_header(const void *data, size_t size, ct_pac_header *header, char *error, size_t error_size);
CT_ASSETS_API int ct_v2m_parse_header(const void *data, size_t size, ct_v2m_header *header, char *error, size_t error_size);
/* the compact mesh format ends with the tag "ryg" */
CT_ASSETS_API int ct_mesh_check_trailer(const void *data, size_t size, char *error, size_t error_size);

/* Reads every asset from `dir` and runs ct_asset_check on it. On failure nothing stays allocated. */
typedef struct ct_assets {
  uint8_t *data[CT_ASSET_COUNT];
  size_t size[CT_ASSET_COUNT];
} ct_assets;
CT_ASSETS_API int ct_assets_load(ct_assets *assets, const char *dir, char *error, size_t error_size);
CT_ASSETS_API void ct_assets_free(ct_assets *assets);

#ifdef __cplusplus
}
#endif

#endif /* CT_ASSETS_H_INCLUDED */

#ifdef CT_ASSETS_IMPLEMENTATION
#ifndef CT_ASSETS_IMPLEMENTATION_DONE
#define CT_ASSETS_IMPLEMENTATION_DONE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const ct_asset_info ct_asset_table[CT_ASSET_COUNT] = {
  { "default.pac", "the packed CSL script", 74281u, "650835ca24d28d434e2d590eb454035026adcfc1e8d8824efd0e0da0dd61ac0d" },
  { "josie.v2m", "the V2 module", 39470u, "5bf17fb8d58d608cfa197d2309ecd629e5465698abf05338a4894c385ea5303b" },
  { "babe.mesh.old", "the Josie mesh", 11244u, "90c597b9acae8c3905fa63ff965a19115ee61d2bde6ed3cab13b98ac569ae267" },
};

static void ct_assets_fail(char *error, size_t error_size, const char *format, const char *a, unsigned long b, unsigned long c)
{
  if (error_size) snprintf(error, error_size, format, a, b, c);
}

static uint32_t ct_assets_le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

CT_ASSETS_API const ct_asset_info *ct_asset_describe(ct_asset_id id) { return (unsigned)id < (unsigned)CT_ASSET_COUNT ? &ct_asset_table[id] : NULL; }

CT_ASSETS_API void ct_assets_sha256_hex(const void *data, size_t size, char hex[65])
{
  ct_sha256 s;
  uint8_t digest[32];
  ct_sha256_init(&s);
  ct_sha256_update(&s, data, size);
  ct_sha256_final(&s, digest);
  ct_sha256_hex(digest, hex);
}

CT_ASSETS_API int ct_pac_parse_header(const void *data, size_t size, ct_pac_header *h, char *error, size_t error_size)
{
  const uint8_t *d = (const uint8_t *)data;
  if (size < 28u) {
    ct_assets_fail(error, error_size, "%s: %lu bytes: shorter than the 28-byte header", "pac", (unsigned long)size, 0);
    return 0;
  }
  h->code_offset = ct_assets_le32(d);
  h->func_offset = ct_assets_le32(d + 4);
  h->imm_offset = ct_assets_le32(d + 8);
  h->func_count = ct_assets_le32(d + 12);
  h->roots_offset = ct_assets_le32(d + 16);
  h->strings_offset = ct_assets_le32(d + 20);
  h->vars_offset = ct_assets_le32(d + 24);
  /* the streams follow each other: header, code, immediates, function table, (roots and) variable indices, strings */
  const int ordered = h->code_offset >= 28u && h->code_offset < h->imm_offset && h->imm_offset <= h->func_offset &&
                      (uint64_t)h->func_offset + 8u * (uint64_t)h->func_count == h->roots_offset && h->roots_offset <= h->vars_offset &&
                      h->vars_offset <= h->strings_offset && h->strings_offset <= size;
  if (!ordered || (h->func_offset - h->imm_offset) % 4u || (h->strings_offset - h->vars_offset) % 2u) {
    ct_assets_fail(error, error_size, "%s: the header offsets do not describe the streams (%lu, %lu)", "pac", (unsigned long)h->code_offset,
                   (unsigned long)h->strings_offset);
    return 0;
  }
  h->code_bytes = h->imm_offset - h->code_offset;
  h->imm_words = (h->func_offset - h->imm_offset) / 4u;
  h->var_refs = (h->strings_offset - h->vars_offset) / 2u;
  h->string_bytes = (uint32_t)size - h->strings_offset;
  return 1;
}

CT_ASSETS_API int ct_v2m_parse_header(const void *data, size_t size, ct_v2m_header *h, char *error, size_t error_size)
{
  const uint8_t *d = (const uint8_t *)data;
  if (size < 12u) {
    ct_assets_fail(error, error_size, "%s: %lu bytes: shorter than the 12-byte header", "v2m", (unsigned long)size, 0);
    return 0;
  }
  h->timediv = ct_assets_le32(d);
  h->maxtime = ct_assets_le32(d + 4);
  h->globals = ct_assets_le32(d + 8);
  if (!h->timediv || h->timediv > 0xffffu || !h->maxtime || 12u + 10u * (uint64_t)h->globals > size) {
    ct_assets_fail(error, error_size, "%s: implausible header (timediv %lu, maxtime %lu)", "v2m", (unsigned long)h->timediv, (unsigned long)h->maxtime);
    return 0;
  }
  return 1;
}

CT_ASSETS_API int ct_mesh_check_trailer(const void *data, size_t size, char *error, size_t error_size)
{
  const uint8_t *d = (const uint8_t *)data;
  if (size < 3u || memcmp(d + size - 3u, "ryg", 3)) {
    ct_assets_fail(error, error_size, "%s: the file does not end with the tag 'ryg'", "mesh", 0, 0);
    return 0;
  }
  return 1;
}

CT_ASSETS_API int ct_asset_check(ct_asset_id id, const void *data, size_t size, char *error, size_t error_size)
{
  const ct_asset_info *info = ct_asset_describe(id);
  if (!info) {
    ct_assets_fail(error, error_size, "%s: unknown asset %lu", "assets", (unsigned long)id, 0);
    return 0;
  }
  if (size != info->size) {
    ct_assets_fail(error, error_size, "%s: %lu bytes, expected %lu", info->file, (unsigned long)size, (unsigned long)info->size);
    return 0;
  }
  char hex[65];
  ct_assets_sha256_hex(data, size, hex);
  if (strcmp(hex, info->sha256)) {
    if (error_size) snprintf(error, error_size, "%s: SHA-256 %s is not the production's %s", info->file, hex, info->sha256);
    return 0;
  }
  ct_pac_header pac;
  ct_v2m_header v2m;
  switch (id) {
  case CT_ASSET_SCRIPT: return ct_pac_parse_header(data, size, &pac, error, error_size);
  case CT_ASSET_SONG: return ct_v2m_parse_header(data, size, &v2m, error, error_size);
  case CT_ASSET_MESH: return ct_mesh_check_trailer(data, size, error, error_size);
  default: return 0;
  }
}

CT_ASSETS_API void ct_assets_free(ct_assets *a)
{
  for (int i = 0; i < CT_ASSET_COUNT; i++) {
    free(a->data[i]);
    a->data[i] = NULL;
    a->size[i] = 0;
  }
}

CT_ASSETS_API int ct_assets_load(ct_assets *a, const char *dir, char *error, size_t error_size)
{
  memset(a, 0, sizeof *a);
  for (int i = 0; i < CT_ASSET_COUNT; i++) {
    const ct_asset_info *info = &ct_asset_table[i];
    char path[1024];
    if (snprintf(path, sizeof path, "%s/%s", dir, info->file) >= (int)sizeof path) {
      ct_assets_fail(error, error_size, "%s: the path is too long", info->file, 0, 0);
      goto fail;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
      ct_assets_fail(error, error_size, "cannot read %.400s", path, 0, 0);
      goto fail;
    }
    /* one byte more than expected: a longer file is read in full and then rejected for its size */
    uint8_t *buffer = (uint8_t *)malloc(info->size + 1u);
    const size_t n = buffer ? fread(buffer, 1, info->size + 1u, f) : 0;
    fclose(f);
    if (!buffer) {
      ct_assets_fail(error, error_size, "out of memory", NULL, 0, 0);
      goto fail;
    }
    a->data[i] = buffer;
    a->size[i] = n;
    if (!ct_asset_check((ct_asset_id)i, buffer, n, error, error_size)) goto fail;
  }
  return 1;
fail:
  ct_assets_free(a);
  return 0;
}

#endif /* CT_ASSETS_IMPLEMENTATION_DONE */
#endif /* CT_ASSETS_IMPLEMENTATION */
