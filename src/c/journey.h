/*
 * journey.h — the dashed trail along the bottom of the watchface.
 *
 * Contains the campfire (trailhead, also doubles as sleep marker), midpoint
 * treasure, end-of-trail boss sigil, and the moving adventurer token whose
 * position is steps/goal of the trail length.
 */

#pragma once

#include <pebble.h>

void journey_draw(GContext *ctx, GRect bounds,
                  int32_t steps, int32_t goal, bool sleeping);
