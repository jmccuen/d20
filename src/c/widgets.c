/*
 * widgets.c — phase-1 procedural rendering of the ribbon and stat row.
 *
 * Phase 1 polish:
 *  - Ribbon is now a 6-vertex unfurled-scroll shape with small fold-crease
 *    accents at each tapered end.
 *  - Familiar placeholder has a visible silhouette (circle + eye) so the
 *    disconnected state can be a faded outline rather than absent.
 *  - Heart is sized to fit 3-digit BPM internally; the BPM number renders
 *    inside the heart; "no data" renders as an em-dash, not zero.
 *  - Torch has four visually distinct states (full / half / embers / dark
 *    wick), and battery percent is rendered beneath rather than beside.
 */

#include "widgets.h"

#define COLOR_RIBBON     PBL_IF_COLOR_ELSE(GColorRajah,             GColorWhite)
#define COLOR_INK        GColorBlack
#define COLOR_HEART      PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorDarkGray)
#define COLOR_BPM_INK    PBL_IF_COLOR_ELSE(GColorWhite,             GColorWhite)
#define COLOR_FLAME      PBL_IF_COLOR_ELSE(GColorOrange,            GColorWhite)
#define COLOR_FLAME_CORE PBL_IF_COLOR_ELSE(GColorIcterine,          GColorWhite)
#define COLOR_HANDLE     PBL_IF_COLOR_ELSE(GColorWindsorTan,        GColorDarkGray)
#define COLOR_FAINT      PBL_IF_COLOR_ELSE(GColorWindsorTan,        GColorLightGray)

/* --- Ribbon ------------------------------------------------------------- */

void widgets_draw_ribbon(GContext *ctx, GRect b,
                         int16_t doy, const char *date,
                         bool bt) {
  const int16_t taper = 8;
  const int16_t midy  = b.origin.y + b.size.h / 2;

  GPoint scroll[6] = {
    { b.origin.x + taper,                b.origin.y                  },
    { b.origin.x + b.size.w - taper - 1, b.origin.y                  },
    { b.origin.x + b.size.w - 1,         midy                        },
    { b.origin.x + b.size.w - taper - 1, b.origin.y + b.size.h - 1   },
    { b.origin.x + taper,                b.origin.y + b.size.h - 1   },
    { b.origin.x,                        midy                        },
  };
  GPathInfo info = { 6, scroll };
  GPath *p = gpath_create(&info);
  graphics_context_set_fill_color(ctx, COLOR_RIBBON);
  gpath_draw_filled(ctx, p);
  graphics_context_set_stroke_color(ctx, COLOR_INK);
  graphics_context_set_stroke_width(ctx, 2);
  gpath_draw_outline(ctx, p);
  gpath_destroy(p);

  /* Fold-crease accents on each tapered end — two short angled lines
   * meeting at the point, suggesting the back-fold of the scroll. */
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx,
    GPoint(b.origin.x + 3,                 midy - 5),
    GPoint(b.origin.x + taper,             midy));
  graphics_draw_line(ctx,
    GPoint(b.origin.x + 3,                 midy + 5),
    GPoint(b.origin.x + taper,             midy));
  graphics_draw_line(ctx,
    GPoint(b.origin.x + b.size.w - 4,      midy - 5),
    GPoint(b.origin.x + b.size.w - taper - 1, midy));
  graphics_draw_line(ctx,
    GPoint(b.origin.x + b.size.w - 4,      midy + 5),
    GPoint(b.origin.x + b.size.w - taper - 1, midy));

  /* Date text, centered in the body of the scroll. Reserve a small slot
   * on the right for the familiar so they never collide. */
  char buf[24];
  snprintf(buf, sizeof(buf), "Day %d  %s", doy, date);
  graphics_context_set_text_color(ctx, COLOR_INK);
  graphics_draw_text(ctx, buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(b.origin.x + taper, b.origin.y - 2,
          b.size.w - 2 * taper - 18, b.size.h),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  /* Familiar placeholder in the right body of the ribbon.
   * Connected: solid outline + eye dot (alert). Disconnected: faded outline
   * only, no eye, in a low-contrast color. Phase 5 swaps to selectable
   * creature silhouettes. */
  GPoint fam = GPoint(b.origin.x + b.size.w - taper - 10, midy);
  if (bt) {
    graphics_context_set_stroke_color(ctx, COLOR_INK);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, fam, 5);
    graphics_context_set_fill_color(ctx, COLOR_INK);
    graphics_fill_circle(ctx, GPoint(fam.x + 2, fam.y - 1), 1);
  } else {
    graphics_context_set_stroke_color(ctx, COLOR_FAINT);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, fam, 5);
  }
}

/* --- Heart with BPM inside ---------------------------------------------- */

