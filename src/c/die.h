/*
 * die.h — single die abstraction.
 *
 * die_draw routes to dice3d.c for both tumbling and settled states (the
 * settled draw is just a tumble at zero net rotation). Phase 4 swaps the
 * body for graphics_draw_rotated_bitmap against per-value sprites; no
 * other file changes.
 */

#pragma once

#include <pebble.h>

typedef enum {
  DIE_HOUR,   /* 1..12; rendered as a D12 dodecahedron        */
  DIE_TENS,   /* 0..5;  rendered as a D10 trapezohedron (00..50) */
  DIE_ONES,   /* 0..9;  rendered as a D10 trapezohedron (0..9)   */
} DieType;

typedef struct {
  GPoint   center;     /* layer-local coordinates */
  int16_t  radius;
  int16_t  value;      /* face value to display when settled */
  DieType  type;
  bool     flash;      /* one-frame palette swap on tumble settle  */
  bool     tumbling;   /* true → numeral cycles with front face;
                          false → numeral shows die->value         */
  int32_t  rot_x;      /* 3D Euler angles. Zero at settle.         */
  int32_t  rot_y;
  int32_t  rot_z;
} Die;

void die_draw(GContext *ctx, const Die *die);
