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
| `dice3d.c` / `dice3d.h` | Procedural 3D dodecahedron (D12, hour die) and pentagonal trapezohedron (D10, minute dice). Euler rotation → orthographic projection → 2D signed-area backface cull → 4-step flat shading. No depth buffer (convex + backface cull = no overdraw). Vertex / face / per-value-rest-rotation tables are emitted by `tools/gen_dice.py` — re-run rather than hand-edit (the script validates pentagon edge lengths, kite planarity, opposite-pair value sums, and CCW perimeter ordering — a face listed out of perimeter order renders as a self-intersecting polygon). At draw time the per-value rest rotation puts the face labeled `die->value` dead-on the camera; every visible face draws its own baked numeral. Two-tier glyph: the active face (`face->value == die->value`) uses a larger font + box and the highlight color (`COLOR_NUMERAL_ACTIVE`); every other visible face uses a smaller "context" font + tighter box and the plain ink color (`COLOR_NUMERAL`). The smaller side-face box fits inside foreshortened polygons without overflow, so no per-face polygon-fit check is needed — the `AREA_DOUBLE_MIN` cull above the loop handles the only edge case (near-edge-on faces). Both ink colors are macros at the top of the file, intended to become user-selectable in Phase 5. |
| `tumble.c` / `tumble.h` | Per-die animation state machine. `TUMBLE_QUICK` (hour re-roll) and `TUMBLE_SHAKE` (minute settle). `TUMBLE_FULL` exists in the enum but is unused — the tap-roll now goes through `physics.c` instead. |
| `physics.c` / `physics.h` | Single-shot dice-tray simulation fired by tap. Random linear + angular velocity, wall and die-die collisions, damping, then a 70→100% return-to-home phase. Owns die positions and rotations during its run, so `face_on_tick` skips dice updates while `physics_is_active()`. Position uses 256× fixed-point so damping doesn't quantize sub-pixel velocity to zero. |
| `journey.c` / `journey.h` | Bottom journey strip. Owns its own slide animations for step changes and sleep transitions. |
| `widgets.c` / `widgets.h` | Date ribbon (with Bluetooth familiar) and stat row (heart-with-BPM-inside, torch with battery %). |

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
0   – 28   ribbon (with familiar in corner)
28  – 166  dice stage (138 px — tray + all 3 dice in one shared layer)
166 – 198  stat row (heart + BPM, torch + %)
198 – 228  journey strip
```

Sections butt up cleanly without overlap. The dice stage is one shared layer (`s_dice_layer`) — Die.center is in stage-local coords. Any tumble marks the stage dirty and re-renders all three dice; the per-frame cost is small. The tray itself is drawn as polygons in `dice_stage_update`; a sprite is planned but not in yet (colors are placeholders).

## Planning docs

These are the source of truth for design decisions; consult them before non-trivial changes:

- `dnd_dice_watchface_brief.md` — visual brief (palette, layout, states, copy rules).
- `plan.md` — phased development plan with module map, sprite manifest, performance budget, SDK cheat sheet, and current phase status.
- `Phase2.5.md` — active refactor plan between Phase 2 and Phase 3 (sliver-cull fix, unify rendering through `dice3d.c`, merge three die layers into one stage layer).

When work spans a phase boundary, update the relevant doc's checklist alongside the code change.
