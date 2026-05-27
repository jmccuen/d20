# D&D Dice Watchface — Refactor Plan (Phase 2.5)

This plan covers the work between "Phase 2 animation mostly working" and "Phase 3 sprite pipeline." Four surgical changes — three small, one structural — that fix a visible bug, unify the rendering pipeline, and unlock the visual-interaction architecture for future polish.

## Status going in

**Working:**
- Phase 0 scaffold + Phase 1 procedural polish are in (facet lines, heart resize with BPM-inside, no-data states, ribbon scroll shape, four-state torch).
- Phase 2 animation is functional: `dice3d.c` renders the D12/D10 polyhedra correctly during tumble; `tumble.c` drives `TUMBLE_FULL`, `TUMBLE_QUICK`, `TUMBLE_SHAKE` with proper time-sampling and a cascading-delay throw on tap. Auto-roll debug timer lets you watch the ceremonial roll in the emulator.
- Three-die hand-off works: dice3d during tumble, flat pentagon/kite via `die.c` at rest.

**Wrong:**
1. **Sliver artifact.** During hour-die tumbles, near-edge-on faces project as narrow "spikes" sticking out of the silhouette. The 3D backface culling is correct; the existing area-based sliver suppression has too loose a floor.
2. **Settled state looks weaker than tumbling state.** The flat pentagon/kite via `die.c` lacks the character of the 3D dodecahedron the eye just saw mid-roll. Hard cut at settle is visible because the visual quality drops.
3. **Three separate dice layers prevent visual interaction.** Dice can never overlap, drift toward each other, or feel like a thrown handful. Each is confined to its own ~90px or ~48px box.

This plan addresses all three before Phase 3 begins.

## Scope

**In scope**
- Sliver-culling fix
- Numeral source swap in `dice3d.c`
- Drop the flat pentagon/kite paths in `die.c`
- Merge the three dice layers into one stage layer

**Out of scope**
- Sprite delivery (Phase 3+)
- Inter-die collision / physics (different concern; can come later on top of the stage-layer merge)
- Settings page and theming (Phase 5)
- Adding new visual elements

## Work order

Sequenced low-risk to high-risk. Commit after each step.

### Step 1 — Sliver-culling fix (~10 min)

**Goal:** Eliminate the "spike" artifacts that appear on the hour die at certain tumble angles.

**File:** `src/c/dice3d.c`

**Change:** Replace the absolute-pixel area floor with an angle-based tilt test. Cull any face whose normal tilts more than ~78° from the camera, independent of die size.

Inside the per-face loop, just after the existing `if (nz <= 0) continue;` camera-facing test:

```c
int64_t n_sq  = (int64_t)nx*nx + (int64_t)ny*ny + (int64_t)nz*nz;
int64_t nz_sq = (int64_t)nz * nz;
// cull faces tilted more than ~78° from camera
// cos(78°) ≈ 0.208, cos²(78°) ≈ 0.043 → ratio nz²/|n|² < 0.043
if (nz_sq * 23 < n_sq) continue;
```

