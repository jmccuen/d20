# D&D Dice Watchface — Development Plan

## Overview

A Pebble Time 2 watchface where the time is displayed via animated polyhedral dice in a TTRPG / parchment aesthetic, with diegetic indicators for heart rate (heart sprite with BPM inside), steps (journey trail with adventurer token), date (parchment ribbon), battery (torch), and Bluetooth (familiar).

## Status

**Phase 1 visually verified in emulator (`Phase1.png`). Phase 2 animation code is in (including 2c procedural 3D); manual emulator testing remains to close Phase 2.** Code state:

- **Phase 1 changes:** Vertical layout reworked into five non-overlapping bands (28 / 90 / 48 / 32 / 30 px); pentagon and kite dice gained facet hints; heart enlarged to host the BPM number internally with em-dash for no-data; torch has 4 distinct battery states with percent stacked below; ribbon is a tapered-scroll polygon with fold-crease accents; familiar has a visible silhouette + faded variant; token nudges off-camp when `steps <= 0`.
- **Phase 2 changes:** New `tumble.h/.c` module owns per-die Pebble Animations. Hour change triggers a ~400 ms re-roll; minute tens/ones changes trigger ~100 ms settle shakes; taps trigger a ~600 ms ceremonial roll across all three dice using a once-per-gesture wall-clock snapshot (time-sampling rule). Dice flash a brighter palette on settle. `journey.c` rewritten to own a slide animation that smoothly walks the token to camp on sleep, away from camp on wake, and between step-count updates the rest of the time; a "Xh Ym rest" label renders under the camp for 2 h after waking. A `s_warm` gate suppresses animations during the initial state push so the dice snap to their first values without a roll.

**Decisions folded into this plan from recent brief revisions:**
- Dice render as flat-shaded 2D sprites with rotation that *read* as 3D (BG3-style aesthetic; not literal 3D rendering)
- BPM number drawn inside the heart sprite; "HP" suffix dropped
- Configurable color themes added to Phase 5 (presets first, custom later)
- Sprite manifest expanded beyond dice faces (heart, torch, milestones, familiar, class overlays)
- Parchment background uses a static-sized bitmap; no 9-slice
- Animation budget revised — full ceremonial roll only on wrist-raise / tap / hour-change, not on every minute change

See `dnd_dice_watchface_brief.md` for the full visual brief.

## Target platform

- **Device:** Pebble Time 2 (Core Devices)
- **Display:** 200×228 px, 64 colors, memory LCD / transflective (fixed resolution; no need for resizable layouts)
- **SDK:** PebbleOS open-source SDK (C), platform key `emery`
- **Sensors used:** heart rate, accelerometer, step counter, sleep tracker
- **Companion:** Pebble companion app for configuration

## Architecture

```
src/c/
├── main.c        — app lifecycle, service subscriptions, callback shims
├── face.h/.c     — FaceState, layer composition, mark-dirty dispatch
├── die.h/.c      — Die abstraction; procedural now, sprite swap later
├── journey.h/.c  — journey strip: trail, milestones, token, sleep mode
└── widgets.h/.c  — date ribbon, HP heart, torch battery, familiar
```

Phase 2 adds:
```
├── tumble.h/.c   — per-die animation state machines and tumble curve
└── dice3d.h/.c   — procedural 3D polyhedron renderer for FULL/QUICK tumble
```

Phase 5 adds:
```
├── theme.h/.c    — palette presets and current-theme accessors
└── settings.h/.c — message-key handlers + persist_write/read
src/pkjs/
└── index.js      — PebbleKit JS settings page
```

Die rendering stays abstracted behind `die_draw(ctx, &die)` so the sprite swap (Phase 4) is a single-file change with no other code touched.

## Sprite manifest

The full asset list across all phases. Dimensions are targets — final art may flex by a few pixels.

