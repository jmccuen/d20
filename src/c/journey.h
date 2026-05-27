/*
 * journey.h — the dashed trail along the bottom of the watchface.
 *
 * Contains the campfire (trailhead, also doubles as sleep marker), midpoint
 * treasure, end-of-trail boss sigil, and the moving adventurer token whose
 * position is steps/goal along the trail.
 *
 * Phase 2 adds:
 *   - Smooth token slide between step updates (no more teleport).
 *   - Walk-to-camp / depart-camp slides on sleep state transitions.
 *   - A "Xh Ym rest" label under the camp for ~2 h after waking.
 *
 * journey.c owns a single module-scope state struct. face.c calls
 * journey_init() once, journey_set_*() when inputs change, and
 * journey_draw() from the layer update_proc.
 */

#pragma once

#include <pebble.h>

void journey_init(Layer *layer, int32_t step_goal);
void journey_deinit(void);

void journey_set_steps(int32_t steps);
void journey_set_goal(int32_t goal);
void journey_set_sleeping(bool sleeping);

/* Today's total sleep duration in seconds, summed from HealthService.
 * Drives the "6.2h" label under the campfire. 0 means no sleep data
 * yet (rendered as an em-dash). */
void journey_set_sleep_seconds(int32_t seconds);

/* Draws the info-area content (sine-wave trail, camp + sleep hours,
 * cloud + temp, mage on trail, step count). Heart and torch are drawn
 * separately by widgets_draw_heart / widgets_draw_torch which the
 * caller invokes on top of this. */
void journey_draw(GContext *ctx, GRect bounds,
                  int16_t weather_temp, bool weather_is_f);
