# D&D Dice Watchface — Development Plan

## Overview

A Pebble Time 2 watchface where the time is displayed via animated polyhedral dice in a TTRPG / parchment aesthetic, with diegetic indicators for heart rate (heart sprite with BPM inside), steps (journey trail with adventurer token), date (parchment ribbon), battery (torch), and Bluetooth (familiar).

## Status

**Phase 1 code is complete; manual emulator state-testing is the remaining gate to closing Phase 1.** Code changes since Phase 0:

- Vertical layout reworked so ribbon, hour die, minute dice, stat row, and journey strip occupy non-overlapping bands (28 / 90 / 48 / 32 / 30 px)
- Hour die has 3 centroid-to-vertex facet spokes; minute kites have a midline + 2 diagonal facets
- Heart resized so a 3-digit BPM fits inside it; BPM number is drawn inside the heart in white; em-dash replaces "0" when no HR reading is available; "HP" suffix removed
- Torch has 4 visually distinct battery states (full flame + core highlight, half flame, ember cluster, single faint ember at the wick); percent label stacks beneath the torch
- Ribbon is a 6-vertex tapered-scroll polygon with fold-crease accents at each end
- Familiar placeholder is a visible silhouette (circle + eye) when Bluetooth is connected; faded windsor-tan outline when disconnected
- Adventurer token nudges 4 px off-camp when `steps <= 0` so it doesn't read as stuck on the trailhead

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
└── tumble.h/.c   — per-die animation state machines and tumble curve
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

Polish-first means the static face is trusted before motion gets added. Animation lives in a new `tumble.h/.c` module.

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
- [ ] Rotation + face-value flicker every 2–3 frames + ease-out settle
- [ ] Flash highlight on settle (one-frame palette swap of the die fill)
- [ ] Time-sampling rule: sample wall clock at roll-start, animate for fixed duration, snap to the sampled time on settle

**Journey strip animation**
- [ ] Token slides smoothly between step-count updates (not teleport)
- [ ] Sleep transition: token "walks to camp" when sleep starts; "departs camp" when waking
- [ ] Wake label: `Xh Ym rest` fades in under camp for ~2 h after waking

### Phase 3 — Stub-sprite pipeline check

Single tracer-bullet asset to de-risk the swap path.

- [ ] Bake one placeholder PNG (flat gray pentagon with "X") into `resources/`
- [ ] Wire through `package.json` → `gbitmap_create_with_resource`
- [ ] Confirm `graphics_draw_rotated_bitmap` renders correctly during a tumble
- [ ] Confirm resource memory budget is reasonable

### Phase 4 — Sprite delivery (core)

Priority block — everything the watchface needs to look "shipped" before configurables.

- [ ] 28 dice face sprites (hour, tens, ones) per the sprite manifest
- [ ] Heart sprite + 4 torch states + token base
- [ ] Treasure chest, campfire idle, campfire lit, boss sigil
- [ ] Swap `die_draw` body to call `graphics_draw_rotated_bitmap`
- [ ] Strip polygon math from `die.c`
- [ ] Wire heart, torch, milestone sprites into `widgets.c` and `journey.c`
- [ ] Optional: static parchment background bitmap (200 × 228); keep procedural fill as fallback if memory is tight
- [ ] Verify resource-bundle size and runtime memory

### Phase 5 — Configurables and theming

**5a — Settings infrastructure**
- [ ] PebbleKit JS settings page in `src/pkjs/index.js`
- [ ] Message keys: `CLASS`, `FAMILIAR`, `STEP_GOAL`, `SLEEP_OVERRIDE`, `THEME`
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
| Rotate a bitmap            | `graphics_draw_rotated_bitmap(ctx, bmp, src_ic, angle, dest_ic)` |
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