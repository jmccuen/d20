/*
 * tumble.h — per-die animation state.
 *
 * A TumbleHandle binds a Die + its Layer to a Pebble Animation. While the
 * animation is scheduled, an internal update procedure drives Die.rotation
 * (spin + ease-out settle), Die.value (face-value flicker), and Die.flash
 * (one-frame palette highlight on settle), marking the layer dirty each
 * tick.
 *
 * Three tumble kinds, each with its own duration / spin count / flicker
 * density:
 *   TUMBLE_FULL  — ~600 ms ceremonial roll for wrist-raise, tap, hour tick
 *   TUMBLE_QUICK — ~400 ms re-roll for the hour die at XX:00
 *   TUMBLE_SHAKE — ~100 ms settle shake on per-minute changes
 *
 * Time-sampling rule: tumble_start() snapshots the target face value at
 * call time. If the wall clock advances mid-animation the die still
 * settles on the snapshot value — no skew between displayed and actual
 * time when the animation ends.
 */

#pragma once

#include <pebble.h>
#include "die.h"

typedef enum {
  TUMBLE_FULL,
  TUMBLE_QUICK,
  TUMBLE_SHAKE,
} TumbleKind;

typedef struct {
  Die        *die;
  Layer      *layer;
  TumbleKind  kind;
  int16_t     target_value;
  int8_t      last_segment;
  Animation  *anim;
} TumbleHandle;

void tumble_init(TumbleHandle *h, Die *die, Layer *layer);
void tumble_deinit(TumbleHandle *h);

/* Cancels any in-flight animation on this handle and schedules a new one.
 * target_value is the face the die should display when the tumble settles.
 * delay_ms holds the tumble before the spin actually starts — used by
 * the cascading tap roll so the lighter dice fire first and the heavy
 * hour die fires last. */
void tumble_start(TumbleHandle *h, TumbleKind kind,
                  int16_t target_value, uint32_t delay_ms);
