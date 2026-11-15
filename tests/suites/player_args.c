/* Checks for player_args.h: the candytron command line. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/check.h"
#include "support/suite.h"

#define PLAYER_ARGS_IMPLEMENTATION
#include <candytron/player/player_args.h>

/* parse "candytron <words...>" (words separated by single spaces) */
static int parse(const char *words, poptions *o, char *error, size_t error_size)
{
  static char text[512];
  char *argv[32];
  int argc = 0;
  argv[argc++] = (char *)"candytron";
  snprintf(text, sizeof text, "%s", words);
  for (char *p = strtok(text, " "); p && argc < 31; p = strtok(NULL, " ")) argv[argc++] = p;
  argv[argc] = NULL;
  return poptions_parse(argc, argv, o, error, error_size);
}

static int rejects(const char *words, const char *fragment)
{
  poptions o;
  char error[256] = "";
  return !parse(words, &o, error, sizeof error) && strstr(error, fragment) != NULL;
}

int suite_player_args(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  poptions o;
  char error[256] = "";

  /* defaults: the executable's own 800x600 window, vsync, nothing else */
  CHECK(parse("", &o, error, sizeof error), "no arguments: %s", error);
  CHECK(o.width == 800 && o.height == 600 && o.vsync == 1 && o.aspect == 0 && o.exit_after < 0 && !o.fullscreen && !o.gl_check && !o.verify_assets &&
          !o.version && !o.licences && !o.assets_dir,
        "defaults");

  CHECK(parse("--assets /some/dir --size 1024x768 --aspect 16:9 --fullscreen --no-vsync --exit-after 2.5 --gl-check", &o, error, sizeof error),
        "all options: %s", error);
  CHECK(!strcmp(o.assets_dir, "/some/dir") && o.width == 1024 && o.height == 768 && o.fullscreen && !o.vsync && o.gl_check, "values");
  CHECK(o.aspect > 1.7777 && o.aspect < 1.7778 && o.exit_after == 2.5, "aspect %f, exit-after %f", o.aspect, o.exit_after);

  CHECK(parse("--aspect 1.5", &o, error, sizeof error) && o.aspect == 1.5, "an aspect as one number");
  CHECK(parse("--verify-assets", &o, error, sizeof error) && o.verify_assets, "--verify-assets");
  CHECK(parse("--version", &o, error, sizeof error) && o.version, "--version");
  CHECK(parse("--licenses", &o, error, sizeof error) && o.licences, "--licenses");
  CHECK(parse("--licences", &o, error, sizeof error) && o.licences, "--licences");
  CHECK(parse("--capture /tmp/x --times 0,5.5,195", &o, error, sizeof error) && !strcmp(o.capture_dir, "/tmp/x") && o.n_times == 3 && o.times[1] == 5.5 &&
          o.times[2] == 195,
        "--capture and --times");
  CHECK(parse("--selftest", &o, error, sizeof error) && o.selftest, "--selftest");
  CHECK(parse("--start 100.5 --soak 3600", &o, error, sizeof error) && o.start == 100.5 && o.soak == 3600, "--start and --soak");
  CHECK(parse("", &o, error, sizeof error) && o.start == 0 && o.soak == 0, "--start and --soak default to off");
  CHECK(rejects("--start 196", "--start wants"), "a start after the song");
  CHECK(rejects("--start -1", "--start wants"), "a negative start");
  CHECK(rejects("--soak 0", "--soak wants"), "a soak of nothing");
  CHECK(parse("--soak 400 --soak-rate 2", &o, error, sizeof error) && o.soak_rate == 2, "--soak-rate");
  CHECK(parse("", &o, error, sizeof error) && o.soak_rate == 30, "--soak-rate defaults to 30");
  CHECK(rejects("--soak-rate 0", "--soak-rate wants"), "a soak rate of nothing");
  CHECK(rejects("--soak-rate 1000", "--soak-rate wants"), "a soak rate beyond 240");
  CHECK(rejects("--soak long", "--soak wants"), "a soak that is no number");
  CHECK(rejects("--times 1,x", "--times wants"), "a time that is none");
  CHECK(rejects("--times 1,", "--times wants") || parse("--times 1,", &o, error, sizeof error), "a trailing comma is harmless or refused");
  CHECK(rejects("--times -3", "--times wants"), "a negative time");
  CHECK(rejects("--capture", "--capture needs a value"), "a missing directory");

  /* what is wrong is said, and nothing is exited here */
  CHECK(rejects("--bogus", "unknown option '--bogus'"), "unknown option");
  CHECK(rejects("--kx file", "unknown option '--kx'"), "the options of the other ports are not accepted");
  CHECK(rejects("--size", "--size needs a value"), "a missing value");
  CHECK(rejects("--assets", "--assets needs a value"), "a missing value");
  CHECK(rejects("--size 800", "--size wants"), "a size without height");
  CHECK(rejects("--size 800x600x1", "--size wants"), "a size with a tail");
  CHECK(rejects("--size 63x600", "--size wants"), "a size below 64");
  CHECK(rejects("--size 20000x600", "--size wants"), "a size above 16384");
  CHECK(rejects("--aspect 0:3", "--aspect wants"), "an aspect of zero");
  CHECK(rejects("--aspect 10:1", "--aspect wants"), "an aspect above 5");
  CHECK(rejects("--aspect wide", "--aspect wants"), "an aspect that is no number");
  CHECK(rejects("--exit-after soon", "--exit-after wants seconds, got 'soon'"), "exit-after that is no number");
  CHECK(rejects("--exit-after -1", "--exit-after wants"), "a negative exit-after");

  /* the error buffer may be empty */
  CHECK(!parse("--bogus", &o, NULL, 0), "an error without a buffer");

  printf("player_args: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
