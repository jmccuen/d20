/*
 * journey.c — sine-wave trail with mage walking on it, plus the
 * surrounding info-area elements (camp + sleep hours, cloud + temp,
 * step count). Heart and torch live in widgets.c — face.c overlays
 * them on top of this draw.
 *
 * Token position is stored as a fixed-point fraction in [0, TRAIL_MAX]
 * so the slide animation is independent of trail pixel width. Three
 * things can change the target position:
 *
 *   1. Step count update          → target = steps/goal
 *   2. Sleep starts                → target = 0 (camp)
 *   3. Sleep ends                  → target = steps/goal again
 *
 * Any target change schedules a slide animation from the current
 * displayed position to the new target, replacing whatever was
 * already in flight.
 *
 * Sleep duration ("6.2h") is computed from the timestamp pair
 * (sleep_started_at, sleep_ended_at) and shown under the camp.
 */

#include "journey.h"

#define COLOR_TRAIL    PBL_IF_COLOR_ELSE(GColorWindsorTan,       GColorDarkGray)
#define COLOR_INK      GColorBlack
#define COLOR_LABEL    GColorBlack

#define TRAIL_MAX             10000  /* fixed-point denominator for token_p_* */
#define SLIDE_STEP_MS         600    /* step-update tween */
#define SLIDE_SLEEP_MS        1200   /* walk-to-camp / depart-camp */

/* Sine-wave trail geometry, in info-layer-local coordinates. */
#define TRAIL_X_START   22
#define TRAIL_X_END    130     /* cuts off before the torch on the right */
#define TRAIL_MIDLINE   38
#define TRAIL_AMP        8

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

  /* Sleep tracking. sleep_started_at / sleep_ended_at drive the
   * "currently sleeping" state from HealthEventSleepUpdate events;
   * sleep_secs is today's cumulative sleep, polled per-minute from
   * HealthMetricSleepSeconds and shown under the campfire. */
  time_t    sleep_started_at;
  time_t    sleep_ended_at;
  int32_t   sleep_secs;
} s_j;

/* Sprite assets. Loaded once in journey_init, released in journey_deinit. */
#define CAMP_W   10
#define CAMP_H   10
#define CLOUD_W  32
#define CLOUD_H  22

static GBitmap *s_mage_idle_1;
static GBitmap *s_mage_idle_2;  /* reserved for future idle cycle */
static GBitmap *s_mage_walk_1;
static GBitmap *s_mage_walk_2;
static GBitmap *s_mage_walk_3;
static GBitmap *s_camp;
static GBitmap *s_cloud;

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
  if (!s_mage_idle_1) APP_LOG(APP_LOG_LEVEL_WARNING, "journey: mage_idle_1 failed");
  s_mage_idle_2 = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MAGE_IDLE_2);
  if (!s_mage_idle_2) APP_LOG(APP_LOG_LEVEL_WARNING, "journey: mage_idle_2 failed");
  s_mage_walk_1 = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MAGE_WALK_1);
  if (!s_mage_walk_1) APP_LOG(APP_LOG_LEVEL_WARNING, "journey: mage_walk_1 failed");
  s_mage_walk_2 = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MAGE_WALK_2);
  if (!s_mage_walk_2) APP_LOG(APP_LOG_LEVEL_WARNING, "journey: mage_walk_2 failed");
  s_mage_walk_3 = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MAGE_WALK_3);
  if (!s_mage_walk_3) APP_LOG(APP_LOG_LEVEL_WARNING, "journey: mage_walk_3 failed");
  s_camp        = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CAMP);
  if (!s_camp)        APP_LOG(APP_LOG_LEVEL_WARNING, "journey: camp failed");
  s_cloud       = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CLOUD);
  if (!s_cloud)       APP_LOG(APP_LOG_LEVEL_WARNING, "journey: cloud failed");
}