| Asset                       | Size         | Count | Phase | Notes                                               |
| --------------------------- | ------------ | ----- | ----- | --------------------------------------------------- |
| Hour die faces              | 90 × 90 px   | 12    | 4     | One per face value 1–12                             |
| Tens die faces              | 56 × 56 px   | 6     | 4     | Values 00, 10, 20, 30, 40, 50                       |
| Ones die faces              | 56 × 56 px   | 10    | 4     | Values 0–9                                          |
| Heart sprite                | ~32 × 28 px  | 1     | 4     | Sized to fit 3-digit BPM internally                 |
| Torch + flame, full         | ~16 × 30 px  | 1     | 4     | Battery ≥ 40%                                       |
| Torch + flame, half         | ~16 × 24 px  | 1     | 4     | Battery 20–40%                                      |
| Torch + flame, embers       | ~16 × 18 px  | 1     | 4     | Battery 5–20%                                       |
| Torch, dark wick            | ~16 × 16 px  | 1     | 4     | Battery < 5%                                        |
| Adventurer token base       | ~12 × 12 px  | 1     | 4     | Circular outline; class overlays draw on top        |
| Class silhouettes           | ~8 × 10 px   | 6     | 5     | Fighter, wizard, rogue, cleric, ranger, barbarian   |
| Familiar silhouettes        | ~12 × 10 px  | 10    | 5     | Raven, owl, cat, bat, fox, rat, frog, hawk, snake, wolf pup |
| Treasure chest              | ~12 × 10 px  | 1     | 4     | Mid-trail milestone                                 |
| Campfire idle (tent)        | ~14 × 14 px  | 1     | 4     | Trailhead, awake                                    |
| Campfire lit (tent + flame) | ~14 × 18 px  | 1     | 4     | Sleep mode                                          |
| Boss sigil                  | ~12 × 12 px  | 1     | 4     | End-of-trail milestone                              |
| Parchment background        | 200 × 228 px | 1     | 4     | Optional polish; procedural fill works as fallback  |
| Ribbon parchment            | 200 × 28 px  | 1     | 4     | Optional polish; static size, no 9-slice            |

**Approximate total: ~55 sprites.** Heart, torch, token, milestones (Phase 4) are the priority block. Class and familiar variants (Phase 5) ride alongside settings work.

## Development phases

### Phase 0 — Scaffold ✅ COMPLETE

- [x] Module split, build pipeline, all service subscriptions
- [x] Procedural rendering of dice, ribbon, stats, journey strip
- [x] Builds in CloudPebble, runs in emery emulator

### Phase 1 — Procedural polish

Refine the procedural rendering until the watchface reads as intentional design rather than scaffolding. No new resources yet — just better polygons and better edge cases.

**Die geometry**
- [x] **Hour die** — 3 facet spokes from the centroid to alternate vertices (`die.c:draw_pentagon`)
- [x] **Minute dice** — trapezohedron midline + 2 diagonal facets to the lower-edge midpoints (`die.c:draw_kite`)
- [x] **Outline weight** — 3 px hour die, 2 px minute dice

**No-data and edge states**
- [x] **Heart sprite resizing** — heart enlarged to ~28w × 25h with ~24 × 14 interior; 3-digit BPM fits (`widgets.c:draw_heart_body`)
- [x] **HR "no data"** — em-dash rendered inside the heart when `heart_rate == 0`; "HP" suffix removed (`widgets.c:draw_bpm_inside`)
- [x] **Steps "no data"** — token nudged 4 px off-camp when `steps <= 0` (`journey.c:journey_draw`)
- [x] **Familiar placeholder** — circle outline + eye dot when connected; faded outline (windsor tan) when disconnected (`widgets.c:widgets_draw_ribbon`)
- [x] **Battery thresholds** — four distinct torch states: full flame + core highlight, half flame, ember cluster, single faint ember at the wick (`widgets.c:draw_torch`)

**Layout and palette**
- [x] **Spacing audit** — vertical layout reworked so sections butt up cleanly (ribbon 0-28 / hour 28-118 / minutes 118-166 / stats 166-198 / journey 198-228); no layer overlap (`face.c:face_init`)
- [x] **Ribbon flourish** — ribbon is now a 6-vertex tapered-scroll polygon with fold-crease accents at each end (`widgets.c:widgets_draw_ribbon`)
- [x] **Color tuning** — added `GColorIcterine` flame core and `GColorWindsorTan` facet-shadow / faint accents; remaining bare `GColor*` constants will move to `theme.h` in Phase 5

**State testing (manual in CloudPebble emulator)**
- [ ] Force sleep mode active (token at camp, dimmed trail, flame on tent)
- [ ] Force each battery threshold (≥40% / 20-40% / 5-20% / <5%)
- [ ] Force Bluetooth disconnected (familiar should render as faded outline)
- [ ] Verify midnight, noon, and AM/PM transitions
- [ ] Verify `heart_rate == 0` renders em-dash inside the heart, not "0"
- [ ] Verify `steps == 0` shows token nudged just past camp, not stuck on it

### Phase 2 — Animation

