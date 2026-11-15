/* The coverage of the strings of Bitmap_Text (the original asks Windows GDI for "arial"): the 7 distinct calls of the intro, baked by tools/bake_text.py into
 * text_data.h. Use gt3_text_baked as the session's `text` provider (csl_session.h); a call that is not one of the baked ones is left blank (returns 0).
 *
 * In ONE .c file define GT3_TEXT_IMPLEMENTATION before including this header; define GT3_TEXT_STATIC to make the functions static (the tests do). */
#ifndef GT3_TEXT_H_INCLUDED
#define GT3_TEXT_H_INCLUDED

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GT3_TEXT_API
#ifdef GT3_TEXT_STATIC
#define GT3_TEXT_API static
#else
#define GT3_TEXT_API extern
#endif
#endif

/* the gt3_text_fn of csl_natives.h (`user` is unused) */
GT3_TEXT_API int gt3_text_baked(void *user, const char *text, int width, int height, int x, int y, int cw, int ch, uint8_t *coverage);

#ifdef __cplusplus
}
#endif

#endif /* GT3_TEXT_H_INCLUDED */

#ifdef GT3_TEXT_IMPLEMENTATION
#ifndef GT3_TEXT_IMPLEMENTATION_DONE
#define GT3_TEXT_IMPLEMENTATION_DONE

#include <string.h>

#include "text_data.h"

GT3_TEXT_API int gt3_text_baked(void *user, const char *text, int width, int height, int x, int y, int cw, int ch, uint8_t *coverage)
{
  (void)user;
  for (int i = 0; i < GT3_TEXT_ENTRIES; i++) {
    const gt3_text_entry *e = &gt3_text_entries[i];
    if (strcmp(e->text, text) || e->bitmap_w != width || e->bitmap_h != height || e->x != x || e->y != y || e->cw != cw || e->ch != ch) continue;
    memset(coverage, 0, (size_t)width * (size_t)height);
    const int64_t box = (int64_t)e->box_w * e->box_h;
    int64_t at = 0;
    for (uint32_t k = 0; k + 1 < e->rle_size && at < box; k += 2) {
      for (int n = 0; n < e->rle[k] && at < box; n++, at++) {
        const int px = e->box_x + (int)(at % e->box_w), py = e->box_y + (int)(at / e->box_w);
        if (px < width && py < height) coverage[(size_t)py * (size_t)width + (size_t)px] = e->rle[k + 1];
      }
    }
    return 1;
  }
  return 0;
}

#endif /* GT3_TEXT_IMPLEMENTATION_DONE */
#endif /* GT3_TEXT_IMPLEMENTATION */
