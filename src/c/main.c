/*
 * main.c — app lifecycle, service subscriptions, callback shims.
 *
 * This file owns the connection between PebbleOS services and the watchface
 * state in face.c. It should stay thin: subscribe, forward, unsubscribe.
 */

#include <pebble.h>
#include "face.h"

static Window *s_window;

/* --- Service callbacks (thin shims) ------------------------------------- */

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  face_on_tick(tick_time, units_changed);
}

static void battery_handler(BatteryChargeState charge) {
  face_on_battery(charge);
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
  face_on_health(event);
}
#endif

/* accel_tap is a stand-in for "wrist-raise / tap-to-roll" until Phase 1
 * upgrades to a continuous accel_data subscription with proper gesture
 * detection. */
static void tap_handler(AccelAxisType axis, int32_t direction) {
  face_on_tap(axis, direction);
}

static void connection_handler(bool connected) {
  face_on_bluetooth(connected);
}

/* AppMessage inbox — pkjs pushes weather via WEATHER_TEMP. */
static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *temp_t = dict_find(iter, MESSAGE_KEY_WEATHER_TEMP);
  if (temp_t) {
    face_on_weather((int16_t)temp_t->value->int32);
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "AppMessage dropped: %d", reason);
}

/* --- Window lifecycle --------------------------------------------------- */

static void prv_window_load(Window *window) {
  face_init(window);
}

static void prv_window_unload(Window *window) {
  face_deinit();
}

/* --- App init / deinit -------------------------------------------------- */

static void prv_init(void) {
  APP_LOG(APP_LOG_LEVEL_INFO, "boot: heap %u free",
          (unsigned)heap_bytes_free());
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load   = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  accel_tap_service_subscribe(tap_handler);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = connection_handler,
  });

#if defined(PBL_HEALTH)
  if (!health_service_events_subscribe(health_handler, NULL)) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "HealthService subscription failed");
  }
#endif

  /* AppMessage subscription for weather updates from pkjs. Small
   * buffers — we currently send a single int per message. */
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_open(64, 64);

  /* Push current state so layers render with real data on first paint. */
  face_on_battery(battery_state_service_peek());
  face_on_bluetooth(connection_service_peek_pebble_app_connection());

  time_t now = time(NULL);
  struct tm *tm_now = localtime(&now);
  face_on_tick(tm_now, MINUTE_UNIT | HOUR_UNIT | DAY_UNIT);
}

static void prv_deinit(void) {
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  connection_service_unsubscribe();
  accel_tap_service_unsubscribe();
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
