# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A D&D-themed watchface for **Pebble Time 2** (Core Devices, `emery` platform, 200×228 px, 64-color memory LCD). Time is displayed as animated polyhedral dice over a parchment aesthetic, with diegetic indicators (heart for HR, torch for battery, journey trail for steps, ribbon for date, familiar for Bluetooth).

UUID and Pebble manifest live in `package.json` (`pebble` block). Configurable message keys are declared there: `CLASS`, `FAMILIAR`, `STEP_GOAL`, `SLEEP_OVERRIDE`.

## Build & run

There is no local build system checked into the repo. Use either:

- **CloudPebble:** GitHub Repo Sync → build → install on emulator.
- **Local Pebble SDK** (if installed):
  ```
  pebble build
  pebble install --emulator emery
  pebble logs --emulator emery
  ```

No test framework — validation is manual in the emulator. Each phase in `plan.md` has its own manual-emulator checklist.

## Architecture

All source is in `src/c/`. Module responsibilities are intentionally narrow; the central rule is **`face.c` owns state, everyone else is dispatched into**.

| File | Owns |
|---|---|
| `main.c` | App lifecycle, service subscriptions. Thin shims that forward every PebbleOS callback to a `face_on_*` function. Stays thin by design. |
| `face.c` / `face.h` | `FaceState`, the layer tree, and event dispatch. Updates state, marks affected layers dirty, hands off to tumble/journey animations. |
| `die.c` / `die.h` | `Die` struct + `die_draw`. After Phase 2.5 step 3, `die_draw` is a one-liner that forwards to `dice3d_draw` for both tumbling and settled states (settled = zero net rotation). Sprite swap (Phase 4) replaces the body with `graphics_draw_rotated_bitmap`. |
| `dice3d.c` / `dice3d.h` | Procedural 3D dodecahedron (D12, hour die) and pentagonal trapezohedron (D10, minute dice). Euler rotation → orthographic projection → 2D signed-area backface cull → 4-step flat shading. No depth buffer (convex + backface cull = no overdraw). Vertex / face / per-value-rest-rotation tables are emitted by `tools/gen_dice.py` — re-run rather than hand-edit (the script validates pentagon edge lengths, kite planarity, opposite-pair value sums, and CCW perimeter ordering — a face listed out of perimeter order renders as a self-intersecting polygon). At draw time the per-value rest rotation puts the face labeled `die->value` dead-on the camera; every visible face draws its own baked numeral. Numeral visibility flips on motion: at rest only the active face (`face->value == die->value`) shows its glyph, in the prominent font + highlight color (`COLOR_NUMERAL_ACTIVE`). While rotating (any `rot_x/y/z` nonzero — physics, QUICK, SHAKE) every visible face draws its numeral in the smaller side font, with the active face still tinted so the target value is scannable as it tumbles. Side faces at rest are blank by design — at small projected sizes their glyphs overflowed into neighbour faces no matter how tight the box. The `AREA_DOUBLE_MIN` sliver cull above the face loop is the only cull. Both ink colors are macros at the top of the file, intended to become user-selectable in Phase 5. |
| `tumble.c` / `tumble.h` | Per-die animation state machine. `TUMBLE_QUICK` (hour re-roll) and `TUMBLE_SHAKE` (minute settle). `TUMBLE_FULL` exists in the enum but is unused — the tap-roll now goes through `physics.c` instead. |
| `physics.c` / `physics.h` | Single-shot dice-tray simulation fired by tap. Random linear + angular velocity, wall and die-die collisions, damping, then a 70→100% return-to-home phase. Owns die positions and rotations during its run, so `face_on_tick` skips dice updates while `physics_is_active()`. Position uses 256× fixed-point so damping doesn't quantize sub-pixel velocity to zero. |
| `journey.c` / `journey.h` | Info-area content — sine-wave trail with mage walking on it, campfire at the trailhead with sleep duration ("6.2h") beneath, cloud + temperature ("28°") above the trail center, numeric step count ("1.2k") below. Owns mage frames + camp + cloud bitmaps. Slide animations keep the mage walking along the sine y-curve as steps update. Heart and torch are drawn separately by `widgets_draw_heart` / `widgets_draw_torch` after `journey_draw` so they overlay. `TRAIL_X_START` / `TRAIL_X_END` / `TRAIL_MIDLINE` / `TRAIL_AMP` define the sine geometry; the trail cuts off before the right edge to leave room for the big torch. |
| `widgets.c` / `widgets.h` | Ribbon (banner + Bluetooth familiar + feather quill + date) plus the heart and big torch widgets. `widgets_draw_heart(at, hr)` and `widgets_draw_torch(at, pct)` are centred on the supplied point. Torch atlas is 256×64 = 4 sub-bitmaps of 64×64 (full / half / embers / dark), keyed off `torch_state_for(pct)`; battery % renders inside the flame in white. Familiar is on the LEFT of the ribbon, feather on the RIGHT (swapped from the earlier layout). |
| Resource bundle | Bitmaps live in `resources/` and are declared in `package.json` under `pebble.resources.media` with `IMAGE_*` resource IDs. Sub-bitmaps from spritesheets use `gbitmap_create_as_sub_bitmap(parent, sub_rect)` — destroy sub-bitmaps **before** their parent atlas, and draw alpha-having bitmaps with `graphics_context_set_compositing_mode(ctx, GCompOpSet)` so transparent regions don't paint over what's behind. Memory budget watch: a 200×228 decoded bitmap is ~45 KB; loading it alongside all other sprites on Emery crashes the watch (App fault, OOM). The parchment background is currently a flat `GColorRajah` fill via `bg_update` in `face.c` — bitmap loading is deferred until we have a smaller texture or a different rendering approach. |

