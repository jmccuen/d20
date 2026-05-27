/*
 * face.c — owns FaceState and the layer tree.
 *
 * The face_on_* functions update FaceState, then either mark_dirty the
 * affected layers or hand off to a tumble / journey animation. Layer
 * update_procs read from s_state and the static Die instances and dispatch
 * to the module's draw function.
 *
 * Phase 2.5 step 4: the three sibling die layers (hour/tens/ones) collapsed
 * into a single stage layer that draws the dice tray + all three dice. Any
 * tumble marks the shared layer dirty and re-renders all three dice; the
 * cost is small (three procedural polyhedra per frame, well within budget)
 * and the architectural payoff is that dice can now overlap visually —
 * the minute dice in the new layout sit in front of/behind each other in
 * the tray.
 *
 * Phase 2 wires animations:
 *   - Hour tick  → TUMBLE_QUICK on the hour die
 *   - Minute tick → TUMBLE_SHAKE on the tens/ones die that changed
 *   - Tap (wrist-raise placeholder) → physics_throw, which moves AND
 *     rotates all three dice as a coordinated throw across the tray
 *     (bouncing off the felt walls and each other) before settling
 *     back at their home positions.
 *   - journey.c owns its own slide animations for step/sleep updates.
 */

#include "face.h"
#include "die.h"
#include "journey.h"
#include "physics.h"
#include "tumble.h"
#include "widgets.h"

/* --- State -------------------------------------------------------------- */

typedef struct {
  int8_t   hour;          /* 1..12                              */
  int8_t   minute;        /* 0..59                              */
  int16_t  day_of_year;
  char     date_str[16];  /* e.g. "26 May"                      */

  int16_t  heart_rate;    /* BPM, 0 when unavailable            */
  uint8_t  battery_pct;
  bool     bluetooth;
  bool     sleeping;

  int32_t  steps;
  int32_t  step_goal;

  /* Weather. Filled from PebbleKit JS via AppMessage in Phase 4.5;
   * for now we hold defaults so the cloud + temp render correctly. */
  int16_t  weather_temp;     /* value in the active unit */
  bool     weather_is_f;     /* true = °F, false = °C (default)    */
} FaceState;

static FaceState s_state;

/* Dice tray placeholder colors. Polygon stand-in until a sprite lands.
 * Felt is #16434a — a deep teal that contrasts with the warm dice
 * shading better than the previous bulgarian-rose; Pebble's 64-color
 * palette quantizes it to GColorMidnightGreen. */
#define COLOR_TRAY_FRAME PBL_IF_COLOR_ELSE(GColorWindsorTan,     GColorWhite)
#define COLOR_TRAY_FELT  PBL_IF_COLOR_ELSE(GColorMidnightGreen,  GColorBlack)

static Layer       *s_ribbon_layer;
static Layer       *s_dice_layer;
/* "Info" area below the dice tray: heart + camp + sleep hours on the
 * left, weather cloud + temp + sine-wave trail + step count in the
 * middle, big torch + battery% on the right. Replaces the previous
 * stats + journey strip pair — the torch sprite spans the full height
 * of this area so the two zones can't be drawn as separate layers
 * with their own clipping. */
static Layer       *s_info_layer;

/* Background parchment. Drawn procedurally as a 3-band "vignette" —
 * darker frame, warm tan body, pale cream center highlight — costing
 * three filled rects per redraw and zero bitmap memory. The bitmap
 * version was crashing on device with an app fault (OOM: the decoded
 * 200×228 bitmap was ~45 KB, well over the Emery app-heap budget once
 * the other sprites loaded). The procedural version sells the same
 * "aged scroll on a wood desk" look — the outer band reads as the
 * desk under the parchment in the small visible corners. */
static Layer *s_bg_layer;

static Die s_hour_die;
static Die s_tens_die;
static Die s_ones_die;

static TumbleHandle s_hour_tumble;
static TumbleHandle s_tens_tumble;
static TumbleHandle s_ones_tumble;

/* False during the initial state push from main.c so the dice snap into
 * their first values without an animation. Flipped to true at the end of
 * the first face_on_tick so subsequent ticks and taps animate. */
static bool s_warm;

/* Dev-only auto-roll. When DEBUG_AUTO_ROLL is defined, a repeating
 * app_timer fires face_on_tap every DEBUG_AUTO_ROLL_MS so the ceremonial
 * roll is visible without depending on the emulator's tap simulator.
 * Comment out for production builds. */
