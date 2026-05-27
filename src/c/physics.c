/*
 * physics.c — implementation of the tap-roll throw.
 *
 * Position and velocity use a 256× fixed-point scale so per-frame damping
 * (multiplication by ~0.93) doesn't quantize sub-pixel velocities to zero
 * on the first frame. Rotation uses Pebble's TRIG_MAX_ANGLE units, with
 * angular velocity in TRIG_MAX_ANGLE units per frame.
 *
 * The animation is one Pebble Animation with linear curve. Pebble drives
 * the update handler each frame; we treat the update as a fixed-step
 * physics tick. No sub-stepping — the dice are big enough relative to
 * the tray that simple per-frame integration handles wall and die-die
 * collisions cleanly.
 *
 * Die-die collisions use a coarse 1D elastic-collision approximation
 * along the contact normal: compute the (dx, dy) between centers,
 * normalize via float sqrt, project relative velocity onto that normal,
 * and exchange the normal component (equal mass). The tangential
 * component is preserved. Bodies are pushed apart by the overlap
 * distance so they're non-overlapping on the next frame.
 */

#include "physics.h"
#include <stdlib.h>
#include <math.h>

#define PHYSICS_DURATION_MS  1500
#define RETURN_PHASE_PCT       70
#define MAX_BODIES              3

/* Position is stored as int32_t with the lower 8 bits as fractional pixels.
 * POS_SCALE is 1 px in that representation. */
#define POS_SCALE  256

/* Per-frame linear damping. v *= LIN_DAMP_N / 256. */
#define LIN_DAMP_N  238  /* ≈ 0.93 */

/* Per-frame angular damping. */
#define ANG_DAMP_N  230  /* ≈ 0.90 — rotation decays a touch faster than translation */

/* Return-phase step. Each frame: pos += (rest - pos) * RET_STEP / 256;
 * rot *= (256 - RET_STEP) / 256. After ~13 frames of phase 2 (≈450 ms),
 * position is within ~2% of rest and rotation is effectively zero. */
#define RET_STEP    64   /* 1/4 per frame */

/* Throw velocity ranges. Stored in 256× units so 256 = 1 px/frame.
 * VEL_MIN guards against a die getting a near-zero initial velocity and
 * looking like it didn't get thrown. */
#define VEL_MAX     900
#define VEL_MIN     300

/* Angular velocity ranges in TRIG_MAX_ANGLE units/frame. 3000 ≈ 16°/frame
 * ≈ half a turn per second at 30 fps. */
#define ANG_VEL_MAX 3500
#define ANG_VEL_MIN 1500

typedef struct {
  Die    *die;
  int16_t rest_x, rest_y;
  int32_t pos_x, pos_y;   /* 256× */
  int32_t vx, vy;         /* 256× px/frame */
  int32_t wx, wy, wz;     /* TRIG_MAX_ANGLE units/frame */
} Body;

static Body      s_bodies[MAX_BODIES];
static Layer    *s_stage_layer;
static GRect     s_bounds;
static Animation *s_anim;
static bool      s_seeded;

/* --- Helpers ------------------------------------------------------------ */

static int32_t rand_in(int32_t lo, int32_t hi) {
  return lo + (rand() % (hi - lo + 1));
}

/* Random non-zero throw velocity component: ±[VEL_MIN..VEL_MAX]. */
static int32_t rand_throw_vel(void) {
  int32_t mag = rand_in(VEL_MIN, VEL_MAX);
  return (rand() & 1) ? mag : -mag;
}

static int32_t rand_throw_ang(void) {
  int32_t mag = rand_in(ANG_VEL_MIN, ANG_VEL_MAX);
  return (rand() & 1) ? mag : -mag;
}

/* --- Per-frame physics ------------------------------------------------- */

