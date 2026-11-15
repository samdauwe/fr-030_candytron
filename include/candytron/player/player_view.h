/* player_view.h: where the picture goes in the window, SDL-free so that it can be tested headless.
 *
 *   #define PLAYER_VIEW_IMPLEMENTATION in exactly one source file.
 *
 * The production is 4:3 (the executable draws 800x600). In a window of another shape the picture keeps its shape and is centred: bars
 * at the sides or at the top and the bottom.
 */
#ifndef PLAYER_VIEW_H
#define PLAYER_VIEW_H

#ifndef PLAYER_VIEW_API
#ifdef PLAYER_VIEW_STATIC
#define PLAYER_VIEW_API static
#else
#define PLAYER_VIEW_API extern
#endif
#endif

#define PVIEW_ASPECT (4.0 / 3.0) /* the production's width over height */

typedef struct pview {
  int x, y, width, height; /* in pixels, origin at the bottom left like glViewport */
} pview;

/* The largest rectangle of shape `aspect` (width over height; <= 0: PVIEW_ASPECT) that fits a window of window_w x window_h pixels, centred.
   A window of no area gives an empty rectangle. */
PLAYER_VIEW_API pview pview_fit(int window_w, int window_h, double aspect);

#endif /* PLAYER_VIEW_H */

#ifdef PLAYER_VIEW_IMPLEMENTATION
#ifndef PLAYER_VIEW_IMPLEMENTATION_DONE
#define PLAYER_VIEW_IMPLEMENTATION_DONE

PLAYER_VIEW_API pview pview_fit(int window_w, int window_h, double aspect)
{
  pview v = { 0, 0, 0, 0 };
  if (window_w <= 0 || window_h <= 0) return v;
  if (!(aspect > 0)) aspect = PVIEW_ASPECT;
  v.width = window_w;
  v.height = (int)((double)window_w / aspect + 0.5);
  if (v.height > window_h) { /* too wide for the window: bars at the sides */
    v.height = window_h;
    v.width = (int)((double)window_h * aspect + 0.5);
    if (v.width > window_w) v.width = window_w;
  }
  v.x = (window_w - v.width) / 2;
  v.y = (window_h - v.height) / 2;
  return v;
}

#endif /* PLAYER_VIEW_IMPLEMENTATION_DONE */
#endif /* PLAYER_VIEW_IMPLEMENTATION */
