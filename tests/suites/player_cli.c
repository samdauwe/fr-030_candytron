/* The player executable, started headless (SDL offscreen video driver) and judged by its exit status and report.
 * Which case to run is argv[1]; the cases are the table below. A case is skipped when the machine has no OpenGL ES context. */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "support/suite.h"

#if !defined(_WIN32) && defined(CANDYTRON_PLAYER_EXE)

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define WORK   CANDYTRON_TEST_WORK_DIR
#define ASSETS CANDYTRON_ASSETS_DIR

typedef struct cli_case {
  const char *name;
  const char *args;            /* the command line after the executable */
  int must_fail;               /* the player must exit with a non-zero status */
  int needs_gl;                /* an OpenGL ES context is created: skip when that is impossible */
  const char *const *contains; /* fragments of the output, in this order; NULL-terminated */
  int (*verify)(const char *output);
} cli_case;

static const char *const k_licenses[] = { "fr-030  candytron", "chaos", "GenThree engine", "Creative Commons Attribution 3.0", "SDL3, zlib license", NULL };
static const char *const k_version[] = { "candytron 0.1.0 (fr-030: Candytron)", NULL };
static const char *const k_bad_arg[] = { "candytron: unknown option '--time'", "usage:", NULL };
static const char *const k_verify[] = { "assets: default.pac",
                                        "74281 bytes",
                                        "assets: josie.v2m",
                                        "39470 bytes",
                                        "assets: babe.mesh.old",
                                        "11244 bytes",
                                        "assets: script: 23708 code bytes, 10283 immediates, 58 functions, 4348 variable references, 253 string bytes",
                                        "assets: song: 480 ticks per beat, 206898 ticks, 1 global events",
                                        "assets: ok",
                                        NULL };
static const char *const k_missing[] = { "candytron: the production data: cannot read ", "does-not-exist/default.pac", NULL };
static const char *const k_tampered[] = { "candytron: the production data: josie.v2m: SHA-256 ", " is not the production's 5bf17fb8", NULL };
static const char *const k_gl_clean[] = { "gl-check: 0 GL errors in ", " polls (", NULL };
static const char *const k_selftest[] = { "selftest: flat quad",       "selftest: render target as a texture",
                                          " checks, 0 failed",         "selftest: ok",
                                          "gl-check: 0 GL errors in ", NULL };
static const char *const k_audio[] = { "audio: ", " frames rendered, the last picture at song time ", "gl-check: 0 GL errors in ", NULL };
static const char *const k_start[] = { "audio: ", "perf: ", "memory: heap in use ", "gl-check: 0 GL errors in ", NULL };
static const char *const k_soak[] = { "soak: 2 s of song in ",     "soak: shorter than two loops of the song",
                                      "perf: 60 pictures, ",       "memory: heap in use ",
                                      "gl-check: 0 GL errors in ", NULL };
static const char *const k_golden[] = { "capture: 1 t=10 800x600 sha256=", "capture: 18 t=180 800x600 sha256=", NULL };
static const char *const k_capture[] = { "capture: 1 t=20 800x600 sha256=", "capture: 2 t=60 800x600 sha256=", "gl-check: 0 GL errors in ", NULL };

/* the player must not have opened a window for a command that is answered from the data alone */
static int verify_no_window(const char *output)
{
  return !strstr(output, "candytron: ");
} /* the prefix of every message the player prints about a window, SDL or a failure */

static char *run(const char *command, int *exit_status);

#define CAPTURE_ARGS "--assets \"" ASSETS "\" --capture \"" WORK "/cli_capture\" --times 20,60 --gl-check --no-vsync --size 320x240"

/* the digests of a capture's lines */
static int capture_digests(const char *output, char digests[2][65])
{
  const char *p = output;
  for (int i = 0; i < 2; i++) {
    p = strstr(p, "sha256=");
    if (!p) return 0;
    memcpy(digests[i], p + 7, 64);
    digests[i][64] = 0;
    p += 7;
  }
  return 1;
}

/* the mean of the pixels of a PPM written by --capture (the header is 15 bytes: P6, 800 600, 255) */
static double ppm_mean(const char *path)
{
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  unsigned char buf[4096];
  double sum = 0, n = 0;
  size_t got, skip = 15;
  while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
    for (size_t i = skip < got ? skip : 0; i < got; i++) sum += buf[i], n += 1;
    skip = 0;
  }
  fclose(f);
  return n > 0 ? sum / n : -1;
}

