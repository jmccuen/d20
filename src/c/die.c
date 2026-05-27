/*
 * die.c — single entry point for die rendering.
 *
 * After Phase 2.5 step 3, settled and tumbling states both render through
 * dice3d.c: settled is just a tumble at zero net rotation. The flat
 * pentagon/kite primitives that previously drew the rest pose are gone —
 * they made the settled die look weaker than the same die mid-tumble, and
 * the visible quality drop at settle was the symptom.
 *
 * Sprite delivery (Phase 4) replaces this with graphics_draw_rotated_bitmap
 * against per-value resources. The single-function boundary stays.
 */

#include "die.h"
#include "dice3d.h"

void die_draw(GContext *ctx, const Die *die) {
  dice3d_draw(ctx, die);
}
