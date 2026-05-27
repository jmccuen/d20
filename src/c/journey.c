/*
 * journey.c — procedural journey strip with smooth token motion.
 *
 * Token position is stored as a fixed-point fraction in [0, TRAIL_MAX] so
 * the slide animation is independent of trail pixel width. Three things
 * can change the target position:
 *
 *   1. Step count update          → target = steps/goal
 *   2. Sleep starts                → target = 0 (camp)
 *   3. Sleep ends                  → target = steps/goal again
 *
 * Any target change schedules a slide animation from the current displayed
 * position to the new target, replacing whatever was already in flight.
 *
 * A "Xh Ym rest" label renders under the camp for REST_LABEL_HOLD_S
 * seconds after waking. The duration is computed from the timestamp pair
 * (sleep_started_at, sleep_ended_at).
 *
 * Phase 4 will replace the camp / chest / boss / token primitives with
 * proper sprite art and add a class silhouette inside the token.
 */

#include "journey.h"

#define COLOR_TRAIL    PBL_IF_COLOR_ELSE(GColorWindsorTan,       GColorDarkGray)
#define COLOR_INK      GColorBlack
#define COLOR_TREASURE PBL_IF_COLOR_ELSE(GColorChromeYellow,     GColorWhite)
#define COLOR_BOSS     PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorDarkGray)
#define COLOR_LABEL    PBL_IF_COLOR_ELSE(GColorBulgarianRose,    GColorDarkGray)

#define TRAIL_MAX             10000  /* fixed-point denominator for token_p_* */
#define SLIDE_STEP_MS         600    /* step-update tween */
#define SLIDE_SLEEP_MS        1200   /* walk-to-camp / depart-camp */
#define REST_LABEL_HOLD_S     (2 * 60 * 60)  /* show rest label for 2 h */

static struct {
  Layer    *layer;
  int32_t   steps;
  int32_t   goal;
  bool      sleeping;

  /* Token position as a fraction of trail span, 0..TRAIL_MAX. */
  int32_t   token_p_current;
  int32_t   token_p_target;
  int32_t   token_p_slide_start;
  Animation *slide_anim;

  /* Walk-frame index 0..2 — set by slide_update from animation progress
   * while a slide is in flight. Read by draw_token; ignored when no
   * slide is active (idle frame used instead). */
  int8_t    walk_frame_idx;

  /* Wake-rest label state. */
  time_t    sleep_started_at;
  time_t    sleep_ended_at;
} s_j;

/* Mage sprite frames + campfire sprite. Loaded once in journey_init,
 * released in journey_deinit. */
#define CAMP_W  10
#define CAMP_H  10

static GBitmap *s_mage_idle_1;
static GBitmap *s_mage_idle_2;  /* reserved for future idle cycle */
static GBitmap *s_mage_walk_1;
static GBitmap *s_mage_walk_2;
static GBitmap *s_mage_walk_3;
static GBitmap *s_camp;

/* --- Slide animation ---------------------------------------------------- */

static void slide_setup(Animation *anim) {
  (void)anim;
}

static void slide_update(Animation *anim, const AnimationProgress p) {
  (void)anim;
  int32_t span = s_j.token_p_target - s_j.token_p_slide_start;
  s_j.token_p_current = s_j.token_p_slide_start
                      + (int32_t)((int64_t)span * p / ANIMATION_NORMALIZED_MAX);

  /* Walk cycle across the 3 frames over the slide duration. */
  int idx = (int)((int64_t)p * 3 / ANIMATION_NORMALIZED_MAX);
  if (idx > 2) idx = 2;
  s_j.walk_frame_idx = (int8_t)idx;

  if (s_j.layer) layer_mark_dirty(s_j.layer);
}

static void slide_teardown(Animation *anim) {
  (void)anim;
  s_j.token_p_current = s_j.token_p_target;
  s_j.slide_anim = NULL;
  if (s_j.layer) layer_mark_dirty(s_j.layer);
}

static const AnimationImplementation s_slide_impl = {
  .setup    = slide_setup,
  .update   = slide_update,
  .teardown = slide_teardown,
};

