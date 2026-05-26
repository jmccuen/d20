/*
 * dice3d.h — procedural 3D polyhedron renderer for tumbling dice.
 *
 * During FULL / QUICK tumbles, die.c routes the draw call here. We
 * transform a small vertex table by an Euler-angle rotation matrix,
 * orthographically project to screen, backface-cull via 2D signed area,
 * flat-shade each visible face with a 4-step palette ramp, and draw the
 * face value as text at the projected centroid of the most front-facing
 * face.
 *
 * No 3D depth buffer needed: convex polyhedra with backface culling have
 * no overdraw between front-facing faces, so order of drawing doesn't
 * matter visually.
 *
 * Supported polyhedra (selected by die->type):
 *   DIE_HOUR        → regular dodecahedron (D12), 20 vertices, 12 faces
 *   DIE_TENS/ONES   → pentagonal trapezohedron (D10), 12 vertices, 10 faces
 *
 * The hard-cut to settled rendering happens in tumble.c when the
 * animation ends — it clears Die.tumbling and zeros rot_x/y/z, so the
 * next draw goes back to die.c's settled procedural pentagon/kite.
 */

#pragma once

#include <pebble.h>
#include "die.h"

void dice3d_draw(GContext *ctx, const Die *die);
