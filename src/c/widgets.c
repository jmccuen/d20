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

/* Torch atlas is 256×64 — 4 frames of 64×64 laid out left-to-right:
 * full / half / embers / dark. Frame index maps directly to battery
 * state via torch_state_for(pct). */
#define TORCH_FRAME_W   64
#define TORCH_FRAME_H   64
#define TORCH_N_FRAMES  4

static GBitmap *s_banner;
static GBitmap *s_feather;
static GBitmap *s_torch_sheet;
static GBitmap *s_torch_frames[TORCH_N_FRAMES];

void widgets_init(void) {
  s_banner      = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_TOP_BANNER);
  if (!s_banner)
    APP_LOG(APP_LOG_LEVEL_WARNING, "widgets: banner failed to load");
  s_feather     = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_FEATHER);
  if (!s_feather)
    APP_LOG(APP_LOG_LEVEL_WARNING, "widgets: feather failed to load");
  s_torch_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_TORCH_SHEET);
  if (!s_torch_sheet) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "widgets: torch sheet failed to load");
  } else {
    for (int i = 0; i < TORCH_N_FRAMES; i++) {
      s_torch_frames[i] = gbitmap_create_as_sub_bitmap(
        s_torch_sheet,
        GRect(i * TORCH_FRAME_W, 0, TORCH_FRAME_W, TORCH_FRAME_H));
      if (!s_torch_frames[i])
        APP_LOG(APP_LOG_LEVEL_WARNING, "widgets: torch frame %d failed", i);
    }
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "widgets_init done: heap %u free",
          (unsigned)heap_bytes_free());
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

  /* Bluetooth familiar on the LEFT, lower + inward from the previous
   * placement. Connected: solid outline + eye dot. Disconnected: faded
   * outline. Phase 5 swaps to selectable creature silhouettes. */
  GPoint fam = GPoint(b.origin.x + 22, b.origin.y + b.size.h / 2 + 2);
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

  /* Feather quill on the RIGHT, lower + inward. */
  if (s_feather) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_feather,
      GRect(b.origin.x + b.size.w - 36,
            b.origin.y + (b.size.h - 20) / 2 + 1,
            20, 20));
  }

  /* Date text. Pushed down by ~5 px so the cap-height sits at the
   * banner's vertical mid-line. Width clamped to the space between
   * familiar (left) and feather (right). Format uses `%b` (abbreviated
   * month — "May", "Sep", "Oct") so even the widest months fit. */
  char buf[24];
  snprintf(buf, sizeof(buf), "Day %d  %s", doy, date);
  graphics_context_set_text_color(ctx, COLOR_INK);
  graphics_draw_text(ctx, buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(b.origin.x + 36, b.origin.y + 5,
          b.size.w - 72, b.size.h),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

/* --- Heart with BPM inside (procedural) -------------------------------- */

/* Smaller heart than the original — sized for the new info-area layout
 * where the heart is one of several elements in the top-left zone. */
void widgets_draw_heart(GContext *ctx, GPoint c, int16_t hr) {
  graphics_context_set_fill_color(ctx, COLOR_HEART);
  graphics_fill_circle(ctx, GPoint(c.x - 4, c.y - 3), 6);
  graphics_fill_circle(ctx, GPoint(c.x + 4, c.y - 3), 6);
  GPoint tri[3] = {
    { c.x - 10, c.y - 1 },
    { c.x + 10, c.y - 1 },
    { c.x,      c.y + 10 },
  };
  GPathInfo info = { 3, tri };
  GPath *p = gpath_create(&info);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);

  /* BPM rendered inside the heart. hr == 0 means HealthService hasn't
   * reported a reading yet; show an em-dash instead of "0". */
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
    GRect(c.x - 14, c.y - 10, 28, 14),
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

/* --- Big torch with battery % inside the flame ------------------------- */

void widgets_draw_torch(GContext *ctx, GPoint at, uint8_t pct) {
  TorchState state = torch_state_for(pct);
  if (!s_torch_frames[state]) return;

  /* Sprite drawn centred on `at`. */
  GRect rect = GRect(at.x - TORCH_FRAME_W / 2,
                     at.y - TORCH_FRAME_H / 2,
                     TORCH_FRAME_W, TORCH_FRAME_H);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_torch_frames[state], rect);

  /* Battery % rendered inside the flame at the top of the sprite.
   * White reads cleanly against the warm flame colours; at very low
   * battery the flame is mostly empty so the number falls onto the
   * parchment background — still legible. */
  char pct_buf[8];
  snprintf(pct_buf, sizeof(pct_buf), "%d", pct);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, pct_buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(at.x - 16, at.y - 22, 32, 22),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}