/* No longer needed
#define DEBUG_AUTO_ROLL
#define DEBUG_AUTO_ROLL_MS 5000
*/

#ifdef DEBUG_AUTO_ROLL
static AppTimer *s_debug_timer;

static void debug_roll_fire(void *context);

static void debug_roll_schedule(void) {
  s_debug_timer = app_timer_register(DEBUG_AUTO_ROLL_MS,
                                     debug_roll_fire, NULL);
}

static void debug_roll_fire(void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "auto-roll tick: heap %u free",
          (unsigned)heap_bytes_free());
  face_on_tap(0, 0);
  debug_roll_schedule();
}
#endif

/* --- Update procs ------------------------------------------------------- */

static void bg_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  /* Three-band "aged parchment" vignette: dark wear frame → warm tan
   * body → pale cream centre highlight. Costs three filled rects per
   * redraw and zero bitmap memory. Most of the inner zones are
   * covered by the dice tray and widgets; the gradient mainly shows
   * in interstitial areas (between heart and torch in the stat row,
   * around the journey-strip elements, at screen corners where the
   * banner's tapered shape doesn't reach). */
  graphics_context_set_fill_color(ctx,
    PBL_IF_COLOR_ELSE(GColorWindsorTan, GColorBlack));
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  graphics_context_set_fill_color(ctx,
    PBL_IF_COLOR_ELSE(GColorRajah, GColorWhite));
  graphics_fill_rect(ctx,
    GRect(2, 2, bounds.size.w - 4, bounds.size.h - 4),
    3, GCornersAll);

  graphics_context_set_fill_color(ctx,
    PBL_IF_COLOR_ELSE(GColorPastelYellow, GColorWhite));
  graphics_fill_rect(ctx,
    GRect(8, 8, bounds.size.w - 16, bounds.size.h - 16),
    5, GCornersAll);
}

static void dice_stage_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  /* Tray placeholder: outer wood/leather frame + darker felt interior.
   * Replaced by a sprite later — kept as polygons so the layout reads
   * during procedural development. */
  graphics_context_set_fill_color(ctx, COLOR_TRAY_FRAME);
  graphics_fill_rect(ctx, bounds, 6, GCornersAll);
  graphics_context_set_fill_color(ctx, COLOR_TRAY_FELT);
  graphics_fill_rect(ctx,
    GRect(6, 6, bounds.size.w - 12, bounds.size.h - 12),
    4, GCornersAll);

  /* Order matters where dice overlap. The two minute dice sit close
   * enough in the new layout that the ones die occludes part of the
   * tens die — draw tens first so ones lands on top. */
  die_draw(ctx, &s_hour_die);
  die_draw(ctx, &s_tens_die);
  die_draw(ctx, &s_ones_die);
}

static void ribbon_update(Layer *layer, GContext *ctx) {
  widgets_draw_ribbon(ctx, layer_get_bounds(layer),
                      s_state.day_of_year, s_state.date_str,
                      s_state.bluetooth);
}

static void info_update(Layer *layer, GContext *ctx) {
  /* Single update_proc for the combined heart / camp / weather / trail /
   * steps / torch area. journey_draw paints the trail-and-friends; the
   * heart and torch widgets layer on top with their own positioning. */
  GRect bounds = layer_get_bounds(layer);
  journey_draw(ctx, bounds, s_state.weather_temp, s_state.weather_is_f);
  widgets_draw_heart(ctx, GPoint(16, 14), s_state.heart_rate);
  widgets_draw_torch(ctx, GPoint(bounds.size.w - 32, 32),
                     s_state.battery_pct);
}

/* --- Init / deinit ------------------------------------------------------ */

