/*
 * face.h — watchface composition and event entry points.
 *
 * Owned by face.c. main.c calls face_init() / face_deinit() during window
 * load/unload and forwards every service callback to one of the face_on_*
 * functions below. face.c keeps the canonical FaceState and marks only the
 * affected layers dirty in response.
 */

#pragma once

#include <pebble.h>

void face_init(Window *window);
void face_deinit(void);

void face_on_tick(struct tm *tick_time, TimeUnits units_changed);
void face_on_battery(BatteryChargeState charge);

#if defined(PBL_HEALTH)
void face_on_health(HealthEventType event);
#endif

void face_on_tap(AccelAxisType axis, int32_t direction);
void face_on_bluetooth(bool connected);
