/* Checks for player_credits.h and the notices the build embeds (licenses_data.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/check.h"
#include "support/suite.h"

#define PLAYER_CREDITS_IMPLEMENTATION
#include <candytron/player/player_credits.h>
#include "licenses_data.h"

static char *capture(const plicense *licenses, size_t n, size_t *size)
{
  char path[1024];
  snprintf(path, sizeof path, "%s/credits.txt", CANDYTRON_TEST_WORK_DIR);
  FILE *f = fopen(path, "wb+");
  if (!f) return NULL;
  const int ok = pcredits_print(f, licenses, n);
  const long len = ftell(f);
  rewind(f);
  char *text = (char *)malloc((size_t)len + 1);
  if (!ok || !text || fread(text, 1, (size_t)len, f) != (size_t)len) {
    free(text);
    text = NULL;
  } else {
    text[len] = 0;
    *size = (size_t)len;
  }
  fclose(f);
  remove(path);
  return text;
}

int suite_player_credits(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  CHECK(strstr(pcredits_title(), "candytron") && strstr(pcredits_title(), "030"), "title: %s", pcredits_title());
  CHECK(strstr(pcredits_subtitle(), "farbrausch") && strstr(pcredits_subtitle(), "breakpoint 2003"), "subtitle: %s", pcredits_subtitle());
  /* all four of the readme's credits lines */
  static const char *const people[] = { "chaos", "ryg", "gizmo", "kb" };
  for (size_t i = 0; i < sizeof people / sizeof people[0]; i++) CHECK(strstr(pcredits_roles(), people[i]) != NULL, "%s is credited", people[i]);

  /* what the build embeds: the five notices of the sources the player is built from */
  static const struct {
    const char *title, *text;
    size_t min_length;
  } notices[] = {
    { "GenThree engine", "Redistribution and use in source and binary forms", 500 },
    { "Candytron production data", "Creative Commons Attribution 3.0", 500 },
    { "Liberation Sans", "SIL OPEN FONT LICENSE Version 1.1", 2000 },
    { "V2 synthesizer", "Creative Commons CC0 1.0 Universal", 500 },
    { "SDL3", "zlib", 100 },
  };
  CHECK(player_n_licenses == sizeof notices / sizeof notices[0], "%lu embedded notices", (unsigned long)player_n_licenses);
  for (size_t i = 0; i < sizeof notices / sizeof notices[0] && i < player_n_licenses; i++) {
    CHECK(strstr(player_licenses[i].title, notices[i].title) != NULL, "notice %lu is '%s'", (unsigned long)i, player_licenses[i].title);
    CHECK(strlen(player_licenses[i].text) >= notices[i].min_length && strstr(player_licenses[i].text, notices[i].text), "notice %lu has its text",
          (unsigned long)i);
  }
  CHECK(strstr(player_licenses[1].text, "Breakpoint 2003") && strstr(player_licenses[1].text, "default.pac"),
        "the data notice names the production and the files");
  CHECK(!strstr(player_licenses[1].text, "ShareAlike"), "the production data is CC BY, not share-alike");

  /* the printed text: credits, the production's licence, one ruled heading per notice */
  size_t size = 0;
  char *text = capture(player_licenses, player_n_licenses, &size);
  CHECK(text != NULL, "cannot print the credits");
  if (text) {
    CHECK(strstr(text, "fr-030  candytron") && strstr(text, "CC BY 3.0"), "the printed text starts with the credits and names the licence");
    for (size_t i = 0; i < player_n_licenses; i++) CHECK(strstr(text, player_licenses[i].title) != NULL, "the printed text has '%s'", player_licenses[i].title);
    free(text);
  }
  const plicense one = { "a title", "no newline at the end" };
  text = capture(&one, 1, &size);
  CHECK(text && text[size - 1] == '\n', "a notice without a final newline gets one");
  free(text);

  printf("player_credits: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