/* one second of the demo with the dummy audio driver (it plays in real time): the song is rendered at about the device's pace and the picture is
   made for the song time the device has reached */
static int verify_audio(const char *output)
{
  const char *p = strstr(output, "audio: ");
  unsigned long long frames = 0;
  double song = -1;
  if (!p || sscanf(p, "audio: %llu frames rendered, the last picture at song time %lf s", &frames, &song) != 2) return 0;
  if (frames < 30000 || frames > 100000) {
    printf("%llu frames rendered in about a second of playing\n", frames);
    return 0;
  }
  if (song < 0.5 || song > 1.3) {
    printf("the last picture is at song time %.2f s after a second of playing\n", song);
    return 0;
  }
  return 1;
}

/* --start 100: the song and the picture begin there (a seek, not a wait): after a second the last picture is made for about 101 s */
static int verify_start(const char *output)
{
  const char *p = strstr(output, "audio: ");
  unsigned long long frames = 0;
  double song = -1;
  if (!p || sscanf(p, "audio: %llu frames rendered, the last picture at song time %lf s", &frames, &song) != 2) return 0;
  if (frames < 100ull * 44100 + 30000 || frames > 103ull * 44100 || song < 100.5 || song > 101.5) {
    printf("%llu frames, the last picture at %.2f s: not a second after 100 s\n", frames, song);
    return 0;
  }
  return 1;
}

/* the pictures are not blank, differ from each other, and a second run makes the same ones bit for bit (the script's time, not the clock, decides) */
static int verify_capture(const char *output)
{
  char first[2][65], second[2][65];
  if (!capture_digests(output, first)) return 0;
  if (!strcmp(first[0], first[1])) {
    printf("the pictures at 20 s and 60 s are the same\n");
    return 0;
  }
  const double m1 = ppm_mean(WORK "/cli_capture/frame_001.ppm"), m2 = ppm_mean(WORK "/cli_capture/frame_002.ppm");
  if (m1 < 8 || m2 < 8) {
    printf("a picture is (nearly) black: mean %.2f and %.2f\n", m1, m2);
    return 0;
  }
  char command[2048];
  int status = 0;
  snprintf(command, sizeof command, "\"%s\" %s 2>&1", CANDYTRON_PLAYER_EXE, CAPTURE_ARGS);
  char *again = run(command, &status);
  if (!again) return 0;
  const int same = status == 0 && capture_digests(again, second) && !strcmp(first[0], second[0]) && !strcmp(first[1], second[1]);
  if (!same) printf("a second run does not make the same pictures\n");
  free(again);
  return same;
}

/* the golden frames: the original's pictures (a recording of the original executable, reduced to 100 x 75) at 18 song times and an RMSE limit for each
   (tests/data/candytron/golden). The port's picture is reduced the same way (8 x 8 means) and must be within the limit: a regression bound against the original,
   not a claim of fidelity (the limits are wide where the port is known to differ, see doc/REFERENCE.md) */
#define GOLDEN_DIR CANDYTRON_TEST_DATA_DIR "/candytron/golden"
#define GOLDEN_ARGS \
  "--assets \"" ASSETS "\" --capture \"" WORK "/cli_golden\" --times 10,20,30,40,50,60,70,80,90,100,110,120,130,140,150,160,170,180 --no-vsync"

/* a binary PPM (P6, width, height, 255) as bytes; NULL when it is not one */
static unsigned char *read_ppm(const char *path, int *w, int *h)
{
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  int maxv = 0;
  unsigned char *pixels = NULL;
  if (fscanf(f, "P6 %d %d %d", w, h, &maxv) == 3 && maxv == 255 && fgetc(f) != EOF && *w > 0 && *h > 0) {
    const size_t n = (size_t)*w * (size_t)*h * 3u;
    pixels = (unsigned char *)malloc(n);
    if (pixels && fread(pixels, 1, n, f) != n) {
      free(pixels);
      pixels = NULL;
    }
  }
  fclose(f);
  return pixels;
}

