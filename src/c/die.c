/*
 * die.c — phase-1 procedural die rendering.
 *
 * The polygon shapes here are scaffolding only — they exist so every
 * downstream system (animation timing, value updates, layout, mark-dirty)
 * can be developed and tested without art.
 *
 * Notes / known limitations:
 *  - GPath is allocated / freed inside the update_proc here for clarity.
 *    For production, cache the GPaths and update vertex positions in place,
 *    or just swap to sprites (Phase 2) where rotation is free.
 *  - The numeral is drawn upright on top of a rotating body. During a tumble
 *    the body spins under static text — looks engineer-ish, but is fine for
 *    tuning the animation timing. Sprites fix this naturally because the
 *    numeral is baked into the rotating bitmap.
 */

#include "die.h"

/* Palette stand-ins. Tune to match the brief once you've eyeballed the
 * 64-color palette in the emulator. */
#define COLOR_DIE_BODY    PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define COLOR_DIE_OUTLINE GColorBlack
#define COLOR_DIE_INK     GColorBlack

static GPoint rotated_vertex(GPoint c, int16_t r, int32_t a) {
  int32_t s = sin_lookup(a);
  int32_t k = cos_lookup(a);
  return GPoint(c.x + (int16_t)(s * r / TRIG_MAX_RATIO),
                c.y - (int16_t)(k * r / TRIG_MAX_RATIO));
}

static void draw_polygon(GContext *ctx, GPoint *pts, int n,
                         GColor fill, GColor stroke) {
  GPathInfo info = { (uint32_t)n, pts };
  GPath *p = gpath_create(&info);
  graphics_context_set_fill_color(ctx, fill);
  gpath_draw_filled(ctx, p);
  graphics_context_set_stroke_color(ctx, stroke);
  graphics_context_set_stroke_width(ctx, 2);
  gpath_draw_outline(ctx, p);
  gpath_destroy(p);
}

static void draw_pentagon(GContext *ctx, GPoint c, int16_t r, int32_t rot) {
  GPoint pts[5];
  for (int i = 0; i < 5; i++) {
    int32_t a = rot + (TRIG_MAX_ANGLE * i / 5) - (TRIG_MAX_ANGLE / 4);
    pts[i] = rotated_vertex(c, r, a);
  }
  draw_polygon(ctx, pts, 5, COLOR_DIE_BODY, COLOR_DIE_OUTLINE);
}

static void draw_kite(GContext *ctx, GPoint c, int16_t r, int32_t rot) {
  /* A simple 4-vertex diamond stands in for the prominent face of a d10.
   * The shorter horizontal axis hints at the trapezohedron midline. */
  GPoint pts[4];
  pts[0] = rotated_vertex(c, r,             rot);
  pts[1] = rotated_vertex(c, r * 9 / 10,    rot + TRIG_MAX_ANGLE / 4);
  pts[2] = rotated_vertex(c, r,             rot + TRIG_MAX_ANGLE / 2);
  pts[3] = rotated_vertex(c, r * 9 / 10,    rot + 3 * TRIG_MAX_ANGLE / 4);
  draw_polygon(ctx, pts, 4, COLOR_DIE_BODY, COLOR_DIE_OUTLINE);
}

static void draw_face_number(GContext *ctx, const Die *die) {
  char buf[4];
  if (die->type == DIE_TENS) {
    /* tens die labels are 00, 10, 20, 30, 40, 50 */
    snprintf(buf, sizeof(buf), "%d0", die->value);
  } else {
    snprintf(buf, sizeof(buf), "%d", die->value);
  }

  GFont font = (die->type == DIE_HOUR)
    ? fonts_get_system_font(FONT_KEY_LECO_38_BOLD_NUMBERS)
    : fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);

  GRect text_box = GRect(die->center.x - die->radius,
                         die->center.y - die->radius / 2 - 2,
                         die->radius * 2, die->radius);
  graphics_context_set_text_color(ctx, COLOR_DIE_INK);
  graphics_draw_text(ctx, buf, font, text_box,
                     GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
}

void die_draw(GContext *ctx, const Die *die) {
  if (die->type == DIE_HOUR) {
    draw_pentagon(ctx, die->center, die->radius, die->rotation);
  } else {
    draw_kite(ctx, die->center, die->radius, die->rotation);
  }
  draw_face_number(ctx, die);
}
