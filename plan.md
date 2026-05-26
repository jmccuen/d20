# D&D Dice Watchface — Development Plan

## Overview

A Pebble Time 2 watchface where the time is displayed via animated polyhedral dice in a TTRPG / parchment aesthetic, with diegetic indicators for heart rate (HP), steps (journey trail with adventurer token), date (parchment ribbon), battery (torch), and Bluetooth (familiar).

See `dnd_dice_watchface_brief.md` for the full visual brief.

## Target platform

- **Device:** Pebble Time 2 (Core Devices)
- **Display:** 200×228 px, 64 colors, memory LCD / transflective
- **SDK:** PebbleOS open-source SDK, C entry points, optional PebbleKit JS for settings
- **Platform key in `package.json`:** `emery` (verify against the current SDK before first build)
- **Sensors used:** heart rate, accelerometer (wrist-raise + taps), step counter, sleep tracker
- **Companion:** Pebble companion app for configuration

## Architecture

A single-window watchface. Each visual element is its own `Layer` with a dedicated `update_proc`, marked dirty only when its underlying state changes. The dispatcher in `face.c` owns the canonical `FaceState` and is updated from service callbacks shimmed in `main.c`.

```
src/c/
├── main.c        — app lifecycle, service subscriptions, callback shims
├── face.h/.c     — FaceState, layer composition, mark-dirty dispatch
├── die.h/.c      — Die abstraction; phase-1 procedural, phase-2 sprite swap
├── journey.h/.c  — journey strip: trail, milestones, adventurer token, sleep mode
└── widgets.h/.c  — date ribbon, HP heart, torch battery, familiar
```

Die rendering is intentionally abstracted behind `die_draw(ctx, &die)` so the phase-2 sprite swap is a single-file change with no other code touched.

## Development phases

### Phase 0 — Scaffold (this commit)

- [x] `pebble new-project` baseline + module split
- [x] Window, tick service, battery service, accel tap, connection service
- [x] `HealthService` subscription for HR, steps, sleep (guarded by `PBL_HEALTH`)
- [x] Procedural die rendering (pentagon hour die, kite minute dice, upright numerals)
- [x] Procedural journey strip with camp / chest / boss / token
- [x] Procedural date ribbon and stat row (heart + torch)
- [x] Familiar placeholder (filled dot on the ribbon)
- [x] Builds in `pebble build` and runs in `pebble install --emulator emery`

### Phase 1 — Procedural feature-complete

Every behaviour working before any art is committed.

- [ ] **Wrist-raise** detection → triggers full ceremonial tumble of all three dice
- [ ] **Hour change** → hour die alone re-rolls (~400 ms)
- [ ] **Minute change** → 100 ms settle-shake on the changed die only (no full tumble)
- [ ] **Tap** (touchscreen) → ceremonial roll (easter egg)
- [ ] **Tumble animation:** rotation + flicker (random face value every 2–3 frames) + settle
- [ ] **Flash highlight** on settle (palette-swap one frame)
- [ ] **Sleep mode:** token returns to camp, dashed trail rendered at half opacity, flame above camp, "Z" above token
- [ ] **Wake transition:** "8h 12m rest" label fades in below camp for ~2 h after waking
- [ ] **Low-battery torch:** flame scales by battery — full / half / embers / dark wick
- [ ] **Bluetooth disconnect:** familiar removed or rendered as faded outline

### Phase 2 — Stub-sprite pipeline check

A single tracer-bullet asset to de-risk the swap path.

- [ ] Bake one placeholder PNG into `resources/` (e.g., flat gray pentagon with an "X")
- [ ] Wire it through `package.json` → `gbitmap_create_with_resource`
- [ ] Confirm `graphics_draw_rotated_bitmap` renders correctly during a tumble
- [ ] Confirm resource memory budget is reasonable

### Phase 3 — Full sprite delivery

- [ ] 12 hour-die sprites, values 1–12, ~90×90 px each
- [ ] 6 tens-die sprites: 00, 10, 20, 30, 40, 50 at ~56×56 px
- [ ] 10 ones-die sprites: 0–9 at ~56×56 px
- [ ] Swap `die_draw` body to call `graphics_draw_rotated_bitmap`
- [ ] Strip the polygon math from `die.c`
- [ ] Verify resource-bundle size and runtime memory