static void draw_heart_body(GContext *ctx, GPoint c) {
  graphics_context_set_fill_color(ctx, COLOR_HEART);
  graphics_fill_circle(ctx, GPoint(c.x - 6, c.y - 4), 8);
  graphics_fill_circle(ctx, GPoint(c.x + 6, c.y - 4), 8);
  GPoint tri[3] = {
    { c.x - 13, c.y - 2 },
    { c.x + 13, c.y - 2 },
    { c.x,      c.y + 13 },
  };
  GPathInfo info = { 3, tri };
  GPath *p = gpath_create(&info);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

static void draw_bpm_inside(GContext *ctx, GPoint c, int16_t hr) {
  /* hr == 0 means HealthService hasn't reported a reading yet. The brief
   * calls for an em-dash rather than "0", and drops the "HP" suffix. */
  const char *txt;
  char num_buf[6];
  if (hr > 0) {
    snprintf(num_buf, sizeof(num_buf), "%d", hr);
    txt = num_buf;
  } else {
    txt = "\xE2\x80\x94"; /* U+2014 em dash, UTF-8 */
  }

  /* Interior text box centered on the heart, sitting in the wide upper
   * region above the triangle's tip. */
  graphics_context_set_text_color(ctx, COLOR_BPM_INK);
  graphics_draw_text(ctx, txt,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(c.x - 14, c.y - 11, 28, 16),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

/* --- Torch with 4 distinct battery states ------------------------------- */

typedef enum {
  TORCH_FULL,    /* >= 40% — full flame with core highlight */
  TORCH_HALF,    /* 20-40% — half flame, no core            */
  TORCH_EMBERS,  /*  5-20% — cluster of small embers        */
  TORCH_DARK,    /* <  5%  — single faint ember at the wick */
} TorchState;

static TorchState torch_state_for(uint8_t pct) {
  if (pct >= 40) return TORCH_FULL;
  if (pct >= 20) return TORCH_HALF;
  if (pct >=  5) return TORCH_EMBERS;
  return TORCH_DARK;
}

static void draw_flame_triangle(GContext *ctx, GPoint base, int16_t h, int16_t w) {
  GPoint flame[3] = {
    { base.x,         base.y - h },
    { base.x + w / 2, base.y     },
    { base.x - w / 2, base.y     },
  };
  GPathInfo info = { 3, flame };
  GPath *p = gpath_create(&info);
  graphics_context_set_fill_color(ctx, COLOR_FLAME);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

static void draw_torch(GContext *ctx, GPoint base, TorchState state) {
  /* Handle: short rectangle below the flame. base.y is the joint between
   * handle and flame. Sized small so the flame + handle + percent label
   * all stack within a 32-px stat row. */
  const int16_t handle_h = 7;
  graphics_context_set_fill_color(ctx, COLOR_HANDLE);
  graphics_fill_rect(ctx, GRect(base.x - 2, base.y, 4, handle_h),
                     0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_INK);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, GRect(base.x - 2, base.y, 4, handle_h));

  switch (state) {
    case TORCH_FULL:
      draw_flame_triangle(ctx, base, 10, 10);
      graphics_context_set_fill_color(ctx, COLOR_FLAME_CORE);
      graphics_fill_circle(ctx, GPoint(base.x, base.y - 4), 2);
      break;
    case TORCH_HALF:
      draw_flame_triangle(ctx, base, 5, 8);
      break;
    case TORCH_EMBERS:
      graphics_context_set_fill_color(ctx, COLOR_FLAME);
      graphics_fill_circle(ctx, GPoint(base.x - 2, base.y - 1), 1);
      graphics_fill_circle(ctx, GPoint(base.x + 2, base.y - 2), 1);
      graphics_fill_circle(ctx, GPoint(base.x,     base.y - 4), 1);
      break;
    case TORCH_DARK:
      graphics_context_set_fill_color(ctx, COLOR_FAINT);
      graphics_fill_circle(ctx, GPoint(base.x, base.y - 1), 1);
      break;
  }
}

/* --- Composite stat row ------------------------------------------------- */

void widgets_draw_stats(GContext *ctx, GRect b,
                        int16_t hr, uint8_t pct) {
  /* Heart on the left, vertically centered. */
  GPoint heart_c = GPoint(b.origin.x + 22,
                          b.origin.y + b.size.h / 2);
  draw_heart_body(ctx, heart_c);
  draw_bpm_inside(ctx, heart_c, hr);

  /* Torch column on the right, sized to fit inside a 32 px stat row:
   *   flame   (up to 10 px tall, drawn above base.y)
   *   handle  (7 px tall, drawn below base.y)
   *   percent label (14 px text box, drawn below the handle)
   * Total stack ≤ 31 px including a 1 px gap. */
  const int16_t flame_max = 10;
  const int16_t handle_h  = 7;
  GPoint torch_base = GPoint(b.origin.x + b.size.w - 30,
                             b.origin.y + flame_max);
  draw_torch(ctx, torch_base, torch_state_for(pct));

  char pct_buf[8];
  snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct);
  graphics_context_set_text_color(ctx, COLOR_INK);
  graphics_draw_text(ctx, pct_buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(torch_base.x - 20, torch_base.y + handle_h + 1, 40, 14),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}