Delete the `AREA_DOUBLE_MIN` macro and the shoelace area computation block. (Keep them if you want belt-and-suspenders, but they're redundant once tilt-culling is in.)

**Acceptance:** Roll the hour die ~20 times in the emulator (the `DEBUG_AUTO_ROLL` timer makes this easy). Watch for spike artifacts from any angle, particularly during the first ~200 ms of the roll where rotation is fastest. None should be visible.

**Risk:** Very low. Pure visual cleanup. No state or API touched.

---

### Step 2 — Numeral source swap (~10 min)

**Goal:** Make the displayed numeral target-driven at rest while keeping the satisfying baked-face-flicker during tumble. This is the one-line change that enables step 3.

**File:** `src/c/dice3d.c`

**Change:** In the numeral rendering block (near the bottom of `dice3d_draw`), pick the displayed value based on whether the die is currently tumbling.

```c
// before:
//   snprintf(buf, sizeof(buf), "%d0", (int)front->value);   // tens
//   snprintf(buf, sizeof(buf), "%d",  (int)front->value);   // hour/ones

// after:
int displayed = die->tumbling ? front->value : die->value;
if (die->type == DIE_TENS) {
  snprintf(buf, sizeof(buf), "%d0", displayed);
} else {
  snprintf(buf, sizeof(buf), "%d", displayed);
}
```

**Acceptance:** Nothing visibly changes yet — during a tumble `die->tumbling == true`, so the front face's baked numeral still flickers as faces turn past, exactly as before. The change only matters when settled-state goes through `dice3d_draw` (step 3).

**Risk:** Very low. Conditional swap is purely additive.

---

### Step 3 — Drop the flat paths in `die.c` (~30 min)

**Goal:** Settled state renders through `dice3d.c` at zero rotation, so the rest pose has the same visual quality as the tumble. Removes the visible quality-drop at settle.

**Files:** `src/c/die.c`, `src/c/die.h`, `src/c/tumble.c`

**Changes in `die.c`:** Collapse `die_draw` to a one-liner. Delete the procedural primitives (`draw_pentagon`, `draw_kite`, `draw_face_number`, `rotated_vertex`, `midpoint`, `draw_polygon`) and their color macros — they're now unused.

```c
void die_draw(GContext *ctx, const Die *die) {
  dice3d_draw(ctx, die);
}
```

Roughly ~100 lines deleted, ~5 lines kept.

**Changes in `die.h`:** Keep the Die struct as-is. The `tumbling` flag is still needed (step 2's numeral swap reads it). The `rotation` field (2D angle) was only consumed by the now-deleted `draw_pentagon` / `draw_kite` — it's dead unless we repurpose it. See `tumble.c` below.

**Changes in `tumble.c`:** `TUMBLE_SHAKE` currently writes `die->rotation` (a 2D wobble angle). With the flat paths gone, no renderer consumes that field. Two options:

- **(a) Repurpose SHAKE to drive `die->rot_z` instead of `die->rotation`.** The wobble becomes a small 3D twist around the camera axis, which through `dice3d_draw` reads as the die "settling" with a brief axial rotation. Visual is slightly different — a small 3D twist instead of a 2D wiggle — and arguably reads better. Set `die->tumbling = true` for the duration of the SHAKE so the numeral flicker engages (or leave it false to keep the value steady; either works). **Recommended.**

- **(b) Keep `die->rotation` as a 2D screen-space wobble layered over the 3D render.** Would require `dice3d_draw` to apply it as a final 2D rotation step after projection. More code, more state, marginal visual gain over (a).

Pick (a). The SHAKE update in `tumble.c` becomes:

```c
if (h->kind == TUMBLE_SHAKE) {
  int32_t remaining = ANIMATION_NORMALIZED_MAX - p;
  int32_t ampl      = (int64_t)SHAKE_AMPL * remaining / ANIMATION_NORMALIZED_MAX;
  int32_t phase     = (int64_t)p * 4 * TRIG_MAX_ANGLE / ANIMATION_NORMALIZED_MAX;
  die->rot_z        = (int64_t)sin_lookup(phase) * ampl / TRIG_MAX_RATIO;
  die->rot_x        = 0;
  die->rot_y        = 0;
  die->value        = h->target_value;
  die->tumbling     = false;   // keep numeral steady on target value during shake
  die->flash        = false;
}
```

The teardown remains unchanged — it already zeroes all rotation axes.

You can also delete `die->rotation` from the struct now if you want to be tidy. Or keep it for a future use.

**Acceptance:**
- The watchface renders dice as full 3D dodecahedra / trapezohedra at rest, not flat pentagons / kites.
- Tumble landing → settled value is the correct target (validates step 2 is wired up).
- TUMBLE_SHAKE on minute change still produces a visible wobble — slightly different character than before, expected.
- No new artifacts. If the rest pose puts the wrong face dead-on for any value, that's fine; the step-2 numeral swap paints the target value on whichever face is front, regardless of the polyhedron's actual labeling.

**Risk:** Moderate. Touches all three of die / dice3d / tumble. Test thoroughly in the emulator with auto-roll plus minute-tick simulation.

---

### Step 4 — Merge to one stage layer (~1–2 hours)

**Goal:** Collapse the three sibling dice layers into a single "stage" layer covering the time-display region. Enables visual overlap and is a prerequisite for any future inter-die motion or physics.

**Files:** `src/c/face.c`, `src/c/face.h`, `src/c/die.h`, possibly `src/c/dice3d.c`

**Changes in `die.h`:** Die struct's `center` is now expected to be in stage-layer coordinates rather than per-die-layer-local. No struct field changes required — only the semantic interpretation. (If you want explicit "rest position" support for future drift, add `int16_t rest_x, rest_y;` now; otherwise defer.)

**Changes in `face.c`:** Replace the three layers and their three update_procs with a single stage layer.

```c
static Layer *s_dice_layer;          // replaces s_hour_layer, s_tens_layer, s_ones_layer
static Die    s_hour_die, s_tens_die, s_ones_die;
static TumbleHandle s_hour_tumble, s_tens_tumble, s_ones_tumble;

static void dice_stage_update(Layer *l, GContext *ctx) {
  dice3d_draw(ctx, &s_hour_die);
  dice3d_draw(ctx, &s_tens_die);
  dice3d_draw(ctx, &s_ones_die);
}

// in face_init:
s_dice_layer = layer_create(GRect(0, 28, bounds.size.w, 138));  // y 28..166
layer_set_update_proc(s_dice_layer, dice_stage_update);
layer_add_child(root, s_dice_layer);

// Die centers now in stage-space (relative to (0,28)):
s_hour_die = (Die){
  .center   = GPoint(bounds.size.w / 2, 45),    // ~73 px down within stage
  .radius   = 45,
  .value    = 12,
  .type     = DIE_HOUR,
};
s_tens_die = (Die){
  .center   = GPoint(bounds.size.w * 30 / 100, 110),
  .radius   = 24,
  .value    = 0,
  .type     = DIE_TENS,
};
s_ones_die = (Die){
  .center   = GPoint(bounds.size.w * 70 / 100, 110),
  .radius   = 24,
  .value    = 0,
  .type     = DIE_ONES,
};

tumble_init(&s_hour_tumble, &s_hour_die, s_dice_layer);
tumble_init(&s_tens_tumble, &s_tens_die, s_dice_layer);
tumble_init(&s_ones_tumble, &s_ones_die, s_dice_layer);
```

All three TumbleHandles now reference `s_dice_layer`. When any tumble updates, it marks the shared layer dirty and all three dice re-render. This is slightly less efficient than per-die layers — a hour-only animation triggers all three `dice3d_draw` calls per frame — but the cost is small (three procedural renders per frame, well within budget) and the architectural simplicity is worth it.

**Note on `tumble.c`:** No changes needed; it already references `h->layer` abstractly and doesn't care that it's now shared across handles.

**Note on `dice3d.c`:** Already uses `die->center` for projection, so it correctly handles either layer-local or stage-local coordinates depending on what `face.c` passes. No changes needed.

**Acceptance:**
- All three dice render in their rest positions correctly.
- Tumbling one die doesn't displace the others positionally.
- Manual experiment: temporarily move `s_hour_die.center.x` to overlap a minute die's center, build, confirm they visibly overlap (the hour die occludes the minute die where they intersect). This verifies the stage layer is actually shared. Revert before committing.
- No regression in animation timing or settled values.

**Risk:** Moderate to high. Touches the layer tree, which means any wrong layer math reads as "stuff in the wrong place." Validate in emulator before pushing.

---

## What you can do after this is done

The stage-layer merge opens specific things that weren't possible before:

- **Positional drift during tumble.** Add a small ease curve on the die's `center.x` / `center.y` so it visibly arcs during a ceremonial throw, then returns to rest. Sells "thrown" much better than three siloed boxes.
- **Hour die occlusion.** During a tap-roll, lay the hour die's animation slightly later than the minute dice so it visibly lands on top of them mid-flight. Already setup-able via the cascading delays in `face_on_tap`; the missing piece was that the dice couldn't *visually* cross before.
- **Scale variation.** Tween `die->radius` slightly during tumble for a "weight" effect — the hour die can briefly grow ~10% as it lands. Cheap and reads well.

These are nice-to-haves, not part of this refactor. Mentioned only as motivation for why the merge is worth the trouble.

## How this fits the broader plan

This refactor lands between Phase 2 and Phase 3 of the development plan. After it:

- **Phase 3 — Stub-sprite pipeline check.** Bake one placeholder PNG, wire through resources, confirm the sprite-loading path. Now informed by the question: do we even *need* the sprite swap? If the unified 3D rendering looks good enough, sprite delivery becomes optional polish rather than a requirement.
- **Phase 4 — Sprite delivery.** Likely scoped down: still need heart, torch, milestone, familiar art, but the 28 dice-face sprites may be unnecessary if procedural 3D carries the look.
- **Phase 5 — Configurables and theming.** Unchanged.

## Reference

### Files touched

| File         | Steps   | Scope                                         |
| ------------ | ------- | --------------------------------------------- |
| `dice3d.c`   | 1, 2    | Two small surgical edits                      |
| `die.c`      | 3       | Delete ~100 lines, keep ~5                    |
| `die.h`      | 3, 4    | Possibly drop unused field; semantic update   |
| `tumble.c`   | 3       | Adapt SHAKE to drive `rot_z` instead of `rotation` |
| `face.c`     | 4       | Significant restructure (layer merge)         |
| `face.h`     | 4       | No public API change                          |

### Build & run

In CloudPebble: GitHub Repo Sync → build → install on emulator.

Locally:

```bash
pebble build
pebble install --emulator emery
pebble logs --emulator emery
```

### Validation checklist (after step 4 is in)

- [ ] Static face: all three dice render as proper 3D polyhedra with shading
- [ ] Hour tick: hour die alone re-rolls (~400 ms), settles on new hour
- [ ] Minute tick (ones change): brief shake on ones die only
- [ ] Minute tick (tens change): brief shake on both tens and ones dice
- [ ] Tap (or auto-roll timer): all three dice tumble with cascade (ones first, hour last), settle on current time
- [ ] No sliver/spike artifacts at any tumble angle
- [ ] Settled values are correct after a roll mid-minute (time-sampling rule still holds)
- [ ] Sleep mode still works (journey strip, etc — should be untouched)
- [ ] Battery state changes still update the torch
- [ ] Bluetooth disconnect still updates the familiar