static void start_slide(uint32_t duration_ms) {
  if (s_j.slide_anim) {
    animation_unschedule(s_j.slide_anim);
    /* teardown nulls slide_anim and snaps current to old target.
     * The next line overrides that with the real start point. */
  }
  s_j.token_p_slide_start = s_j.token_p_current;

  Animation *anim = animation_create();
  animation_set_implementation(anim, &s_slide_impl);
  animation_set_handlers(anim, (AnimationHandlers){
    .started = NULL,
    .stopped = NULL,
  }, NULL);
  animation_set_duration(anim, duration_ms);
  animation_set_curve(anim, AnimationCurveEaseInOut);
  s_j.slide_anim = anim;
  animation_schedule(anim);
}

/* --- Target computation ------------------------------------------------- */

static int32_t compute_step_target(void) {
  if (s_j.goal <= 0) return 0;
  int32_t clamped = (s_j.steps < s_j.goal) ? s_j.steps : s_j.goal;
  if (clamped < 0) clamped = 0;
  return (int32_t)((int64_t)clamped * TRAIL_MAX / s_j.goal);
}

static void retarget(uint32_t duration_ms) {
  int32_t new_target = s_j.sleeping ? 0 : compute_step_target();
  if (new_target == s_j.token_p_target && s_j.slide_anim == NULL) {
    /* Already there and idle; no work. */
    return;
  }
  s_j.token_p_target = new_target;
  if (s_j.token_p_target == s_j.token_p_current) {
    if (s_j.slide_anim) {
      animation_unschedule(s_j.slide_anim);
    }
    if (s_j.layer) layer_mark_dirty(s_j.layer);
    return;
  }
  start_slide(duration_ms);
}

/* --- Public API --------------------------------------------------------- */

void journey_init(Layer *layer, int32_t step_goal) {
  s_j.layer            = layer;
  s_j.steps            = 0;
  s_j.goal             = step_goal > 0 ? step_goal : 10000;
  s_j.sleeping         = false;
  s_j.token_p_current  = 0;
  s_j.token_p_target   = 0;
  s_j.slide_anim       = NULL;
  s_j.walk_frame_idx   = 0;
  s_j.sleep_started_at = 0;
  s_j.sleep_ended_at   = 0;

  s_mage_idle_1 = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MAGE_IDLE_1);
  s_mage_idle_2 = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MAGE_IDLE_2);
  s_mage_walk_1 = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MAGE_WALK_1);
  s_mage_walk_2 = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MAGE_WALK_2);
  s_mage_walk_3 = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MAGE_WALK_3);
  s_camp        = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CAMP);
}

void journey_deinit(void) {
  if (s_j.slide_anim) {
    animation_unschedule(s_j.slide_anim);
    s_j.slide_anim = NULL;
  }
  s_j.layer = NULL;
  if (s_camp)        { gbitmap_destroy(s_camp);        s_camp        = NULL; }
  if (s_mage_walk_3) { gbitmap_destroy(s_mage_walk_3); s_mage_walk_3 = NULL; }
  if (s_mage_walk_2) { gbitmap_destroy(s_mage_walk_2); s_mage_walk_2 = NULL; }
  if (s_mage_walk_1) { gbitmap_destroy(s_mage_walk_1); s_mage_walk_1 = NULL; }
  if (s_mage_idle_2) { gbitmap_destroy(s_mage_idle_2); s_mage_idle_2 = NULL; }
  if (s_mage_idle_1) { gbitmap_destroy(s_mage_idle_1); s_mage_idle_1 = NULL; }
}

void journey_set_steps(int32_t steps) {
  if (steps == s_j.steps) return;
  s_j.steps = steps;
  if (!s_j.sleeping) {
    retarget(SLIDE_STEP_MS);
  }
}

void journey_set_goal(int32_t goal) {
  if (goal <= 0 || goal == s_j.goal) return;
  s_j.goal = goal;
  if (!s_j.sleeping) {
    retarget(SLIDE_STEP_MS);
  }
}

void journey_set_sleeping(bool sleeping) {
  if (sleeping == s_j.sleeping) return;
  s_j.sleeping = sleeping;
  time_t now = time(NULL);
  if (sleeping) {
    s_j.sleep_started_at = now;
  } else {
    s_j.sleep_ended_at = now;
  }
  retarget(SLIDE_SLEEP_MS);
}

/* --- Drawing primitives ------------------------------------------------- */

static void draw_camp(GContext *ctx, GPoint at, bool sleeping) {
  /* Camp sprite is 10×10. Centered on (at.x, at.y) — the trail midline.
   * sleeping is unused for now: the campfire is always lit in the art.
   * A separate idle / no-flame variant can come later. */
  (void)sleeping;
  if (!s_camp) return;
  GRect dest = GRect(at.x - CAMP_W / 2, at.y - CAMP_H / 2, CAMP_W, CAMP_H);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_camp, dest);
}