Polish-first means the static face is trusted before motion gets added. Animation lives in `tumble.h/.c` for dice; `journey.c` owns its own slide animation.

**Animation triggers**

| Trigger                       | Animation                                  | Daily frequency      | Cost note |
| ----------------------------- | ------------------------------------------ | -------------------- | --------- |
| Wrist-raise                   | Full ceremonial roll, all 3 dice (~600 ms) | ~50× (when looking)  | The signature moment |
| Tap (touchscreen)             | Full ceremonial roll, all 3 dice           | user-driven          | Easter egg |
| Hour change (XX:00)           | Hour die re-rolls in place (~400 ms)       | 24×                  | Hour die only |
| Minute change, tens increment | Tens die settle-shake (~100 ms)            | 6× / hour            | + ones-die shake |
| Minute change, ones only      | Ones die settle-shake (~100 ms)            | 54× / hour           | Cheap, tiny shake |
| Idle                          | Static                                     | majority of the time | E-paper holds; zero power |

Rationale: full ceremonial rolls are reserved for moments where the user is actually looking (wrist-raise, tap) or has a thematic reason (hour change). Minute changes get a settle-shake — enough to read as "the dice just settled to this number" without burning the battery on 1,440 daily animations.

**Tumble implementation**
- [x] Rotation + face-value flicker (6 segments full / 4 quick) + ease-out settle (`tumble.c:tumble_update`)
- [x] Flash highlight on settle — `GColorIcterine` palette swap during the final segment (`die.c:draw_pentagon/kite`)
- [x] Time-sampling rule — `face_on_tap` snapshots the wall clock once, then settles all three dice on that snapshot
- [x] Hour-change → `TUMBLE_QUICK`, minute tens/ones → `TUMBLE_SHAKE` (`face.c:face_on_tick`)
- [x] Tap → `TUMBLE_FULL` across all three dice (`face.c:face_on_tap`)
- [x] `s_warm` gate so the dice snap into their first values without animating on cold start (`face.c`)

**Journey strip animation**
- [x] Token slides smoothly between step-count updates (`journey.c:journey_set_steps` + `start_slide`)
- [x] Sleep transition: token slides to camp on `journey_set_sleeping(true)`; slides back to step-position on wake (longer `SLIDE_SLEEP_MS` curve, ease-in-out)
- [x] Wake label: `Xh Ym rest` renders under camp for 2 h after waking, derived from `(sleep_started_at, sleep_ended_at)` (`journey.c:draw_rest_label`)

**Phase 2c — Procedural 3D tumble**

Replaces the in-plane 2D rotation during FULL / QUICK with a true 3D polyhedron rotation. SHAKE stays 2D — a per-minute shake doesn't need a polyhedron tumble. Decisions locked: D12 for the hour die, baked-in face numerals, upper-left light direction with 4-step palette ramp (`GColorWhite` / `GColorLightGray` / `GColorWindsorTan` / `GColorBulgarianRose`), hard-cut to settled rendering on settle.

- [x] `dice3d.h/.c` — D12 (20 verts / 12 pentagonal faces, values 1-12 with opposite pairs summing to 13) and D10 trapezohedron (12 verts / 10 kite faces, values 0-9)
- [x] 3-axis Euler rotation via `sin_lookup`/`cos_lookup`, orthographic projection
- [x] 2D signed-area backface cull
- [x] 4-color flat shading keyed on transformed-vertex average z
- [x] Numeral on front-facing face, drawn axis-aligned at the projected centroid
- [x] `Die` extended with `tumbling` + `rot_x/y/z`; `die_draw` routes to `dice3d_draw` when tumbling
- [x] `tumble.c` drives integer spin counts so progress=1 lands at zero net rotation (hides the hard-cut)
- [x] Cascade + weight: tap roll fires ones (0 ms delay) → tens (100 ms) → hour (200 ms); hour die's `TUMBLE_FULL` duration is 1400 ms vs 900 ms for the minute dice, so settle order is ones → tens → hour (last)