static void integrate_and_walls(Body *b) {
  b->pos_x += b->vx;
  b->pos_y += b->vy;

  /* Wall collisions: clamp center to (bounds + radius) and reflect. */
  int16_t r = b->die->radius;
  int32_t min_x = (int32_t)(s_bounds.origin.x + r) * POS_SCALE;
  int32_t max_x = (int32_t)(s_bounds.origin.x + s_bounds.size.w  - r) * POS_SCALE;
  int32_t min_y = (int32_t)(s_bounds.origin.y + r) * POS_SCALE;
  int32_t max_y = (int32_t)(s_bounds.origin.y + s_bounds.size.h - r) * POS_SCALE;

  if (b->pos_x < min_x) { b->pos_x = min_x; b->vx = -b->vx; }
  if (b->pos_x > max_x) { b->pos_x = max_x; b->vx = -b->vx; }
  if (b->pos_y < min_y) { b->pos_y = min_y; b->vy = -b->vy; }
  if (b->pos_y > max_y) { b->pos_y = max_y; b->vy = -b->vy; }

  /* Linear damping. */
  b->vx = b->vx * LIN_DAMP_N / 256;
  b->vy = b->vy * LIN_DAMP_N / 256;

  /* Rotation. Pebble's sin/cos lookups handle large/negative angles, so
   * we don't need to wrap rot_x/y/z explicitly. */
  b->die->rot_x += b->wx;
  b->die->rot_y += b->wy;
  b->die->rot_z += b->wz;
  b->wx = b->wx * ANG_DAMP_N / 256;
  b->wy = b->wy * ANG_DAMP_N / 256;
  b->wz = b->wz * ANG_DAMP_N / 256;
}

static void resolve_die_die(Body *a, Body *b) {
  int32_t ax = a->pos_x / POS_SCALE;
  int32_t ay = a->pos_y / POS_SCALE;
  int32_t bx = b->pos_x / POS_SCALE;
  int32_t by = b->pos_y / POS_SCALE;
  int32_t dx = bx - ax;
  int32_t dy = by - ay;
  int32_t r_sum = a->die->radius + b->die->radius;
  int32_t dist_sq = dx * dx + dy * dy;
  if (dist_sq >= r_sum * r_sum || dist_sq == 0) return;

  /* Normalize via float sqrt — Pebble Time 2 has FP, and three pairs
   * per frame is negligible cost. */
  float dist = sqrtf((float)dist_sq);
  if (dist < 1.0f) dist = 1.0f;
  float inv_dist = 1.0f / dist;
  float nx = dx * inv_dist;
  float ny = dy * inv_dist;

  /* Push apart so they're not overlapping next frame — half the overlap
   * to each body, in 256× units. */
  float overlap = (float)r_sum - dist;
  int32_t push_x = (int32_t)(nx * overlap * (POS_SCALE / 2));
  int32_t push_y = (int32_t)(ny * overlap * (POS_SCALE / 2));
  a->pos_x -= push_x;
  a->pos_y -= push_y;
  b->pos_x += push_x;
  b->pos_y += push_y;

  /* Exchange the normal component of velocity (equal mass, elastic).
   * Tangential component is preserved. */
  float avn = a->vx * nx + a->vy * ny;
  float bvn = b->vx * nx + b->vy * ny;
  if (bvn - avn >= 0) return;  /* already separating */

  int32_t da = (int32_t)((bvn - avn) * nx);
  int32_t db = (int32_t)((avn - bvn) * nx);
  a->vx += da;
  b->vx += db;
  da = (int32_t)((bvn - avn) * ny);
  db = (int32_t)((avn - bvn) * ny);
  a->vy += da;
  b->vy += db;
}

static void lerp_toward_home(Body *b) {
  int32_t target_x = (int32_t)b->rest_x * POS_SCALE;
  int32_t target_y = (int32_t)b->rest_y * POS_SCALE;
  b->pos_x += (target_x - b->pos_x) * RET_STEP / 256;
  b->pos_y += (target_y - b->pos_y) * RET_STEP / 256;

  /* Decay rotation toward zero. */
  b->die->rot_x = b->die->rot_x * (256 - RET_STEP) / 256;
  b->die->rot_y = b->die->rot_y * (256 - RET_STEP) / 256;
  b->die->rot_z = b->die->rot_z * (256 - RET_STEP) / 256;

  /* Stop integrating velocity in phase 2 — the lerp takes over. Damp
   * residual angular velocity hard so it doesn't keep pushing rotation
   * away from zero. */
  b->vx = 0;
  b->vy = 0;
  b->wx = b->wx * (256 - RET_STEP) / 256;
  b->wy = b->wy * (256 - RET_STEP) / 256;
  b->wz = b->wz * (256 - RET_STEP) / 256;
}

/* --- Animation hooks --------------------------------------------------- */

static void anim_setup(Animation *anim) { (void)anim; }

