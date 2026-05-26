/*
 * tumble.c — Pebble Animation glue for the dice.
 *
 * The custom AnimationImplementation lives once at file scope; the
 * per-die state (target value, current segment, etc.) hangs off each
 * TumbleHandle and is reached through animation_get_context().
 *
 * Each Animation is auto-destroyed when stopped (Pebble default), so the
 * teardown handler is the canonical "settle" point: it forces the die to
 * the target value, clears rotation and flash, and nulls h->anim.
 */

#include "tumble.h"

#define DUR_FULL_MS    600
#define DUR_QUICK_MS   400
#define DUR_SHAKE_MS   100

/* FULL/QUICK now drive a 3D polyhedron via dice3d.c. The per-axis spin
 * counts are integers so progress=1 lands at a multiple of
 * TRIG_MAX_ANGLE — i.e., the die settles at zero net rotation. That's
 * what makes the hard-cut to die.c's flat settled rendering invisible.
 *
 * Different rates per axis keep the tumble chaotic. The hour die
 * (TUMBLE_QUICK) gets fewer total turns so the animation reads faster. */
#define FULL_SPINS_X   2
#define FULL_SPINS_Y   3
#define FULL_SPINS_Z   1

#define QUICK_SPINS_X  1
#define QUICK_SPINS_Y  2
#define QUICK_SPINS_Z  1

/* Settle-shake wobble amplitude, in fixed-point Pebble angle units.
 * TRIG_MAX_ANGLE = 360°; this works out to ~11°. */
#define SHAKE_AMPL   (TRIG_MAX_ANGLE / 32)

/* --- AnimationImplementation hooks ------------------------------------- */

static void tumble_setup(Animation *anim) {
  TumbleHandle *h = (TumbleHandle *)animation_get_context(anim);
  if (h) h->last_segment = -1;
}

static void tumble_update(Animation *anim, const AnimationProgress p) {
  TumbleHandle *h = (TumbleHandle *)animation_get_context(anim);
  if (!h || !h->die || !h->layer) return;
  Die *die = h->die;

  if (h->kind == TUMBLE_SHAKE) {
    /* Damped sine wobble in 2D — the die is "already settled" and just
     * quivers to the new value. dice3d isn't engaged for SHAKE. */
    int32_t remaining = ANIMATION_NORMALIZED_MAX - p;
    int32_t ampl  = (int64_t)SHAKE_AMPL * remaining / ANIMATION_NORMALIZED_MAX;
    int32_t phase = (int64_t)p * 4 * TRIG_MAX_ANGLE / ANIMATION_NORMALIZED_MAX;
    die->rotation = (int64_t)sin_lookup(phase) * ampl / TRIG_MAX_RATIO;
    die->value    = h->target_value;
    die->tumbling = false;
    die->flash    = false;
  } else {
    /* FULL/QUICK: 3D polyhedron tumble. Drive Euler angles directly;
     * dice3d.c reads them in die_draw. Per-axis spin counts are integer
     * so progress=1 lands at a multiple of TRIG_MAX_ANGLE — the
     * polyhedron's orientation is zero net at settle, matching what the
     * flat settled rendering depicts. */
    int sx, sy, sz;
    if (h->kind == TUMBLE_FULL) {
      sx = FULL_SPINS_X; sy = FULL_SPINS_Y; sz = FULL_SPINS_Z;
    } else {
      sx = QUICK_SPINS_X; sy = QUICK_SPINS_Y; sz = QUICK_SPINS_Z;
    }
    die->rot_x    = (int64_t)p * sx * TRIG_MAX_ANGLE / ANIMATION_NORMALIZED_MAX;
    die->rot_y    = (int64_t)p * sy * TRIG_MAX_ANGLE / ANIMATION_NORMALIZED_MAX;
    die->rot_z    = (int64_t)p * sz * TRIG_MAX_ANGLE / ANIMATION_NORMALIZED_MAX;
    die->tumbling = true;
    /* Face value during tumble comes from whichever face is front-facing
     * in dice3d — die->value is left alone until teardown. */
  }

  layer_mark_dirty(h->layer);
}

static void tumble_teardown(Animation *anim) {
  TumbleHandle *h = (TumbleHandle *)animation_get_context(anim);
  if (!h || !h->die || !h->layer) return;
  h->die->value    = h->target_value;
  h->die->rotation = 0;
  h->die->rot_x    = 0;
  h->die->rot_y    = 0;
  h->die->rot_z    = 0;
  h->die->tumbling = false;
  h->die->flash    = false;
  h->anim          = NULL;
  layer_mark_dirty(h->layer);
}

static const AnimationImplementation s_impl = {
  .setup    = tumble_setup,
  .update   = tumble_update,
  .teardown = tumble_teardown,
};

/* --- Public API -------------------------------------------------------- */

void tumble_init(TumbleHandle *h, Die *die, Layer *layer) {
  h->die           = die;
  h->layer         = layer;
  h->kind          = TUMBLE_SHAKE;
  h->target_value  = die->value;
  h->last_segment  = -1;
  h->anim          = NULL;
}

void tumble_deinit(TumbleHandle *h) {
  if (h->anim) {
    animation_unschedule(h->anim);
    h->anim = NULL;
  }
}

void tumble_start(TumbleHandle *h, TumbleKind kind, int16_t target_value) {
  if (h->anim) {
    animation_unschedule(h->anim);
    /* teardown nulls h->anim and snaps the die to the *old* target_value.
     * That's overwritten below before the new animation starts updating. */
  }

  h->kind         = kind;
  h->target_value = target_value;
  h->last_segment = -1;

  uint32_t dur;
  switch (kind) {
    case TUMBLE_FULL:  dur = DUR_FULL_MS;  break;
    case TUMBLE_QUICK: dur = DUR_QUICK_MS; break;
    case TUMBLE_SHAKE: dur = DUR_SHAKE_MS; break;
    default:           dur = DUR_SHAKE_MS; break;
  }

  Animation *anim = animation_create();
  animation_set_implementation(anim, &s_impl);
  animation_set_handlers(anim, (AnimationHandlers){
    .started = NULL,
    .stopped = NULL,
  }, h);
  animation_set_duration(anim, dur);
  animation_set_curve(anim, AnimationCurveEaseOut);

  h->anim = anim;
  animation_schedule(anim);
}