### Phase 4 — Configurables & polish

- [ ] Settings page (PebbleKit JS) with: adventurer class, familiar, daily step goal, sleep override
- [ ] Six class silhouettes (~8×10 px) drawn on the journey token
- [ ] Familiar art for 10 creatures, ribbon-corner placement
- [ ] RGB backlight tinting (warm orange after local sunset)
- [ ] Edge-case audit (DST, midnight rollover, sleep crossing midnight, no HR sensor data, no step data)
- [ ] Battery-life measurement: target < 5% drain attributable to the watchface

## Open questions

1. **Die geometry vs. art direction:** keep the D20 silhouette for the hour die (per current mockup) or correct to D12? Either is supported by the scaffold; the pentagon stand-in works for both.
2. **Familiar position:** ribbon corner (current scaffold) or stat-row center? Decide after seeing the final familiar art.
3. **Wrist-raise sensitivity:** Pebble's `accel_tap_service` fires on sharp gestures; a true wrist-raise detector likely needs a custom `accel_data_service` consumer with a low-pass filter on the gravity vector. Defer to Phase 1.
4. **HP framing:** raw BPM as "72 HP" (current brief) vs. "72 / max" with a thematic max. Confirm after on-wrist testing.
5. **Sleep-end label timing:** show "Xh Ym rest" for 2 h after waking, or until first user interaction? Test both.
6. **Color palette mapping:** Pebble's 64-color fixed palette won't reproduce the brief's exact hex values (`#F0DFB4`, etc.). Use `GColorPastelYellow` / `GColorRajah` / `GColorWindsorTan` as the closest stand-ins and tune by eye in the emulator.

## SDK reference (cheat sheet)

| Need                       | API |
| -------------------------- | --- |
| Per-minute redraw          | `tick_timer_service_subscribe(MINUTE_UNIT, …)` |
| Battery                    | `battery_state_service_subscribe(…)` / `battery_state_service_peek()` |
| Heart rate / steps / sleep | `health_service_events_subscribe(…)`, `health_service_peek_current_value(HealthMetricHeartRateBPM)`, `health_service_sum_today(HealthMetricStepCount)`, `health_service_peek_current_activities()` |
| Tap (proxy for wrist-raise during scaffold) | `accel_tap_service_subscribe(…)` |
| Continuous accel (for real wrist-raise)     | `accel_data_service_subscribe(…)` |
| Bluetooth connection       | `connection_service_subscribe(…)` / `connection_service_peek_pebble_app_connection()` |
| Rotate a bitmap            | `graphics_draw_rotated_bitmap(ctx, bmp, src_ic, angle, dest_ic)` |
| Fixed-point trig           | `sin_lookup`, `cos_lookup`, `TRIG_MAX_ANGLE`, `TRIG_MAX_RATIO` |
| Polygons                   | `gpath_create`, `gpath_draw_filled`, `gpath_draw_outline` |
| Animation                  | `animation_create()`, `animation_set_handlers`, `animation_set_duration`, `animation_schedule` |

## Build & run

```bash
# from a fresh checkout
pebble build
pebble install --emulator emery
pebble logs --emulator emery
```

To start from a brand new project instead of this scaffold:

```bash
pebble new-project dnd-dice
# then copy the files in src/c/ and the contents of package.json over
```

## Performance budget (preliminary)

| Pass                         | Frequency             | Cost note |
| ---------------------------- | --------------------- | --------- |
| Minute tick                  | 1× / minute           | Cheap — mostly text + dashed line redraw |
| Hour tick                    | 1× / hour             | Hour die re-roll (~400 ms animation, 12 frames) |
| Wrist-raise roll             | ~50× / day (estimate) | Full tumble (~600 ms, 18 frames). Only fires when looking. |
| Heart rate update            | every 5–10 min        | Stat row redraw only |
| Battery state push           | on change             | Stat row redraw only |
| Steps polling                | 1× / minute           | Journey strip redraw only |
| Sleep state push             | on change             | Journey strip redraw only |

Target: no full-screen redraws at idle, no animation when the watch is not being looked at, no continuous accel polling at higher than the minimum useful rate.