static int verify_golden(const char *output)
{
  (void)output;
  FILE *list = fopen(GOLDEN_DIR "/golden.txt", "r");
  if (!list) {
    printf("cannot read " GOLDEN_DIR "/golden.txt\n");
    return 0;
  }
  char line[256];
  int index = 0, bad = 0;
  double worst_margin = 1e9;
  while (fgets(line, sizeof line, list)) {
    double t, limit, made;
    if (line[0] == '#' || sscanf(line, "%lf %lf %lf", &t, &limit, &made) != 3) continue;
    index++;
    char mine_path[2048], gold_path[2048];
    snprintf(mine_path, sizeof mine_path, WORK "/cli_golden/frame_%03d.ppm", index);
    snprintf(gold_path, sizeof gold_path, GOLDEN_DIR "/frame_%03d.ppm", (int)(t + 0.5));
    int mw = 0, mh = 0, gw = 0, gh = 0;
    unsigned char *mine = read_ppm(mine_path, &mw, &mh), *gold = read_ppm(gold_path, &gw, &gh);
    if (!mine || !gold || mw != 800 || mh != 600 || gw != 100 || gh != 75) {
      printf("t=%g: cannot read the pictures (%s %dx%d, %s %dx%d)\n", t, mine_path, mw, mh, gold_path, gw, gh);
      bad++;
    } else {
      double sum = 0;
      for (int y = 0; y < 75; y++)
        for (int x = 0; x < 100; x++)
          for (int c = 0; c < 3; c++) {
            unsigned total = 0;
            for (int dy = 0; dy < 8; dy++)
              for (int dx = 0; dx < 8; dx++) total += mine[(((size_t)y * 8 + dy) * 800 + (size_t)x * 8 + dx) * 3 + c];
            const double d = total / 64.0 - gold[((size_t)y * 100 + x) * 3 + c];
            sum += d * d;
          }
      const double rmse = sqrt(sum / (75.0 * 100.0 * 3.0));
      if (rmse > limit) {
        printf("t=%g: RMSE %.2f against the original, limit %.2f (it was %.2f)\n", t, rmse, limit, made);
        bad++;
      }
      if (limit - rmse < worst_margin) worst_margin = limit - rmse;
    }
    free(mine);
    free(gold);
  }
  fclose(list);
  if (index != 18) {
    printf("%d times in golden.txt, 18 expected\n", index);
    return 0;
  }
  printf("golden: 18 pictures within their limits, the smallest margin %.2f levels\n", worst_margin);
  return bad == 0;
}

static const cli_case k_cases[] = {
  /* answered without a window */
  { "licenses", "--licenses", 0, 0, k_licenses, verify_no_window },
  { "version", "--version", 0, 0, k_version, NULL },
  { "bad_args", "--time 5", 1, 0, k_bad_arg, NULL },
  /* the data: the player verifies it before it does anything else */
  { "verify_assets", "--verify-assets --assets \"" ASSETS "\"", 0, 0, k_verify, verify_no_window },
  { "missing_assets", "--verify-assets --assets \"" WORK "/does-not-exist\"", 1, 0, k_missing, NULL },
  { "tampered_assets", "--verify-assets --assets \"" WORK "/cli_tampered\"", 1, 0, k_tampered, NULL },
  { "tampered_assets_live", "--exit-after 0.1 --assets \"" WORK "/cli_tampered\"", 1, 0, k_tampered, NULL },
  /* the window: one second of frames, GL errors polled after every one */
  { "window", "--assets \"" ASSETS "\" --gl-check --no-vsync --size 320x240 --exit-after 1", 0, 1, k_gl_clean, NULL },
  { "audio", "--assets \"" ASSETS "\" --gl-check --no-vsync --size 320x240 --exit-after 1", 0, 1, k_audio, verify_audio },
  { "start", "--assets \"" ASSETS "\" --gl-check --no-vsync --size 320x240 --start 100 --exit-after 1", 0, 1, k_start, verify_start },
  { "soak", "--assets \"" ASSETS "\" --gl-check --size 320x240 --soak 2", 0, 1, k_soak, NULL },
  { "selftest", "--selftest --gl-check --no-vsync --size 320x240", 0, 1, k_selftest, NULL },
  { "capture", CAPTURE_ARGS, 0, 1, k_capture, verify_capture },
  { "golden", GOLDEN_ARGS, 0, 1, k_golden, verify_golden },
};