void face_init(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  APP_LOG(APP_LOG_LEVEL_INFO, "face_init begin: heap %u free",
          (unsigned)heap_bytes_free());

  /* Fallback color in case the bg layer's update_proc hasn't fired yet
   * (single-frame transient). */
  window_set_background_color(window, PBL_IF_COLOR_ELSE(GColorRajah, GColorWhite));

  /* Defaults — overwritten by the initial push from main.c */
  s_state.step_goal     = 10000;
  s_state.hour          = 12;
  s_state.minute        = 0;
  s_state.battery_pct   = 100;
  s_state.bluetooth     = true;
  s_state.weather_temp  = 22;     /* placeholder until PKJS hooks up */
  s_state.weather_is_f  = false;  /* default to Celsius */

  /* Vertical layout, 228 px total:
   *   0   – 28   ribbon         (28 px)
   *   28  – 156  dice stage    (128 px — tray + all 3 dice)
   *   156 – 228  info area      (72 px — heart + camp + sleep hours
   *                              on left; cloud/temp + trail + step
   *                              count in middle; big torch on right)
   * Sections butt up against each other without overlap. */

  /* Background — flat-color fill (parchment-tone) added first so every
   * widget composes over it. See the static block at the top of this
   * file for why the bitmap is currently off. */
  s_bg_layer = layer_create(bounds);
  layer_set_update_proc(s_bg_layer, bg_update);
  layer_add_child(root, s_bg_layer);

  /* Date ribbon along the top. */
  s_ribbon_layer = layer_create(GRect(0, 0, bounds.size.w, 28));
  layer_set_update_proc(s_ribbon_layer, ribbon_update);
  layer_add_child(root, s_ribbon_layer);

  /* Dice stage — single shared layer hosting the tray + all three dice.
   * Die.center is in stage-local coordinates. The hour die sits left of
   * center as a "thrown" anchor; the two minute dice stack diagonally on
   * the right so the ones die occludes part of the tens. */
  s_dice_layer = layer_create(GRect(0, 28, bounds.size.w, 128));
  layer_set_update_proc(s_dice_layer, dice_stage_update);
  layer_add_child(root, s_dice_layer);

  s_hour_die = (Die){
    .center = GPoint(65, 60),
    .radius = 42,
    .value  = 12,
    .type   = DIE_HOUR,
  };
  s_tens_die = (Die){
    .center = GPoint(138, 42),
    .radius = 24,
    .value  = 0,
    .type   = DIE_TENS,
  };
  s_ones_die = (Die){
    .center = GPoint(158, 88),
    .radius = 24,
    .value  = 0,
    .type   = DIE_ONES,
  };

  /* Combined info area — heart, camp, sleep hours on the left; cloud
   * + temp, sine-wave trail, step count in the middle; big torch on
   * the right. */
  s_info_layer = layer_create(GRect(0, 156, bounds.size.w, 72));
  layer_set_update_proc(s_info_layer, info_update);
  layer_add_child(root, s_info_layer);

  /* All three TumbleHandles bind to the shared stage layer. */
  tumble_init(&s_hour_tumble, &s_hour_die, s_dice_layer);
  tumble_init(&s_tens_tumble, &s_tens_die, s_dice_layer);
  tumble_init(&s_ones_tumble, &s_ones_die, s_dice_layer);

  /* Physics needs the dice + their rest positions + the felt rect
   * (the inset region inside the tray frame — same GRect used by
   * dice_stage_update). The home positions match the Die.center values
   * above so the dice settle back to their layout positions. */
  physics_init(s_dice_layer,
               &s_hour_die, &s_tens_die, &s_ones_die,
               s_hour_die.center, s_tens_die.center, s_ones_die.center,
               GRect(6, 6, bounds.size.w - 12, 128 - 12));
  APP_LOG(APP_LOG_LEVEL_INFO, "after physics_init: heap %u free",
          (unsigned)heap_bytes_free());

  /* Sprite-owning modules load their bitmaps in their init. */
  widgets_init();
  APP_LOG(APP_LOG_LEVEL_INFO, "after widgets_init: heap %u free",
          (unsigned)heap_bytes_free());

  journey_init(s_info_layer, s_state.step_goal);
  APP_LOG(APP_LOG_LEVEL_INFO, "after journey_init: heap %u free",
          (unsigned)heap_bytes_free());

  s_warm = false;

#ifdef DEBUG_AUTO_ROLL
  debug_roll_schedule();
#endif
}

void face_deinit(void) {
#ifdef DEBUG_AUTO_ROLL
  if (s_debug_timer) {
    app_timer_cancel(s_debug_timer);
    s_debug_timer = NULL;
  }
#endif
  journey_deinit();
  widgets_deinit();
  physics_deinit();
  tumble_deinit(&s_ones_tumble);
  tumble_deinit(&s_tens_tumble);
  tumble_deinit(&s_hour_tumble);
  layer_destroy(s_info_layer);
  layer_destroy(s_dice_layer);
  layer_destroy(s_ribbon_layer);
  layer_destroy(s_bg_layer);
}

/* --- Event handlers ----------------------------------------------------- */

