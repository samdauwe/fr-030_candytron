/* player_args.h: the command line of the candytron player, SDL-free so that it can be tested headless.
 *
 *   #define PLAYER_ARGS_IMPLEMENTATION in exactly one source file.
 *
 * poptions_parse() fills a poptions or explains in `error` what is wrong with the arguments (unknown option, missing or
 * malformed value); nothing is printed and nothing is exited here. The options are described at the top of src/player/main.c.
 */
#ifndef PLAYER_ARGS_H
#define PLAYER_ARGS_H

#include <stddef.h>

#ifndef PLAYER_ARGS_API
#ifdef PLAYER_ARGS_STATIC
#define PLAYER_ARGS_API static
#else
#define PLAYER_ARGS_API extern
#endif
#endif

typedef struct poptions {
  const char *assets_dir; /* --assets: where default.pac, josie.v2m and babe.mesh.old are; NULL: next to the executable, then ./assets */
  int width, height;      /* --size: the window's size in screen coordinates */
  double aspect;          /* --aspect W:H or a number: the presentation's width over height; 0: the production's (4:3) */
  int fullscreen, vsync;
  double exit_after;      /* < 0: never; seconds after the process started */
  int gl_check;           /* --gl-check: poll glGetError after every frame and count the GL objects; errors are failures */
  int verify_assets;      /* --verify-assets: check the data (size, SHA-256, structure), print the result and exit; no window */
  int version;            /* --version: print the name and version and exit */
  int licences;           /* --licenses: print the credits and licences and exit */
  const char *
    capture_dir; /* --capture DIR: make the pictures at --times, write them as DIR/frame_<NNN>.ppm (numbered from 001) with their SHA-256 on stdout, and exit; no clock, no vsync */
  double times[64]; /* --times T,T,...: song times in seconds for --capture (default: 0, 5, ... 195) */
  int n_times;
  double start;     /* --start S: the song time the live run begins at, seconds (0 ... 195) */
  double soak_rate, /* --soak-rate R: pictures per second of song time of --soak (default 30; the browser's software GL needs fewer) */
    soak; /* --soak S: render S seconds of the demo at 30 pictures a second, as fast as possible, the song's time wrapping, and report the memory; 0: off */
  int selftest; /* --selftest: render the renderer's unit scenes (one per feature) and compare them with the Direct3D formulas; exit */
} poptions;

/* Returns 1 on success; 0 with a message in `error` (error_size > 0). argv[0] is skipped; the strings are not copied. */
PLAYER_ARGS_API int poptions_parse(int argc, char **argv, poptions *o, char *error, size_t error_size);

#endif /* PLAYER_ARGS_H */

#ifdef PLAYER_ARGS_IMPLEMENTATION
#ifndef PLAYER_ARGS_IMPLEMENTATION_DONE
#define PLAYER_ARGS_IMPLEMENTATION_DONE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int poptions_number(const char *text, double *value)
{
  char *end;
  *value = strtod(text, &end);
  return *text && !*end && *value == *value;
}

PLAYER_ARGS_API int poptions_parse(int argc, char **argv, poptions *o, char *error, size_t error_size)
{
#define PARSE_FAIL(...)                                       \
  do {                                                        \
    if (error_size) snprintf(error, error_size, __VA_ARGS__); \
    return 0;                                                 \
  } while (0)
  memset(o, 0, sizeof *o);
  o->soak_rate = 30;
  o->width = 800;
  o->height = 600;
  o->vsync = 1;
  o->exit_after = -1;
  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    static const char *const with_value[] = { "--assets", "--size", "--aspect", "--exit-after", "--capture", "--times", "--start", "--soak", "--soak-rate" };
    int takes_value = 0;
    for (size_t k = 0; k < sizeof with_value / sizeof with_value[0]; k++) takes_value |= !strcmp(arg, with_value[k]);
    if (takes_value && i + 1 >= argc) PARSE_FAIL("%s needs a value", arg);
    const char *value = takes_value ? argv[++i] : NULL;
    double d;
    if (!strcmp(arg, "--assets"))
      o->assets_dir = value;
    else if (!strcmp(arg, "--size")) {
      char tail;
      if (sscanf(value, "%dx%d%c", &o->width, &o->height, &tail) != 2 || o->width < 64 || o->height < 64 || o->width > 16384 || o->height > 16384)
        PARSE_FAIL("--size wants WIDTHxHEIGHT between 64x64 and 16384x16384, got '%s'", value);
    } else if (!strcmp(arg, "--aspect")) {
      double w, h = 1;
      char tail;
      const int n = sscanf(value, "%lf:%lf%c", &w, &h, &tail);
      if ((n != 2 && !(n == 1 && sscanf(value, "%lf%c", &w, &tail) == 1)) || !(w > 0) || !(h > 0) || w / h < 0.2 || w / h > 5)
        PARSE_FAIL("--aspect wants W:H or a width over height between 0.2 and 5, got '%s'", value);
      o->aspect = w / h;
    } else if (!strcmp(arg, "--exit-after")) {
      if (!poptions_number(value, &d) || d < 0) PARSE_FAIL("--exit-after wants seconds, got '%s'", value);
      o->exit_after = d;
    } else if (!strcmp(arg, "--capture"))
      o->capture_dir = value;
    else if (!strcmp(arg, "--times")) {
      char *end;
      const char *p = value;
      o->n_times = 0;
      while (*p) {
        const double t = strtod(p, &end);
        if (end == p || !(t >= 0) || t > 1000.0 || o->n_times >= 64)
          PARSE_FAIL("--times wants up to 64 song times in seconds, separated by commas, got '%s'", value);
        o->times[o->n_times++] = t;
        p = end;
        if (*p == ',')
          p++;
        else if (*p)
          PARSE_FAIL("--times wants up to 64 song times in seconds, separated by commas, got '%s'", value);
      }
      if (!o->n_times) PARSE_FAIL("--times wants up to 64 song times in seconds, separated by commas, got '%s'", value);
    } else if (!strcmp(arg, "--start")) {
      if (!poptions_number(value, &d) || d < 0 || d > 195) PARSE_FAIL("--start wants a song time between 0 and 195 seconds, got '%s'", value);
      o->start = d;
    } else if (!strcmp(arg, "--soak-rate")) {
      if (!poptions_number(value, &d) || d < 1 || d > 240)
        PARSE_FAIL("--soak-rate wants a number of pictures per song second between 1 and 240, got '%s'", value);
      o->soak_rate = d;
    } else if (!strcmp(arg, "--soak")) {
      if (!poptions_number(value, &d) || !(d > 0) || d > 1e7) PARSE_FAIL("--soak wants a number of song seconds above 0, got '%s'", value);
      o->soak = d;
    } else if (!strcmp(arg, "--selftest"))
      o->selftest = 1;
    else if (!strcmp(arg, "--fullscreen"))
      o->fullscreen = 1;
    else if (!strcmp(arg, "--no-vsync"))
      o->vsync = 0;
    else if (!strcmp(arg, "--gl-check"))
      o->gl_check = 1;
    else if (!strcmp(arg, "--verify-assets"))
      o->verify_assets = 1;
    else if (!strcmp(arg, "--version"))
      o->version = 1;
    else if (!strcmp(arg, "--licenses") || !strcmp(arg, "--licences"))
      o->licences = 1;
    else
      PARSE_FAIL("unknown option '%s'", arg);
  }
  return 1;
#undef PARSE_FAIL
}

#endif /* PLAYER_ARGS_IMPLEMENTATION_DONE */
#endif /* PLAYER_ARGS_IMPLEMENTATION */
