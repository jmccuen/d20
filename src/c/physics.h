/*
 * physics.h — single-shot dice-tray physics for the tap-roll.
 *
 * Replaces the three independent TUMBLE_FULL animations that previously
 * fired on tap. Now a single animation drives all three dice through a
 * coordinated throw:
 *
 *   - At start: each die gets a random linear velocity + angular
 *     velocity. die->value is snapped to the new target up-front so the
 *     per-value rest rotation in dice3d_draw reflects the new face
 *     immediately.
 *   - Phase 1 (0..70%): free flight. Position integrates velocity each
 *     frame, rotation integrates angular velocity. Damping decays both.
 *     Wall and die-die collisions reflect velocities.
 *   - Phase 2 (70..100%): return to home. Position lerps toward rest_xy
 *     and rotation lerps toward zero so the polyhedron settles at the
 *     per-value rest pose with the correct face dead-on.
 *
 * Hour-tick re-rolls (TUMBLE_QUICK) and minute-tick shakes (TUMBLE_SHAKE)
 * continue to use tumble.c — physics is only for the tap event.
 */

#pragma once

#include <pebble.h>
#include "die.h"

void physics_init(Layer *stage_layer,
                  Die *hour, Die *tens, Die *ones,
                  GPoint hour_home, GPoint tens_home, GPoint ones_home,
                  GRect tray_bounds);
void physics_deinit(void);

/* Kicks off the throw. Each die's value is set to *_value before the
 * simulation starts. Random seeds are pulled from the system RNG. */
void physics_throw(int16_t hour_value, int16_t tens_value, int16_t ones_value);

/* True while the throw is in progress. face_on_tap consults this to
 * suppress re-trigger while a throw is still settling. */
bool physics_is_active(void);