static char *run(const char *command, int *exit_status)
{
  FILE *p = popen(command, "r");
  if (!p) return NULL;
  size_t size = 0, cap = 1 << 16;
  char *buf = (char *)malloc(cap);
  size_t n;
  while (buf && (n = fread(buf + size, 1, cap - size - 1, p)) > 0) {
    size += n;
    if (cap - size < 4096) {
      char *bigger = (char *)realloc(buf, cap * 2);
      if (!bigger) {
        free(buf);
        buf = NULL;
        break;
      }
      buf = bigger;
      cap *= 2;
    }
  }
  const int status = pclose(p);
  if (buf) buf[size] = 0;
  *exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return buf;
}

/* the production data with one byte of the song changed, for the cases that must refuse it */
static int make_tampered_assets(void)
{
  char dir[1024], to[2048], command[4096];
  snprintf(dir, sizeof dir, "%s/cli_tampered", WORK);
  mkdir(WORK, 0777);
  mkdir(dir, 0777);
  snprintf(command, sizeof command, "cp \"%s/default.pac\" \"%s/babe.mesh.old\" \"%s/josie.v2m\" \"%s\"", ASSETS, ASSETS, ASSETS, dir);
  if (system(command) != 0) return 0;
  snprintf(to, sizeof to, "%s/josie.v2m", dir);
  FILE *f = fopen(to, "r+b");
  if (!f) return 0;
  int ok = fseek(f, 1000, SEEK_SET) == 0;
  const int byte = ok ? fgetc(f) : EOF;
  ok = ok && byte != EOF && fseek(f, 1000, SEEK_SET) == 0 && fputc(byte ^ 0x80, f) != EOF;
  return fclose(f) == 0 && ok;
}

int suite_player_cli(int argc, char **argv)
{
  if (argc < 2) {
    printf("usage: player_cli <case>\n");
    return 2;
  }
  const cli_case *c = NULL;
  for (size_t i = 0; i < sizeof k_cases / sizeof k_cases[0]; i++)
    if (!strcmp(k_cases[i].name, argv[1])) c = &k_cases[i];
  if (!c) {
    printf("unknown case %s\n", argv[1]);
    return 2;
  }

  mkdir(WORK, 0777);
  if (strstr(c->name, "tampered") && !make_tampered_assets()) {
    printf("cannot make the tampered copy of the data\n");
    return 1;
  }
  setenv("SDL_VIDEODRIVER", "offscreen", 1);
  setenv("SDL_AUDIODRIVER", "dummy", 1);
#ifdef CANDYTRON_TEST_LSAN_SUPP
  setenv("LSAN_OPTIONS", "suppressions=" CANDYTRON_TEST_LSAN_SUPP, 1);
#endif

  char command[2048];
  snprintf(command, sizeof command, "\"%s\" %s 2>&1", CANDYTRON_PLAYER_EXE, c->args);
  int status = 0;
  char *output = run(command, &status);
  if (!output) {
    printf("cannot run %s\n", command);
    return 1;
  }

  int rc = 0;
  if (c->needs_gl && status != 0 && (strstr(output, "OpenGL context") || strstr(output, "window:"))) {
    printf("no OpenGL ES context: %s\n", output);
    rc = CANDYTRON_TEST_SKIP;
  } else {
    if (c->must_fail ? status == 0 : status != 0) {
      printf("exit status %d\n", status);
      rc = 1;
    }
    const char *p = output;
    for (size_t i = 0; rc == 0 && c->contains && c->contains[i]; i++) {
      const char *hit = strstr(p, c->contains[i]);
      if (!hit) {
        printf("the output lacks '%s'\n", c->contains[i]);
        rc = 1;
      } else
        p = hit + strlen(c->contains[i]);
    }
    if (rc == 0 && c->verify && !c->verify(output)) {
      printf("the output is not what is expected\n");
      rc = 1;
    }
  }
  if (rc == 1) printf("--- %s\n%s\n", command, output);
  free(output);
  return rc;
}

#else

int suite_player_cli(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  printf("the player executable is not available\n");
  return CANDYTRON_TEST_SKIP;
}

#endif
