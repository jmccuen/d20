# D&D Dice Watchface

**Platform:** Pebble Time 2 — 200×228 px, 64-color e-paper, touchscreen, RGB backlight, heart-rate sensor.

## Overall vibe

A TTRPG character sheet collided with a dungeon map and got etched onto aged parchment. The watchface reads like a snapshot of a character mid-adventure: metallic dice show the time as if just rolled, the journey strip shows where the party has walked today, the torch indicates how long until the lantern needs replenishing. Everything is diegetic — no element is labeled "Battery" or "Heart Rate"; there is a torch, and there is a heart.

## Visual style

Dice are rendered to read as 3D polyhedra in the spirit of Baldur's Gate 3's dice roller — flat-shaded metallic sprites with rotation transforms during animation. The display is fundamentally 2D; the "3D" look comes from the art direction (shading, edge wear, weight, highlights) rather than literal depth rendering.

Backgrounds and decorative elements use sprites where art is provided, with polygon fallbacks during procedural development. The parchment background is a single static-sized bitmap at 200×228; no 9-slice scaling is needed since the display is fixed-resolution.

## Color palette (Parchment preset, default)

The default theme. Three additional presets ship as selectable themes (Midnight, Blood Moon, Frostfell).

| Role              | Hex       | Notes                                  |
|-------------------|-----------|----------------------------------------|
| Parchment cream   | `#F0DFB4` | screen background                      |
| Aged scroll       | `#C8A878` | date ribbon, secondary surface         |
| Sepia ink         | `#2A1808` | text, outlines                         |
| Faded brown       | `#6A4828` | dashed trails, dividers                |
| Facet shadow      | `#8A6838` | inner die lines (low opacity)          |
| Blood red         | `#A02818` | HP heart                               |
| Torchflame outer  | `#E89018` | battery flame body                     |
| Torchflame core   | `#F4C870` | flame highlight                        |
| Token blue        | `#185FA5` | adventurer figure                      |
| Treasure gold     | `#D4A018` | milestone accent                       |

Pebble's 64-color palette can't reproduce these hex values exactly; the final `GColor*` mappings live in `plan.md`.

## Typography

- **Dice numerals, BPM, percent:** bold serif — IM Fell English, Cinzel, or Georgia Bold as fallback.
- **Date and small labels:** same family, regular weight.
- All copy in sentence case. No all-caps. No invented fantasy month names.

## Layout — top to bottom inside the 200×228 screen

**1. Date ribbon (~28 px tall).** A parchment-darker horizontal band across the full screen width, tapered at both ends to feel like an unfurled scroll. Centered text: `Day 147 · 26 May` (day-of-year, then real calendar date). Small triangular fold notches at each end. The Bluetooth familiar sits in the right corner of the ribbon.

**2. Hour die (~95 px tall, centered).** A large dodecahedron-flavoured pentagon with three faint interior lines hinting at hidden facets. Body in dice cream, sepia outline at ~2.5 px stroke. The face shows the current hour 1–12 in a large bold serif numeral.

**3. Minute dice (~60 px tall, two side by side).** Two smaller D10s rendered as kite/diamond shapes.
- Left die (tens) shows `00`, `10`, `20`, `30`, `40`, or `50`.
- Right die (ones) shows `0`–`9`.

Same flat-shaded style as the hour die, single horizontal interior line suggesting the trapezohedron midline.

**4. Divider (~5 px tall).** A dashed brown cord running across the screen, separating the time region from the stat row.

**5. Stat row (~50 px tall, two items evenly spaced).**
- **Left — heart rate.** A stylized heart silhouette in blood red with sepia outline, sized to fit a 3-digit BPM internally (minimum interior box ~24 × 14 px). Current heart rate rendered inside the heart in bold serif (e.g. `72`). No "HP" suffix. When no sensor reading is available, render `—` inside the heart, not `0`.
- **Right — torch / battery.** A wall-torch silhouette: sepia handle below, amber flame body with a bright core above. Battery percent beneath in bold serif (e.g. `85%`). Flame height scales with battery: full flame ≥40%, half flame 20–40%, embers only 5–20%, dark wick <5%. Drawn with polygons if no sprite is available.

