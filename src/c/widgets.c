/*
 * widgets.c — phase-1 procedural rendering of the ribbon and stat row.
 *
 * The heart is built from two filled circles and a triangle. The torch is a
 * scaled flame triangle over a small handle rect. Phase 4 will replace the
 * familiar placeholder with selectable creature art.
 */

#include "widgets.h"

#define COLOR_RIBBON   PBL_IF_COLOR_ELSE(GColorRajah,            GColorWhite)
#define COLOR_INK      GColorBlack
#define COLOR_HEART    PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorDarkGray)
#define COLOR_FLAME    PBL_IF_COLOR_ELSE(GColorOrange,           GColorWhite)
#define COLOR_HANDLE   PBL_IF_COLOR_ELSE(GColorWindsorTan,       GColorDarkGray)

void widgets_draw_ribbon(GContext *ctx, GRect b,
                         int16_t doy, const char *date,
                         bool bt) {
  graphics_context_set_fill_color(ctx, COLOR_RIBBON);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_INK);
  graphics_draw_rect(ctx, b);

  char buf[24];
  snprintf(buf, sizeof(buf), "Day %d  %s", doy, date);
  graphics_context_set_text_color(ctx, COLOR_INK);
  graphics_draw_text(ctx, buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(b.origin.x, b.origin.y, b.size.w - 18, b.size.h),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  if (bt) {
    /* Familiar placeholder — a filled dot on the right side of the ribbon.
     * Phase 4 swaps to a chosen creature silhouette from settings. */
    graphics_context_set_fill_color(ctx, COLOR_INK);
    graphics_fill_circle(ctx,
      GPoint(b.origin.x + b.size.w - 12,
             b.origin.y + b.size.h / 2),
      4);
  }
}

static void draw_heart(GContext *ctx, GPoint c) {
  graphics_context_set_fill_color(ctx, COLOR_HEART);
  graphics_fill_circle(ctx, GPoint(c.x - 4, c.y - 2), 5);
  graphics_fill_circle(ctx, GPoint(c.x + 4, c.y - 2), 5);
  GPoint tri[3] = {
    { c.x - 8, c.y },
    { c.x + 8, c.y },
    { c.x,     c.y + 9 },
  };
  GPathInfo info = { 3, tri };
  GPath *p = gpath_create(&info);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

static void draw_torch(GContext *ctx, GPoint base, uint8_t pct) {
  /* Flame height scales with battery: full / half / embers / dark wick. */
  int16_t max_h = 14;
  int16_t flame_h;
  if      (pct >= 40) flame_h = max_h;
  else if (pct >= 20) flame_h = max_h / 2;
  else if (pct >=  5) flame_h = 3;
  else                flame_h = 0;

  /* Handle. */
  graphics_context_set_fill_color(ctx, COLOR_HANDLE);
  graphics_fill_rect(ctx, GRect(base.x - 2, base.y, 4, 12), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_INK);
  graphics_draw_rect(ctx, GRect(base.x - 2, base.y, 4, 12));

  if (flame_h > 0) {
    graphics_context_set_fill_color(ctx, COLOR_FLAME);
    GPoint flame[3] = {
      { base.x,     base.y - flame_h },
      { base.x + 5, base.y },
      { base.x - 5, base.y },
    };
    GPathInfo info = { 3, flame };
    GPath *p = gpath_create(&info);
    gpath_draw_filled(ctx, p);
    gpath_destroy(p);
  }
}

void widgets_draw_stats(GContext *ctx, GRect b,
                        int16_t hr, uint8_t pct) {
  int16_t mid_y = b.origin.y + b.size.h / 2;

  /* HP heart + value, left side. */
  GPoint heart = GPoint(b.origin.x + 22, mid_y);
  draw_heart(ctx, heart);

  char hr_buf[8];
  snprintf(hr_buf, sizeof(hr_buf), "%d HP", hr > 0 ? hr : 0);
  graphics_context_set_text_color(ctx, COLOR_INK);
  graphics_draw_text(ctx, hr_buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(heart.x + 14, mid_y - 12, 70, 24),
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  /* Torch + percent, right side. */
  GPoint torch = GPoint(b.origin.x + b.size.w - 70, mid_y);
  draw_torch(ctx, torch, pct);

  char pct_buf[8];
  snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct);
  graphics_draw_text(ctx, pct_buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(torch.x + 12, mid_y - 12, 56, 24),
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}