void face_on_tick(struct tm *tt, TimeUnits units_changed) {
  bool day_changed = (units_changed & DAY_UNIT) != 0;

  int new_hour = tt->tm_hour % 12;
  if (new_hour == 0) new_hour = 12;
  int new_minute = tt->tm_min;

  /* Skip dice updates while physics is in flight — physics owns
   * rot_x/y/z and die->center during the throw. We re-sync from
   * die->value at the next tick (or the next tap) after physics
   * settles. Worst case the dice are stale by at most one minute. */
  bool dice_owned_by_physics = physics_is_active();

  if (!dice_owned_by_physics && s_hour_die.value != new_hour) {
    if (s_warm) {
      tumble_start(&s_hour_tumble, TUMBLE_QUICK, new_hour, 0);
    } else {
      s_hour_die.value = new_hour;
      layer_mark_dirty(s_dice_layer);
    }
  }
  s_state.hour = new_hour;

  if (!dice_owned_by_physics) {
    int new_tens = new_minute / 10;
    int new_ones = new_minute % 10;
    if (s_tens_die.value != new_tens) {
      if (s_warm) {
        tumble_start(&s_tens_tumble, TUMBLE_SHAKE, new_tens, 0);
      } else {
        s_tens_die.value = new_tens;
        layer_mark_dirty(s_dice_layer);
      }
    }
    if (s_ones_die.value != new_ones) {
      if (s_warm) {
        tumble_start(&s_ones_tumble, TUMBLE_SHAKE, new_ones, 0);
      } else {
        s_ones_die.value = new_ones;
        layer_mark_dirty(s_dice_layer);
      }
    }
  }
  s_state.minute = new_minute;

  if (day_changed || s_state.date_str[0] == '\0') {
    s_state.day_of_year = tt->tm_yday + 1;
    strftime(s_state.date_str, sizeof(s_state.date_str), "%d %b", tt);
    layer_mark_dirty(s_ribbon_layer);
  }

  /* Poll steps + sleep totals each minute — both are cheap reads.
   * journey.c will slide the token if steps changed and rerender the
   * sleep-hours label if the sum changed. */
#if defined(PBL_HEALTH)
  s_state.steps = (int32_t)health_service_sum_today(HealthMetricStepCount);
  journey_set_steps(s_state.steps);
  journey_set_sleep_seconds(
    (int32_t)health_service_sum_today(HealthMetricSleepSeconds));
#endif

  s_warm = true;
}

void face_on_battery(BatteryChargeState s) {
  s_state.battery_pct = s.charge_percent;
  layer_mark_dirty(s_info_layer);
}

#if defined(PBL_HEALTH)
void face_on_health(HealthEventType event) {
  switch (event) {
    case HealthEventHeartRateUpdate: {
      HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
      s_state.heart_rate = (int16_t)bpm;
      layer_mark_dirty(s_info_layer);
      break;
    }
    case HealthEventSleepUpdate: {
      HealthActivityMask mask = health_service_peek_current_activities();
      s_state.sleeping = (mask & HealthActivitySleep) ||
                         (mask & HealthActivityRestfulSleep);
      journey_set_sleeping(s_state.sleeping);
      break;
    }
    case HealthEventMovementUpdate: {
      s_state.steps = (int32_t)health_service_sum_today(HealthMetricStepCount);
      journey_set_steps(s_state.steps);
      break;
    }
    default:
      break;
  }
}
#endif

void face_on_tap(AccelAxisType axis, int32_t direction) {
  if (!s_warm) return;
  if (physics_is_active()) return;  /* let an in-flight throw finish */

  /* Time-sampling rule: snapshot the wall clock once and physics_throw
   * settles all three dice on this exact time. If the minute advances
   * mid-throw the dice still land on the sampled values; the next
   * minute tick will then SHAKE them to the new value.
   *
   * Cancel any in-flight tumbles (hour QUICK or minute SHAKE) before
   * starting the throw — otherwise the tumble update would fight
   * physics for the rot_x/y/z fields. tumble_deinit triggers the
   * teardown, which physics_throw immediately overwrites with the
   * new target value and zero rotation. */
  tumble_deinit(&s_hour_tumble);
  tumble_deinit(&s_tens_tumble);
  tumble_deinit(&s_ones_tumble);

  time_t now = time(NULL);
  struct tm *tm_now = localtime(&now);
  int hour = tm_now->tm_hour % 12;
  if (hour == 0) hour = 12;
  int minute = tm_now->tm_min;

  physics_throw(hour, minute / 10, minute % 10);
}

void face_on_bluetooth(bool connected) {
  s_state.bluetooth = connected;
  layer_mark_dirty(s_ribbon_layer);
}

void face_on_weather(int16_t temp_c) {
  s_state.weather_temp = temp_c;
  layer_mark_dirty(s_info_layer);
}
