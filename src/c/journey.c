/*
 * journey.c — phase-1 procedural journey strip.
 *
 * Phase 4 will replace the camp / chest / boss / token primitives with
 * proper sprite art and add a class silhouette inside the token.
 */

#include "journey.h"

#define COLOR_TRAIL    PBL_IF_COLOR_ELSE(GColorWindsorTan, GColorDarkGray)
#define COLOR_INK      GColorBlack
#define COLOR_TENT     PBL_IF_COLOR_ELSE(GColorBulgarianRose, GColorDarkGray)
#define COLOR_TREASURE PBL_IF_COLOR_ELSE(GColorChromeYellow,  GColorWhite)
#define COLOR_BOSS     PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorDarkGray)
#define COLOR_TOKEN    PBL_IF_COLOR_ELSE(GColorOxfordBlue,   GColorBlack)
#define COLOR_FLAME    PBL_IF_COLOR_ELSE(GColorOrange,       GColorWhite)

static void draw_camp(GContext *ctx, GPoint at, bool sleeping) {
  GPoint tent[3] = {
    { at.x - 6, at.y + 4 },
    { at.x,     at.y - 7 },
    { at.x + 6, at.y + 4 },
  };
  GPathInfo info = { 3, tent };
  GPath *p = gpath_create(&info);
  graphics_context_set_fill_color(ctx, COLOR_TENT);
  gpath_draw_filled(ctx, p);
  graphics_context_set_stroke_color(ctx, COLOR_INK);
  gpath_draw_outline(ctx, p);
  gpath_destroy(p);

  if (sleeping) {
    /* Tiny lit flame next to the tent. */
    graphics_context_set_fill_color(ctx, COLOR_FLAME);
    graphics_fill_circle(ctx, GPoint(at.x + 9, at.y + 1), 2);
  }
}

static void draw_chest(GContext *ctx, GPoint at) {
  GRect body = GRect(at.x - 5, at.y - 3, 10, 8);
  graphics_context_set_fill_color(ctx, COLOR_TREASURE);
  graphics_fill_rect(ctx, body, 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_INK);
  graphics_draw_rect(ctx, body);
  /* Lid line. */
  graphics_draw_line(ctx,
    GPoint(at.x - 5, at.y),
    GPoint(at.x + 5, at.y));
}

static void draw_boss(GContext *ctx, GPoint at) {
  GPoint tri[3] = {
    { at.x,     at.y - 7 },
    { at.x + 6, at.y + 4 },
    { at.x - 6, at.y + 4 },
  };
  GPathInfo info = { 3, tri };
  GPath *p = gpath_create(&info);
  graphics_context_set_fill_color(ctx, COLOR_BOSS);
  gpath_draw_filled(ctx, p);
  graphics_context_set_stroke_color(ctx, COLOR_INK);
  gpath_draw_outline(ctx, p);
  gpath_destroy(p);
}

static void draw_token(GContext *ctx, GPoint at, bool sleeping) {
  graphics_context_set_fill_color(ctx, COLOR_TOKEN);
  graphics_fill_circle(ctx, at, 5);
  graphics_context_set_stroke_color(ctx, COLOR_INK);
  graphics_draw_circle(ctx, at, 5);

  if (sleeping) {
    graphics_context_set_text_color(ctx, COLOR_INK);
    graphics_draw_text(ctx, "Z",
      fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
      GRect(at.x + 5, at.y - 16, 10, 14),
      GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  }
}

void journey_draw(GContext *ctx, GRect bounds,
                  int32_t steps, int32_t goal, bool sleeping) {
  const int16_t y    = bounds.size.h / 2;
  const int16_t x0   = 16;
  const int16_t x1   = bounds.size.w - 16;
  const int16_t span = x1 - x0;

  /* Dashed trail. Halved alpha is not available on color e-paper, so
   * during sleep we just skip more of the dashes for a "muted" look. */
  graphics_context_set_stroke_color(ctx, COLOR_TRAIL);
  graphics_context_set_stroke_width(ctx, 2);
  const int16_t period = sleeping ? 10 : 6;
  const int16_t seglen = 3;
  for (int16_t x = x0; x < x1; x += period) {
    int16_t seg_end = (x + seglen < x1) ? (x + seglen) : x1;
    graphics_draw_line(ctx, GPoint(x, y), GPoint(seg_end, y));
  }

  draw_camp (ctx, GPoint(x0,            y), sleeping);
  draw_chest(ctx, GPoint(x0 + span / 2, y));
  draw_boss (ctx, GPoint(x1,            y));

  /* Token position.
   *  - Sleep mode: always at camp (the adventurer is resting).
   *  - Steps unavailable / zero so far today: nudged a few px off-camp so
   *    the token doesn't look stuck on the trailhead before HealthService
   *    has reported anything.
   *  - Otherwise: position scales with steps / goal, clamped to the trail. */
  int16_t tx;
  if (sleeping) {
    tx = x0;
  } else if (steps <= 0 || goal <= 0) {
    tx = x0 + 4;
  } else {
    int32_t clamped = (steps < goal) ? steps : goal;
    tx = x0 + (int16_t)(span * clamped / goal);
  }
  draw_token(ctx, GPoint(tx, y), sleeping);
}