**6. Journey strip (~30 px tall, bottom edge).** A dashed brown trail running left to right with three milestone markers:
- **Campfire** at the trailhead (left) — small tent triangle with sepia outline; a small flame appears above when in sleep mode.
- **Treasure chest** at midpoint — gold rectangle with a darker band.
- **Boss sigil** at the right end — red triangle or rune.

An adventurer token slides along the trail as steps accumulate. Position = `(steps / step_goal) × trail_length`, clamped to the strip. The selected class silhouette renders inside or just above the token.

## States and behaviors

**Idle (default, watch not being looked at).** Static. E-paper holds the image at zero power; no animation. Lean into this.

**Wrist-raise or screen tap.** Full ceremonial roll across all three dice in BG3 style — tumble + face-value flicker every 2–3 frames + ease-out settle + brief flash highlight. Lands on the current time. ~600 ms.

**Hour change (XX:00).** Hour die alone re-rolls in place with the same tumble curve. ~400 ms.

**Minute change.** A 100 ms settle-shake on the changed die only — the ones die every minute, the tens die every 10 minutes. No full tumble. The full ceremonial roll is reserved for moments the user can actually see (wrist-raise, tap, hour change), to preserve battery life.

**Long rest — sleep detected via HealthService.**
- Adventurer token returns to the campfire at the trailhead.
- A small flame appears above the tent.
- A tiny `Z` floats above the token.
- The dashed trail is rendered at ~50% opacity for the duration of the rest.
- For the first ~2 hours after waking, a small `8h 12m rest` label appears under the camp, then fades.

**Low battery.** Torch flame shrinks per the stat row spec. Below 5%, the flame is replaced with a faint ember dot at the wick.

**Bluetooth familiar.** A small animal silhouette sits to the right of the date on the ribbon. When the phone is connected, the familiar is present and alert (open eye dot). When disconnected, it is gone or rendered as a faded outline.

## Configurables

**Theme** — color palette preset. The user picks one; the watchface reskins itself accordingly.
- Parchment (default) — warm cream / sepia / red / amber
- Midnight — deep navy / silver / pale blue / muted gold
- Blood Moon — dark crimson / bone / ember / gold
- Frostfell — pale blue / white / steel / icy cyan

**Adventurer class** — selects the journey-token silhouette:
- Fighter — sword raised
- Wizard — pointed hat and staff
- Rogue — hood and daggers
- Cleric — mace and shield
- Ranger — drawn bow
- Barbarian — axe and rough outline

Each silhouette is designed to read clearly at roughly 8×10 px against parchment.

**Familiar** — Bluetooth status creature: raven, owl, cat, bat, fox, rat, frog, hawk, snake, or wolf pup.

**Daily step goal** — integer, scales the trail length. Default 10,000.

**Wake/sleep override** — optional manual toggle for users whose automatic sleep detection is unreliable.

## Sprite assets

Non-dice sprites used by the watchface (dimensions in `plan.md` sprite manifest):
- **Parchment background** — single static 200 × 228 bitmap, optional polish
- **Ribbon parchment** — 200 × 28, optional polish
- **Heart** — sized to fit 3-digit BPM
- **Torch** in 4 states (full / half / embers / dark wick)
- **Adventurer token base** + 6 class silhouette overlays
- **Familiar** silhouettes (10 creatures)
- **Treasure chest** (mid-trail milestone)
- **Campfire idle** and **campfire lit** (trailhead, awake vs. sleep)
- **Boss sigil** (end of trail)

Plus 28 dice face sprites (12 hour + 6 tens + 10 ones).

Default shapes will be drawn until sprites are available.

## Copy rules

- Sparse, slightly archaic but always readable at a glance.
- Date format: `Day N · DD Mon` using real-world months.
- Never label icons with words like "Battery" or "Heart Rate" — the iconography carries the meaning.
- Numbers always shown as integers. No decimals on the watchface.

## Reference readout for the mockup

A complete sample state to render:

> `10:25`, `Day 147 · 26 May`, `72` inside the heart, `85%` torch, ranger class, raven familiar, `4,287` of `10,000` steps, awake (token roughly one-third along the trail between camp and chest), Parchment theme.