### Cross-cutting conventions

- **`s_warm` gate** in `face.c`: false during the initial state push from `main.c` so the dice snap to their first values without animating. Flipped true at end of first `face_on_tick`. Don't bypass.
- **Time-sampling rule** (tap roll): `face_on_tap` snapshots wall clock *once* per gesture, then all three dice settle on that snapshot. `tumble_start` itself captures the target face value at call time, so a mid-tumble wall-clock advance never skews the settled value.
- **Cascading throw**: tap fires ones (0 ms delay) → tens (100 ms) → hour (200 ms). The hour die's FULL duration is longer than the minute dice, so the heavy die lands last. Don't reorder without revisiting `plan.md` Phase 2c.
- **Animation budget**: full ceremonial roll only on tap/wrist-raise/hour-change. Minute changes get the cheap SHAKE only — 1,440 daily animations would shred the battery otherwise.
- **`PBL_HEALTH` guard**: health code must be wrapped (`#if defined(PBL_HEALTH) ... #endif`) — see `main.c` and `face.c`.
- **`DEBUG_AUTO_ROLL`** in `face.c` fires `face_on_tap` every 5 s for emulator testing. Comment out for production.
- **Color palette** (Parchment preset) is currently bare `GColor*` constants throughout the code; Phase 5c moves them behind a `theme.h` accessor. See `plan.md` "Color palette" table for current mappings.

### Layer layout (vertical, 228 px total)

```
0   – 28   ribbon (BT familiar left, feather quill right, date centered)
28  – 156  dice stage (128 px — tray + all 3 dice in one shared layer)
156 – 228  info area (72 px — heart top-left, camp + sleep hours
                       beneath it, cloud + temp top-center, sine-wave
                       trail through middle with mage walking on it,
                       step count below trail, big torch right)
```

Sections butt up cleanly without overlap. The dice stage is one shared layer (`s_dice_layer`) — Die.center is in stage-local coords. Any tumble marks the stage dirty and re-renders all three dice; the per-frame cost is small. The tray itself is drawn as polygons in `dice_stage_update`; a sprite is planned but not in yet (colors are placeholders).

The info area is also one shared layer (`s_info_layer`), drawn by `info_update` in `face.c`. The big torch sprite (64×64) spans almost the full height of this zone — that's why stats and journey can't be separate layers any more (clipping would chop the torch). `journey_draw` handles the trail / camp / sleep hours / cloud / temp / steps / mage; `widgets_draw_heart` and `widgets_draw_torch` are layered on top by the same `info_update`.

## Planning docs

These are the source of truth for design decisions; consult them before non-trivial changes:

- `dnd_dice_watchface_brief.md` — visual brief (palette, layout, states, copy rules).
- `plan.md` — phased development plan with module map, sprite manifest, performance budget, SDK cheat sheet, and current phase status.
- `Phase2.5.md` — active refactor plan between Phase 2 and Phase 3 (sliver-cull fix, unify rendering through `dice3d.c`, merge three die layers into one stage layer).

When work spans a phase boundary, update the relevant doc's checklist alongside the code change.