static void anim_update(Animation *anim, AnimationProgress p) {
  (void)anim;
  int32_t pct = (int32_t)p * 100 / ANIMATION_NORMALIZED_MAX;

  for (int i = 0; i < MAX_BODIES; i++) {
    Body *b = &s_bodies[i];
    if (!b->die) continue;

    if (pct < RETURN_PHASE_PCT) {
      integrate_and_walls(b);
    } else {
      lerp_toward_home(b);
    }
  }

  if (pct < RETURN_PHASE_PCT) {
    for (int i = 0; i < MAX_BODIES; i++) {
      for (int j = i + 1; j < MAX_BODIES; j++) {
        if (s_bodies[i].die && s_bodies[j].die) {
          resolve_die_die(&s_bodies[i], &s_bodies[j]);
        }
      }
    }
  }

  /* Push integrated state back to the Die structs. */
  for (int i = 0; i < MAX_BODIES; i++) {
    Body *b = &s_bodies[i];
    if (!b->die) continue;
    b->die->center.x = (int16_t)(b->pos_x / POS_SCALE);
    b->die->center.y = (int16_t)(b->pos_y / POS_SCALE);
  }

  layer_mark_dirty(s_stage_layer);
}

static void anim_teardown(Animation *anim) {
  (void)anim;
  /* Snap to exact rest pose so the static face has clean values to display. */
  for (int i = 0; i < MAX_BODIES; i++) {
    Body *b = &s_bodies[i];
    if (!b->die) continue;
    b->die->center.x = b->rest_x;
    b->die->center.y = b->rest_y;
    b->die->rot_x = 0;
    b->die->rot_y = 0;
    b->die->rot_z = 0;
    b->pos_x = (int32_t)b->rest_x * POS_SCALE;
    b->pos_y = (int32_t)b->rest_y * POS_SCALE;
    b->vx = 0;
    b->vy = 0;
    b->wx = 0;
    b->wy = 0;
    b->wz = 0;
  }
  s_anim = NULL;
  layer_mark_dirty(s_stage_layer);
}

static const AnimationImplementation s_impl = {
  .setup    = anim_setup,
  .update   = anim_update,
  .teardown = anim_teardown,
};

/* --- Public API -------------------------------------------------------- */

void physics_init(Layer *stage_layer,
                  Die *hour, Die *tens, Die *ones,
                  GPoint hour_home, GPoint tens_home, GPoint ones_home,
                  GRect tray_bounds) {
  s_stage_layer = stage_layer;
  s_bounds      = tray_bounds;

  s_bodies[0] = (Body){
    .die = hour, .rest_x = hour_home.x, .rest_y = hour_home.y,
    .pos_x = (int32_t)hour_home.x * POS_SCALE,
    .pos_y = (int32_t)hour_home.y * POS_SCALE,
  };
  s_bodies[1] = (Body){
    .die = tens, .rest_x = tens_home.x, .rest_y = tens_home.y,
    .pos_x = (int32_t)tens_home.x * POS_SCALE,
    .pos_y = (int32_t)tens_home.y * POS_SCALE,
  };
  s_bodies[2] = (Body){
    .die = ones, .rest_x = ones_home.x, .rest_y = ones_home.y,
    .pos_x = (int32_t)ones_home.x * POS_SCALE,
    .pos_y = (int32_t)ones_home.y * POS_SCALE,
  };

  if (!s_seeded) {
    srand((unsigned)time(NULL));
    s_seeded = true;
  }
  s_anim = NULL;
}

void physics_deinit(void) {
  if (s_anim) {
    animation_unschedule(s_anim);
    s_anim = NULL;
  }
}

void physics_throw(int16_t hour_value, int16_t tens_value, int16_t ones_value) {
  if (s_anim) {
    animation_unschedule(s_anim);
    /* teardown will fire from unschedule, resetting state. */
  }

  /* Set values up-front so dice3d_draw's per-value rest rotation lines
   * up with the new target from the start of the throw. */
  s_bodies[0].die->value = hour_value;
  s_bodies[1].die->value = tens_value;
  s_bodies[2].die->value = ones_value;

  for (int i = 0; i < MAX_BODIES; i++) {
    Body *b = &s_bodies[i];
    b->pos_x = (int32_t)b->rest_x * POS_SCALE;
    b->pos_y = (int32_t)b->rest_y * POS_SCALE;
    b->vx = rand_throw_vel();
    b->vy = rand_throw_vel();
    b->wx = rand_throw_ang();
    b->wy = rand_throw_ang();
    b->wz = rand_throw_ang();
    b->die->rot_x = 0;
    b->die->rot_y = 0;
    b->die->rot_z = 0;
  }

  Animation *anim = animation_create();
  animation_set_implementation(anim, &s_impl);
  animation_set_handlers(anim, (AnimationHandlers){ .started = NULL, .stopped = NULL }, NULL);
  animation_set_duration(anim, PHYSICS_DURATION_MS);
  animation_set_curve(anim, AnimationCurveLinear);
  s_anim = anim;
  animation_schedule(anim);
}

bool physics_is_active(void) {
  return s_anim != NULL;
}
