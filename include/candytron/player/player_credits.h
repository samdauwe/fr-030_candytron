/* player_credits.h: the credits of the production and the notices that come with the player, SDL-free.
 *
 *   #define PLAYER_CREDITS_IMPLEMENTATION in exactly one source file.
 *
 * The credits are those of the production's own readme (section 2 of its final release). The licence texts are embedded in the player by the
 * build (EmbedLicenses.cmake): the BSD notice of the GenThree sources, the Creative Commons Attribution 3.0 notice of the production data, the
 * Liberation Sans notice (the baked text), the V2 synthesizer's CC0 notice and the SDL3 notice. `candytron --licenses` prints all of it.
 */
#ifndef PLAYER_CREDITS_H
#define PLAYER_CREDITS_H

#include <stddef.h>
#include <stdio.h>

#ifndef PLAYER_CREDITS_API
#ifdef PLAYER_CREDITS_STATIC
#define PLAYER_CREDITS_API static
#else
#define PLAYER_CREDITS_API extern
#endif
#endif

typedef struct plicense {
  const char *title; /* what the notice covers */
  const char *text;  /* the notice itself */
} plicense;

PLAYER_CREDITS_API const char *pcredits_title(void);    /* "fr-030  candytron" */
PLAYER_CREDITS_API const char *pcredits_subtitle(void); /* the group and the party */
PLAYER_CREDITS_API const char *pcredits_roles(void);    /* the credits, one line per two or three roles */
/* The complete text of `candytron --licenses`: title, credits, then every licence under a ruled heading. Returns 0 if a write failed. */
PLAYER_CREDITS_API int pcredits_print(FILE *out, const plicense *licenses, size_t n_licenses);

#endif /* PLAYER_CREDITS_H */

#ifdef PLAYER_CREDITS_IMPLEMENTATION
#ifndef PLAYER_CREDITS_IMPLEMENTATION_DONE
#define PLAYER_CREDITS_IMPLEMENTATION_DONE

#include <string.h>

PLAYER_CREDITS_API const char *pcredits_title(void) { return "fr-030  candytron"; }
PLAYER_CREDITS_API const char *pcredits_subtitle(void) { return "farbrausch - chaos / ryg / gizmo / kb - breakpoint 2003"; }
PLAYER_CREDITS_API const char *pcredits_roles(void)
{
  return "chaos code: base system, 3d engine, texture generator - ryg code: mesh engine, postproc effects\n"
         "gizmo textures, models, animations, timing - kb music, sound code, voice synth";
}

PLAYER_CREDITS_API int pcredits_print(FILE *out, const plicense *licenses, size_t n_licenses)
{
  int ok = fprintf(out, "%s\n%s\n\n%s\n\n", pcredits_title(), pcredits_subtitle(), pcredits_roles()) > 0;
  ok = ok && fprintf(out, "Source-level C99 / SDL3 / OpenGL ES port of fr-030: candytron by Farbrausch (Breakpoint 2003).\n"
                          "The production data is released by Farbrausch under CC BY 3.0. Notices follow.\n") > 0;
  for (size_t i = 0; ok && i < n_licenses; i++) {
    const size_t n = strlen(licenses[i].text);
    ok = fprintf(out,
                 "\n================================================================================\n%s\n"
                 "================================================================================\n%s%s",
                 licenses[i].title, licenses[i].text, n && licenses[i].text[n - 1] != '\n' ? "\n" : "") > 0;
  }
  return ok && fflush(out) == 0;
}

#endif /* PLAYER_CREDITS_IMPLEMENTATION_DONE */
#endif /* PLAYER_CREDITS_IMPLEMENTATION */
