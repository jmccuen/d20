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

/* Segment count for face-value flicker. Value re-randomizes when the
 * progress segment index ticks over. Last segment always shows the
 * target value with flash highlight on. */
#define FULL_SEGMENTS   6
#define QUICK_SEGMENTS  4

/* Total spin during full / quick roll, in full turns. */
#define FULL_SPINS   3
#define QUICK_SPINS  2

/* Settle-shake wobble amplitude, in fixed-point Pebble angle units.
 * TRIG_MAX_ANGLE = 360°; this works out to ~11°. */
#define SHAKE_AMPL   (TRIG_MAX_ANGLE / 32)

static int16_t random_face_value(DieType type) {
  switch (type) {
    case DIE_HOUR: return 1 + (rand() % 12);
    case DIE_TENS: return rand() % 6;
    case DIE_ONES: return rand() % 10;
  }
  return 0;
}

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
    /* Damped sine wobble — amplitude shrinks linearly with progress so the
     * die quivers and settles. Face value is already the new target; only
     * rotation animates. */
    int32_t remaining = ANIMATION_NORMALIZED_MAX - p;
    int32_t ampl = (int64_t)SHAKE_AMPL * remaining / ANIMATION_NORMALIZED_MAX;
    int32_t phase = (int64_t)p * 4 * TRIG_MAX_ANGLE / ANIMATION_NORMALIZED_MAX;
    die->rotation = (int64_t)sin_lookup(phase) * ampl / TRIG_MAX_RATIO;
    die->value = h->target_value;
    die->flash = false;
  } else {
    int n_seg  = (h->kind == TUMBLE_FULL) ? FULL_SEGMENTS : QUICK_SEGMENTS;
    int spins  = (h->kind == TUMBLE_FULL) ? FULL_SPINS    : QUICK_SPINS;

    die->rotation = (int64_t)p * spins * TRIG_MAX_ANGLE / ANIMATION_NORMALIZED_MAX;

    int seg = (int)((int64_t)p * n_seg / ANIMATION_NORMALIZED_MAX);
    if (seg >= n_seg - 1) {
      /* Final segment: lock to target value and flash the body. The
       * progress curve is ease-out, so this segment is the longest of
       * the animation in wall time — the flash reads as a deliberate
       * "click" on settle. */
      die->value = h->target_value;
      die->flash = true;
    } else if (seg != h->last_segment) {
      h->last_segment = seg;
      die->value = random_face_value(die->type);
      die->flash = false;
    }
  }

  layer_mark_dirty(h->layer);
}

static void tumble_teardown(Animation *anim) {
  TumbleHandle *h = (TumbleHandle *)animation_get_context(anim);
  if (!h || !h->die || !h->layer) return;
  h->die->value    = h->target_value;
  h->die->rotation = 0;
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
