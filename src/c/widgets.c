/*
 * widgets.c — ribbon (banner + feather + date + familiar) and stat row
 * (heart + torch sprite + battery %).
 *
 * Bitmap ownership lives here: widgets_init loads the banner, feather,
 * and torch atlas once; widgets_deinit releases them. The draw
 * functions are stateless beyond reading from those module-scope
 * pointers. The heart and the familiar stay procedural — they look
 * right at this size and don't yet have art.
 */

#include "widgets.h"

#define COLOR_INK        GColorBlack
#define COLOR_HEART      PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorDarkGray)
#define COLOR_BPM_INK    PBL_IF_COLOR_ELSE(GColorWhite,             GColorWhite)
#define COLOR_FAINT      PBL_IF_COLOR_ELSE(GColorWindsorTan,        GColorLightGray)

/* --- Bitmap resources --------------------------------------------------- */

/* Torch atlas is 128×32 — 4 frames of 32×32 laid out left-to-right:
 * full / half / embers / dark. Frame index maps directly to battery
 * state via torch_state_for(pct). */
#define TORCH_FRAME_W   32
#define TORCH_FRAME_H   32
#define TORCH_N_FRAMES  4

static GBitmap *s_banner;
static GBitmap *s_feather;
static GBitmap *s_torch_sheet;
static GBitmap *s_torch_frames[TORCH_N_FRAMES];

void widgets_init(void) {
  s_banner      = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_TOP_BANNER);
  s_feather     = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_FEATHER);
  s_torch_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_TORCH_SHEET);
  if (s_torch_sheet) {
    for (int i = 0; i < TORCH_N_FRAMES; i++) {
      s_torch_frames[i] = gbitmap_create_as_sub_bitmap(
        s_torch_sheet,
        GRect(i * TORCH_FRAME_W, 0, TORCH_FRAME_W, TORCH_FRAME_H));
    }
  }
}

void widgets_deinit(void) {
  /* Sub-bitmaps share data with their parent atlas — destroy them
   * BEFORE the parent. */
  for (int i = 0; i < TORCH_N_FRAMES; i++) {
    if (s_torch_frames[i]) {
      gbitmap_destroy(s_torch_frames[i]);
      s_torch_frames[i] = NULL;
    }
  }
  if (s_torch_sheet) { gbitmap_destroy(s_torch_sheet); s_torch_sheet = NULL; }
  if (s_feather)     { gbitmap_destroy(s_feather);     s_feather     = NULL; }
  if (s_banner)      { gbitmap_destroy(s_banner);      s_banner      = NULL; }
}

/* --- Ribbon ------------------------------------------------------------- */

void widgets_draw_ribbon(GContext *ctx, GRect b,
                         int16_t doy, const char *date,
                         bool bt) {
  /* Banner bitmap covers the entire ribbon rect (200×28 by construction). */
  if (s_banner) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_banner, b);
  }

  /* Feather quill on the left corner. */
  if (s_feather) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_feather,
      GRect(b.origin.x + 4,
            b.origin.y + (b.size.h - 20) / 2,
            20, 20));
  }

  /* Date text centered, with slots reserved on both ends for the
   * feather (left) and familiar (right). */
  char buf[24];
  snprintf(buf, sizeof(buf), "Day %d  %s", doy, date);
  graphics_context_set_text_color(ctx, COLOR_INK);
  graphics_draw_text(ctx, buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(b.origin.x + 26, b.origin.y - 2,
          b.size.w - 52, b.size.h),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  /* Familiar placeholder in the right corner. Connected: solid outline
   * + eye dot; disconnected: faded outline. Phase 5 swaps to selectable
   * creature silhouettes. */
  GPoint fam = GPoint(b.origin.x + b.size.w - 14,
                      b.origin.y + b.size.h / 2);
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

/* --- Heart with BPM inside (procedural) -------------------------------- */

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
  const char *txt;
  char num_buf[6];
  if (hr > 0) {
    snprintf(num_buf, sizeof(num_buf), "%d", hr);
    txt = num_buf;
  } else {
    txt = "\xE2\x80\x94"; /* em dash */
  }
  graphics_context_set_text_color(ctx, COLOR_BPM_INK);
  graphics_draw_text(ctx, txt,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(c.x - 14, c.y - 11, 28, 16),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

/* --- Torch state -------------------------------------------------------- */

/* Atlas frame indices, in the same order the artwork lays them out:
 * full / half / embers / dark. */
typedef enum {
  TORCH_FULL   = 0,
  TORCH_HALF   = 1,
  TORCH_EMBERS = 2,
  TORCH_DARK   = 3,
} TorchState;

static TorchState torch_state_for(uint8_t pct) {
  if (pct >= 40) return TORCH_FULL;
  if (pct >= 20) return TORCH_HALF;
  if (pct >=  5) return TORCH_EMBERS;
  return TORCH_DARK;
}

/* --- Composite stat row ------------------------------------------------- */

void widgets_draw_stats(GContext *ctx, GRect b,
                        int16_t hr, uint8_t pct) {
  /* Heart on the left, vertically centered. */
  GPoint heart_c = GPoint(b.origin.x + 22,
                          b.origin.y + b.size.h / 2);
  draw_heart_body(ctx, heart_c);
  draw_bpm_inside(ctx, heart_c, hr);

  /* Torch on the right. The 32×32 sprite frame holds the flame in its
   * upper region and is transparent below — the percent label sits in
   * that lower transparent area so the two stack visually. Sprite is
   * pinned to the right edge with a 4-px breathing margin. */
  TorchState state = torch_state_for(pct);
  if (s_torch_frames[state]) {
    GRect torch_rect = GRect(b.origin.x + b.size.w - 36,
                             b.origin.y,
                             TORCH_FRAME_W, TORCH_FRAME_H);
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_torch_frames[state], torch_rect);
  }

  char pct_buf[8];
  snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct);
  graphics_context_set_text_color(ctx, COLOR_INK);
  graphics_draw_text(ctx, pct_buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(b.origin.x + b.size.w - 40,
          b.origin.y + 17,
          32, 14),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}
