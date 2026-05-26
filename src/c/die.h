/*
 * die.h — abstraction over how a single die is rendered on screen.
 *
 * Phase 1 (this scaffold): die_draw() builds the die body from polygons
 * and overlays an upright system-font numeral.
 *
 * Phase 2: replace the body of die_draw() with a single call to
 * graphics_draw_rotated_bitmap() against a per-value sprite resource. No
 * other file needs to change.
 */

#pragma once

#include <pebble.h>

typedef enum {
  DIE_HOUR,   /* 1..12; rendered as a pentagon (D12 / stylized D20) */
  DIE_TENS,   /* 0..5;  rendered as a kite     (D10 tens 00..50)    */
  DIE_ONES,   /* 0..9;  rendered as a kite     (D10 ones 0..9)      */
} DieType;

typedef struct {
  GPoint   center;     /* in layer-local coordinates */
  int16_t  radius;
  int16_t  value;      /* settled face value */
  int32_t  rotation;   /* legacy 2D rotation, used by TUMBLE_SHAKE        */
  DieType  type;
  bool     flash;      /* one-frame palette swap on tumble settle         */
  bool     tumbling;   /* when true, die_draw routes to dice3d (3D)       */
  int32_t  rot_x;      /* 3D Euler angles, used during FULL/QUICK tumble  */
  int32_t  rot_y;
  int32_t  rot_z;
} Die;

void die_draw(GContext *ctx, const Die *die);