**Phase 2 manual emulator checklist**
- [ ] Tap triggers a visible 3-dice ceremonial roll: dice tumble as 3D polyhedra (not coins spinning in plane), settle on the current time
- [ ] At XX:00 the hour die tumbles in 3D and re-settles
- [ ] On minute change, only the changed die(s) do the 2D settle-shake (not the 3D tumble — that's only for FULL/QUICK)
- [ ] All D12 / D10 faces appear visible at some rotation (none permanently culled — would indicate winding-order bugs in `dice3d.c`'s face tables)
- [ ] Triggering sleep mode slides the token to camp; ending sleep slides it back
- [ ] After sleep ends, "Xh Ym rest" label is visible under the camp; gone after 2 h
- [ ] Cold start: dice show first value with no animation

### Phase 3 — Stub-sprite pipeline check

Single tracer-bullet asset to de-risk the swap path. **Note: probably folded
into Phase 4a (parchment background) now — the procedural 3D dice are
reading as intentional, so dice-face sprites are likely skipped. The
parchment background does double duty as the pipeline tracer.**

- [ ] Confirm `package.json` resource block accepts a `bitmap` entry and
      builds without errors
- [ ] Confirm `gbitmap_create_with_resource` returns a usable bitmap on
      Pebble Time 2 and the resource memory budget is reasonable

### Phase 4 — Sprite delivery

Ordered low-risk to higher, in the sequence we'll wire them. Procedural
dice rendering stays — the 3D D12/D10 read as intentional D&D art and
the per-value rest rotation plus highlight makes the current time read
clearly. **Dice-face sprites are no longer planned.**

Source folder is `resources/` (already populated for the first few items).
Heart and dice are deliberately deferred — both look right procedurally.

**4a — Parchment background + ribbon** (pipeline tracer)
- [x] Add resources to `package.json`: `IMAGE_PARCHMENT_BG` (200 × 228),
      `IMAGE_TOP_BANNER` (200 × 28), `IMAGE_FEATHER` (20 × 20)
- [x] Draw bg under the window in `face_init` via a `BitmapLayer`
      added first so it sits below every widget
- [x] Draw banner + feather quill + (still-procedural) familiar in
      `widgets_draw_ribbon`, replacing the tapered-scroll polygon
- [ ] Verify resource bundle size + runtime memory (do in emulator)

**4b — Torch sprite atlas**
- [x] New `torch.png` is 128 × 32 — 4 frames of 32 × 32 in the order
      full / half / embers / dark.
- [x] `widgets_init` loads the atlas + creates 4 sub-bitmaps;
      `widgets_draw_stats` picks the right sub-bitmap via
      `torch_state_for(pct)`.
- [x] Percent label sits in the bottom transparent region of the
      32 × 32 frame so the torch + % read as a vertical stack inside
      the 32-px stat row.
- [ ] If the text-vs-sprite alignment still reads off, options are
      (a) tighter sub-bitmap of just the top of each frame + dedicated
      text region below, (b) reposition the % to the left of the
      torch, or (c) crop the source torches so they don't drift
      between frames.

**4c — Mage on the journey token**
- [x] Added `MAGE_IDLE_1..2`, `MAGE_WALK_1..3` as separate bitmap
      resources
- [x] `journey.c` extended init/deinit to load/release the frames;
      `slide_update` writes `s_j.walk_frame_idx` (0..2) from animation
      progress; `draw_token` picks `mage_walk_*` while a slide is in
      flight, `mage_idle_1` otherwise
- [x] Procedural token circle removed (mage sprite replaces it entirely)
- [ ] Idle-cycle animation (alternating idle_1 / idle_2 on a slow timer)
      deferred — single idle frame for now
- [ ] Sprite orientation: idle/walk frames may face left while the
      trail runs left→right; verify in emulator and flip-x if needed

**4d — Campfire**
- [x] `camp.png` (32 × 32) sourced and wired into `journey.c`. Drawn
      via `draw_camp` at the trailhead, centered on the trail midline.
      Layer clipping handles the 1-px overhang above/below the strip.
- [ ] Single-state for now — campfire is always lit in the art. If we
      want the brief's idle-tent-vs-lit-fire distinction back, add a
      second sprite and switch on `s_j.sleeping`.
- [ ] Mage occludes the campfire when at the trailhead (steps = 0 or
      sleeping). If that reads wrong, offset the mage a few px from
      the campfire when at-camp.

**4e — Deferred / probably skip**
- Heart sprite: procedural heart reads fine; defer to "Phase 4 polish".
- Treasure chest, boss sigil: procedural milestones read fine; same.
- Dice face sprites: no longer planned.

### Phase 4.5 — Extended diegetic UI

New feature work that sits naturally with the sprite phase. Sequence
this after 4a–4c so the visual base is in.

**Weather indicator**
- [ ] Phone-side `src/pkjs/index.js` requests location via
      `navigator.geolocation` and queries OpenWeatherMap (or
      equivalent); sends temp + condition code back via `AppMessage`
- [ ] Watch-side message keys: `WEATHER_TEMP_C`, `WEATHER_COND`
- [ ] Render a small weather glyph (sun / cloud / rain) above the
      journey path as a fourth milestone — diegetic, positioned along
      the trail
- [ ] Temperature shown numerically under the glyph (°F or °C per a
      future setting)

**Winding journey path**
- [ ] Replace the straight dashed line in `journey.c` with a curve
      (sine wave or a few cubic segments)
- [ ] Token position becomes `(steps/goal) × arc_length` with x/y
      interpolated along the curve
- [ ] Milestones (camp / chest / weather / boss) snap to specific arc
      positions on the curve

**Numeric labels (diegetic, sparingly used)**
- [ ] Battery % under the torch is already in. Confirmed.
- [ ] Step count near the token or under the trailhead campfire, format
      like `4,287`. Single small numeral, doesn't compete with the dice.
- [ ] Temperature number under the weather glyph (per above).
- [ ] All numeric labels use `GOTHIC_14_BOLD` for visual consistency
      with the side-face dice numerals.

### Phase 5 — Configurables and theming

**5a — Settings infrastructure**
- [ ] PebbleKit JS settings page in `src/pkjs/index.js` (also hosts the
      weather fetch from Phase 4.5)
- [ ] Message keys: `CLASS`, `FAMILIAR`, `STEP_GOAL`, `SLEEP_OVERRIDE`,
      `THEME`, `TEMP_UNIT_F` (°F vs °C), `DICE_INK_COLOR`,
      `DICE_INK_ACTIVE_COLOR`, plus weather keys from 4.5
- [ ] `persist_write_*` / `persist_read_*` on the watch
- [ ] Settings re-apply on app launch and on app message receipt

**5b — Class and familiar art**
- [ ] Six class silhouettes drawn on the journey token
- [ ] Ten familiar silhouettes for the ribbon corner
- [ ] Faded "disconnected" variant of the chosen familiar

**5c — Color theming**
- [ ] `theme.h/.c` with palette structs and accessors (replace bare `GColor*` constants with `theme_color(THEME_COLOR_DIE_BODY)` etc.)
- [ ] Three or four named presets to start:
  - **Parchment** (current default — warm cream / sepia / red / amber)
  - **Midnight** (deep navy / silver / pale blue / muted gold)
  - **Blood Moon** (dark crimson / bone / ember / gold)
  - **Frostfell** (pale blue / white / steel / icy cyan)
- [ ] Selectable from settings page; persists across launches
- [ ] **Stretch:** per-element color picker (defer unless requested)

**5d — Final polish**
- [ ] RGB backlight tinting matching the active theme (warm orange for Parchment after sunset, cool blue for Midnight, etc.)
- [ ] Edge-case audit: DST, midnight rollover, sleep crossing midnight, missing HR, missing steps
- [ ] Battery-life measurement: target < 5% drain attributable to the watchface

## Open questions

1. **Die geometry vs. art direction.** With facet lines added in Phase 1, decide whether the hour die reads as D12 (correct geometry) or stays ambiguous D12/D20 (iconic-D&D look). Either is supported.
2. **HR "no data" copy.** `—`, `??`, or hide entirely? Decide during Phase 1.
3. **Familiar placement.** Ribbon corner (current) or stat-row center?
4. **Wrist-raise sensitivity.** `accel_tap_service` is the placeholder; true wrist-raise likely needs `accel_data_service` with a low-pass filter on the gravity vector. Decide during Phase 2.
5. **HP framing.** BPM inside the heart with no max (current direction) vs. "72 / max" with a thematic ceiling. Confirm during on-wrist testing.
6. **Sleep-end label timing.** Show "Xh Ym rest" for 2 h after waking, or until first user interaction?
7. **Theme storage budget.** With 4 themes × ~12 colors each, theming is essentially free in memory. Custom-color storage would need careful `persist_write` planning if added later.
8. **Dice-face sprites.** Currently planned to skip — procedural 3D dice with per-value rest rotation and per-face baked numerals look intentional and animate cleanly through physics. Revisit only if testing surfaces a specific reason (e.g., baked-art look the procedural can't match, or a CPU/battery cost we didn't anticipate).
9. **Campfire art.** Not in `resources/` yet. Need 2-state asset (idle tent / lit tent + flame) — flag for sourcing.
10. **Heart sprite.** Procedural reads fine. Skip unless theming requires a specific shape that's hard to draw with polygons.

## Color palette — Parchment preset (default)

| Role           | Pebble color (working)         | Brief target |
| -------------- | ------------------------------ | ------------ |
| Background     | `GColorPastelYellow`           | `#F0DFB4`    |
| Die body       | `GColorLightGray`              | `#E8D4A8`    |
| Outline / ink  | `GColorBlack`                  | `#2A1808`    |
| Facet shadow   | `GColorWindsorTan` (low alpha) | `#8A6838`    |
| Ribbon         | `GColorRajah`                  | `#C8A878`    |
| HP / heart     | `GColorDarkCandyAppleRed`      | `#A02818`    |
| Torch flame    | `GColorOrange`                 | `#E89018`    |
| Torch flame core | `GColorIcterine`             | `#F4C870`    |
| Torch handle   | `GColorWindsorTan`             | `#6A3818`    |
| Trail          | `GColorWindsorTan`             | `#6A4828`    |
| Token          | `GColorOxfordBlue`             | `#185FA5`    |
| Treasure       | `GColorChromeYellow`           | `#D4A018`    |
| Boss sigil     | `GColorDarkCandyAppleRed`      | `#A02818`    |
| Camp tent      | `GColorBulgarianRose`          | (brown)      |

Other presets (Midnight, Blood Moon, Frostfell) land in Phase 5c.

## SDK reference (cheat sheet)

| Need                       | API |
| -------------------------- | --- |
| Per-minute redraw          | `tick_timer_service_subscribe(MINUTE_UNIT, …)` |
| Battery                    | `battery_state_service_subscribe(…)` / `battery_state_service_peek()` |
| Heart rate / steps / sleep | `health_service_events_subscribe(…)`, `health_service_peek_current_value(HealthMetricHeartRateBPM)`, `health_service_sum_today(HealthMetricStepCount)`, `health_service_peek_current_activities()` |
| Tap                        | `accel_tap_service_subscribe(…)` |
| Continuous accel (wrist-raise) | `accel_data_service_subscribe(…)` |
| Bluetooth connection       | `connection_service_subscribe(…)` / `connection_service_peek_pebble_app_connection()` |
| Load a bitmap resource     | `gbitmap_create_with_resource(RESOURCE_ID_*)`, paired with `gbitmap_destroy` |
| Atlas / spritesheet sub-rect | `gbitmap_create_as_sub_bitmap(parent, sub_rect)` — no extra data copy |
| Draw a bitmap              | `graphics_draw_bitmap_in_rect(ctx, bmp, dest_rect)` |
| Rotate a bitmap            | `graphics_draw_rotated_bitmap(ctx, bmp, src_ic, angle, dest_ic)` |
| Animated bitmap sequence   | `bitmap_sequence_create_with_resource(RESOURCE_ID_*)` (APNG) |
| Fixed-point trig           | `sin_lookup`, `cos_lookup`, `TRIG_MAX_ANGLE`, `TRIG_MAX_RATIO` |
| Polygons                   | `gpath_create`, `gpath_draw_filled`, `gpath_draw_outline` |
| Animation                  | `animation_create()`, `animation_set_handlers`, `animation_set_duration`, `animation_schedule` |
| Persistent storage         | `persist_write_int`, `persist_read_int`, `persist_write_string`, etc. |
| App messages (settings)    | `app_message_register_inbox_received`, `app_message_open` |

## Performance budget

| Pass                 | Frequency             | Cost note |
| -------------------- | --------------------- | --------- |
| Minute tick          | 1× / min              | Cheap — text + dashed line redraw + ~100 ms ones-die shake |
| Tens-change shake    | 6× / hour             | Same as above + tens-die shake |
| Hour tick            | 24× / day             | Hour die re-roll (~400 ms / ~12 frames) |
| Wrist-raise roll     | ~50× / day            | Full tumble (~600 ms / ~18 frames). Only when looking. |
| Tap roll             | user-driven           | Same as wrist-raise |
| Heart rate update    | every 5–10 min        | Stat row redraw only |
| Battery state push   | on change             | Stat row redraw only |
| Steps polling        | 1× / min              | Journey strip redraw only |
| Sleep state push     | on change             | Journey strip redraw only |

Target: no full-screen redraws at idle, no animation when the watch is not being looked at, no continuous accel polling above the minimum useful rate.

## Build & run

In CloudPebble: GitHub Repo Sync → build → install on emulator.

Locally:

Unavailable