/* Checks for player_view.h: the 4:3 picture in windows of every shape. */
#include <stdio.h>

#include "support/check.h"
#include "support/suite.h"

#define PLAYER_VIEW_IMPLEMENTATION
#include <candytron/player/player_view.h>

static int is(pview v, int x, int y, int w, int h) { return v.x == x && v.y == y && v.width == w && v.height == h; }

int suite_player_view(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  pview v;

  v = pview_fit(800, 600, 0);
  CHECK(is(v, 0, 0, 800, 600), "the production's own size fills the window: %d %d %d %d", v.x, v.y, v.width, v.height);
  v = pview_fit(1600, 600, 0);
  CHECK(is(v, 400, 0, 800, 600), "a wide window: bars at the sides: %d %d %d %d", v.x, v.y, v.width, v.height);
  v = pview_fit(800, 1200, 0);
  CHECK(is(v, 0, 300, 800, 600), "a tall window: bars at the top and the bottom: %d %d %d %d", v.x, v.y, v.width, v.height);
  v = pview_fit(1920, 1080, 0);
  CHECK(is(v, 240, 0, 1440, 1080), "full HD: %d %d %d %d", v.x, v.y, v.width, v.height);
  v = pview_fit(1920, 1080, 16.0 / 9.0);
  CHECK(is(v, 0, 0, 1920, 1080), "an aspect given: %d %d %d %d", v.x, v.y, v.width, v.height);
  v = pview_fit(1920, 1080, -1);
  CHECK(is(v, 240, 0, 1440, 1080), "a negative aspect means the production's");

  /* no window, no picture; a sliver of a window still gives something that fits */
  CHECK(is(pview_fit(0, 600, 0), 0, 0, 0, 0) && is(pview_fit(800, 0, 0), 0, 0, 0, 0) && is(pview_fit(-5, -5, 0), 0, 0, 0, 0), "empty windows");
  v = pview_fit(1, 1, 0);
  CHECK(v.width <= 1 && v.height <= 1 && v.x >= 0 && v.y >= 0, "a 1x1 window: %d %d %d %d", v.x, v.y, v.width, v.height);

  /* whatever the window, the rectangle lies inside it and keeps its shape within a pixel */
  for (int w = 1; w <= 211; w += 13)
    for (int h = 1; h <= 211; h += 11) {
      v = pview_fit(w, h, 0);
      CHECK(v.x >= 0 && v.y >= 0 && v.x + v.width <= w && v.y + v.height <= h && v.width > 0 && v.height > 0, "%dx%d: %d %d %d %d", w, h, v.x, v.y, v.width,
            v.height);
      if (w >= 40 && h >= 30) {
        const double shape = (double)v.width / (double)v.height;
        CHECK(shape > PVIEW_ASPECT - 0.1 && shape < PVIEW_ASPECT + 0.1, "%dx%d: shape %f", w, h, shape);
      }
    }

  printf("player_view: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