void journey_deinit(void) {
  if (s_j.slide_anim) {
    animation_unschedule(s_j.slide_anim);
    s_j.slide_anim = NULL;
  }
  s_j.layer = NULL;
  if (s_cloud)       { gbitmap_destroy(s_cloud);       s_cloud       = NULL; }
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

void journey_set_sleep_seconds(int32_t seconds) {
  if (seconds == s_j.sleep_secs) return;
  s_j.sleep_secs = seconds;
  if (s_j.layer) layer_mark_dirty(s_j.layer);
}

/* --- Helpers ------------------------------------------------------------ */

/* Sine y-position for a given x along the trail.
 * One full cycle from TRAIL_X_START to TRAIL_X_END. */
static int16_t sine_y(int16_t x) {
  int32_t span = TRAIL_X_END - TRAIL_X_START;
  if (span <= 0) return TRAIL_MIDLINE;
  int32_t phase = (int32_t)(x - TRAIL_X_START) * TRIG_MAX_ANGLE / span;
  int32_t s = sin_lookup(phase);
  return (int16_t)(TRAIL_MIDLINE + (s * TRAIL_AMP) / TRIG_MAX_RATIO);
}

/* "245" for sub-1k, "1.2k" / "12.3k" for thousands. */
static void format_steps(int32_t steps, char *buf, size_t buf_size) {
  if (steps < 0) steps = 0;
  if (steps < 1000) {
    snprintf(buf, buf_size, "%ld", (long)steps);
  } else {
    int32_t tenths = steps / 100;     /* e.g. 1234 → 12 → "1.2k" */
    snprintf(buf, buf_size, "%ld.%ldk",
             (long)(tenths / 10), (long)(tenths % 10));
  }
}

/* "—" when no sleep data yet today, otherwise "6.2h". Reads the
 * cumulative seconds pushed in by face_on_tick's HealthService poll. */
static void format_sleep_hours(char *buf, size_t buf_size) {
  if (s_j.sleep_secs <= 0) {
    snprintf(buf, buf_size, "\xE2\x80\x94");  /* em dash */
    return;
  }
  int32_t deci_h = (s_j.sleep_secs * 10) / 3600;
  snprintf(buf, buf_size, "%ld.%ldh",
           (long)(deci_h / 10), (long)(deci_h % 10));
}

/* --- Drawing primitives ------------------------------------------------- */

static void draw_trail(GContext *ctx, bool sleeping) {
  /* Dashed sine wave from TRAIL_X_START to TRAIL_X_END. During sleep
   * we widen the gaps so the trail reads as muted/inactive. */
  graphics_context_set_stroke_color(ctx, COLOR_TRAIL);
  graphics_context_set_stroke_width(ctx, 2);
  const int16_t period = sleeping ? 10 : 6;
  const int16_t seglen = 3;
  for (int16_t x = TRAIL_X_START; x < TRAIL_X_END; x += period) {
    int16_t end_x = (x + seglen < TRAIL_X_END) ? (x + seglen) : TRAIL_X_END;
    graphics_draw_line(ctx,
      GPoint(x,     sine_y(x)),
      GPoint(end_x, sine_y(end_x)));
  }
}

static void draw_camp(GContext *ctx) {
  if (!s_camp) return;
  /* Camp at the trail's start, sitting on the trail's local y. */
  int16_t cx = TRAIL_X_START;
  int16_t cy = sine_y(cx);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_camp,
    GRect(cx - CAMP_W / 2, cy - CAMP_H / 2, CAMP_W, CAMP_H));

  /* Sleep hours under the camp. */
  char buf[8];
  format_sleep_hours(buf, sizeof(buf));
  graphics_context_set_text_color(ctx, COLOR_LABEL);
  graphics_draw_text(ctx, buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(cx - 18, TRAIL_MIDLINE + 12, 36, 14),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void draw_cloud_and_temp(GContext *ctx, GRect bounds,
                                int16_t temp, bool is_f) {
  if (!s_cloud) return;
  /* Cloud centred horizontally on the screen (not the trail) so it
   * reads as a separate weather panel rather than another trail
   * marker. */
  int16_t cx = bounds.size.w / 2;
  int16_t cy = 12;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_cloud,
    GRect(cx - CLOUD_W / 2, cy - CLOUD_H / 2, CLOUD_W, CLOUD_H));

  /* Temperature centred inside the cloud — "28°". Unit selection
   * (C/F) is deferred until settings ship. */
  (void)is_f;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d\xc2\xb0", temp);
  graphics_context_set_text_color(ctx, COLOR_INK);
  graphics_draw_text(ctx, buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(cx - 16, cy - 9, 32, 16),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void draw_step_count(GContext *ctx, GRect bounds) {
  /* Numeric step count centred horizontally on the screen, sitting
   * just below the trail's vertical midline. */
  char buf[8];
  format_steps(s_j.steps, buf, sizeof(buf));
  int16_t cx = bounds.size.w / 2;
  graphics_context_set_text_color(ctx, COLOR_LABEL);
  graphics_draw_text(ctx, buf,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(cx - 24, TRAIL_MIDLINE + 12, 48, 14),
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void draw_trail_end(GContext *ctx) {
  /* Small triangular sigil at the end of the trail — the journey
   * destination. Sits on the sine y so it reads as part of the path. */
  int16_t tx = TRAIL_X_END;
  int16_t ty = sine_y(tx);
  GPoint tri[3] = {
    { tx,     ty - 5 },
    { tx + 4, ty + 3 },
    { tx - 4, ty + 3 },
  };
  GPathInfo info = { 3, tri };
  GPath *p = gpath_create(&info);
  graphics_context_set_fill_color(ctx,
    PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorDarkGray));
  gpath_draw_filled(ctx, p);
  graphics_context_set_stroke_color(ctx, COLOR_INK);
  graphics_context_set_stroke_width(ctx, 1);
  gpath_draw_outline(ctx, p);
  gpath_destroy(p);
}

static void draw_mage(GContext *ctx, bool sleeping) {
  /* Pick the frame:
   *   - mid-slide: cycle the 3 walk frames keyed off slide progress;
   *   - otherwise: idle frame. */
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
  if (!frame) return;

  /* Mage x-position along the trail; y follows the sine wave. When
   * stationary at camp (no steps, not sleeping) we nudge a few px so
   * the mage isn't drawn dead-centre on the campfire. */
  int32_t span = TRAIL_X_END - TRAIL_X_START;
  int16_t mx;
  if (s_j.token_p_current <= 0 && s_j.steps <= 0 && !sleeping) {
    mx = TRAIL_X_START + 4;
  } else {
    mx = TRAIL_X_START + (int16_t)((int64_t)span * s_j.token_p_current / TRAIL_MAX);
  }
  int16_t my = sine_y(mx);

  GSize size = gbitmap_get_bounds(frame).size;
  /* Stand on the trail rather than centre on it — bottom of sprite at
   * the sine y. */
  GRect dest = GRect(mx - size.w / 2,
                     my - size.h + 2,
                     size.w, size.h);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, frame, dest);

  if (sleeping) {
    graphics_context_set_text_color(ctx, COLOR_INK);
    graphics_draw_text(ctx, "Z",
      fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
      GRect(mx + 8, my - 22, 10, 14),
      GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  }
}

/* --- Composition -------------------------------------------------------- */

void journey_draw(GContext *ctx, GRect bounds,
                  int16_t weather_temp, bool weather_is_f) {
  draw_trail(ctx, s_j.sleeping);
  draw_trail_end(ctx);
  draw_camp(ctx);
  draw_cloud_and_temp(ctx, bounds, weather_temp, weather_is_f);
  draw_step_count(ctx, bounds);
  draw_mage(ctx, s_j.sleeping);
}