static void draw_chest(GContext *ctx, GPoint at) {
  GRect body = GRect(at.x - 5, at.y - 3, 10, 8);
  graphics_context_set_fill_color(ctx, COLOR_TREASURE);
  graphics_fill_rect(ctx, body, 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, COLOR_INK);
  graphics_draw_rect(ctx, body);
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
  /* Frame pick:
   *   - Sleeping: idle frame (mage at camp).
   *   - Mid-slide: cycle the 3 walk frames keyed off slide progress.
   *   - Otherwise: idle frame at the current position.
   * Class is hardcoded to mage for now; selectable class silhouettes
   * are Phase 5b. */
  GBitmap *frame;
  if (s_j.slide_anim && !sleeping) {
    GBitmap *walks[3] = { s_mage_walk_1, s_mage_walk_2, s_mage_walk_3 };
    int idx = s_j.walk_frame_idx;
    if (idx < 0) idx = 0;
    if (idx > 2) idx = 2;
    frame = walks[idx];
  } else {
    frame = s_mage_idle_1;
  }

  if (frame) {
    GSize size = gbitmap_get_bounds(frame).size;
    GRect dest = GRect(at.x - size.w / 2,
                       at.y - size.h / 2,
                       size.w, size.h);
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, frame, dest);
  }

  if (sleeping) {
    graphics_context_set_text_color(ctx, COLOR_INK);
    graphics_draw_text(ctx, "Z",
      fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
      GRect(at.x + 8, at.y - 18, 10, 14),
      GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  }
}

static void draw_rest_label(GContext *ctx, int16_t camp_x, int16_t camp_y) {
  /* Render only inside the 2-hour window after a wake event. */
  if (s_j.sleep_ended_at == 0)                       return;
  if (s_j.sleep_started_at == 0)                     return;
  if (s_j.sleep_ended_at <= s_j.sleep_started_at)    return;
  time_t now = time(NULL);
  if (now - s_j.sleep_ended_at > REST_LABEL_HOLD_S)  return;

  int32_t rest_s = s_j.sleep_ended_at - s_j.sleep_started_at;
  int rest_h = (int)(rest_s / 3600);
  int rest_m = (int)((rest_s % 3600) / 60);

  char buf[16];
  snprintf(buf, sizeof(buf), "%dh %dm rest", rest_h, rest_m);
  graphics_context_set_text_color(ctx, COLOR_LABEL);
  graphics_draw_text(ctx, buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(camp_x - 30, camp_y + 6, 80, 14),
    GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

/* --- Composition -------------------------------------------------------- */

void journey_draw(GContext *ctx, GRect bounds) {
  const int16_t y    = bounds.size.h / 2;
  const int16_t x0   = 16;
  const int16_t x1   = bounds.size.w - 16;
  const int16_t span = x1 - x0;

  /* Dashed trail. Halved alpha is not available on color e-paper, so during
   * sleep we just skip more of the dashes for a "muted" look. */
  graphics_context_set_stroke_color(ctx, COLOR_TRAIL);
  graphics_context_set_stroke_width(ctx, 2);
  const int16_t period = s_j.sleeping ? 10 : 6;
  const int16_t seglen = 3;
  for (int16_t x = x0; x < x1; x += period) {
    int16_t seg_end = (x + seglen < x1) ? (x + seglen) : x1;
    graphics_draw_line(ctx, GPoint(x, y), GPoint(seg_end, y));
  }

  draw_camp (ctx, GPoint(x0,            y), s_j.sleeping);
  draw_chest(ctx, GPoint(x0 + span / 2, y));
  draw_boss (ctx, GPoint(x1,            y));

  /* Token: position derived from token_p_current.
   *   - Sleep mode: animation has slid current toward 0 (camp).
   *   - No data: current sits at the small off-camp offset.
   *   - Otherwise: current interpolates toward steps/goal. */
  int16_t tx;
  if (s_j.token_p_current <= 0 && s_j.steps <= 0 && !s_j.sleeping) {
    tx = x0 + 4;  /* nudge off-camp so token doesn't read as stuck */
  } else {
    tx = x0 + (int16_t)((int64_t)span * s_j.token_p_current / TRAIL_MAX);
  }
  draw_token(ctx, GPoint(tx, y), s_j.sleeping);

  draw_rest_label(ctx, x0, y);
}
