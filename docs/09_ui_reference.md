# UI reference — screens, menu structure, controls

Living description of what the slider's display actually shows, kept in sync with
`firmware/src/display.cpp` (drawing) and `firmware/src/menu.cpp` (navigation). The
screenshots in `docs/ui/` are generated from the same geometry, palette, glyph bitmaps and
status-bar icons the firmware uses — they are not mockups.

> **For the designer:** every image in `docs/ui/` is a 1:1 PNG at the panel's native
> 160×128. View them scaled by an integer factor (2×/4×/8×) with nearest-neighbour
> filtering; smoothing will misrepresent how this looks on the real screen. Constraints and
> known rough edges are listed in [Notes for redesign](#notes-for-redesign).

---

## 1. The canvas

| Property | Value |
|---|---|
| Panel | ST7735, **160×128**, landscape (`setRotation(1)`) |
| Colour depth | RGB565 (16-bit) |
| Font | Adafruit GFX classic 5×7 glyph in a **6×8 cell** — no other font is loaded |
| Text sizes in use | 1 (6×8 px/char), 2 (12×16), 3 (18×24) |
| Refresh | ~10 Hz, and only when something changed |
| Composition | Full 160×128 RGB565 `GFXcanvas16` (40 KB), flushed atomically over SPI |
| Backlight | PWM, user-adjustable 10–100 % |

Because the only font is a 6 px-wide bitmap, **text width is always `chars × 6 × size`
pixels**. That gives hard limits worth designing against:

| Text size | Chars per full width (160 px) |
|---|---|
| 1 | 26 |
| 2 | 13 |
| 3 | 8 |

### Standard layout bands

Nearly every screen is built from three bands:

```
y=0    ┌──────────────────────────────┐
       │ status bar (14 px)           │  BG_PANEL + 1 px DIVIDER underneath
y=14   ├──────────────────────────────┤
       │                              │
       │ content (98 px)              │  BG_BASE
       │                              │
y=112  ├──────────────────────────────┤
       │ button hints (16 px)         │  BG_PANEL + 1 px DIVIDER on top
y=128  └──────────────────────────────┘
```

The status bar shows the terse title `SLIDER` in cyan and a battery group (right): a drawn
18×9 3-segment glyph plus a `%.1f`-formatted voltage number,
with a small amber bolt to its left when a charger is detected. List-style screens replace
the title band with the screen's own name but **keep** the battery group — every screen
shows it, including Error.

The battery glyph is drawn with primitives (`drawRect`/`fillRect`), not a bitmap: 3 segments
fill green above 66/33 %, amber below 25 %, red at 0 %. This replaced the old 5-level
`bat_0..bat_4` RGB565 bitmap set specifically to drop its "opaque black background" artifact
(see [Notes for redesign](#notes-for-redesign)).

---

## 2. Controls

Three physical inputs. **BTN1 and BTN2 sit on the left flank of the enclosure, the encoder
on the right** — which is why the hint bar groups both button hints on the left and the
encoder hint on the right, rather than spreading them left/middle/right.

| Hint bar notation | Input |
|---|---|
| `▶` / `⏸` | BTN1, short press |
| `☰` | BTN2, short press |
| `(o)` | Encoder press |

Every button hint combines the physical glyph with a terse action label (`▶ STOP`,
`☰ MENU`, and so on). `▶`/`⏸`/`☰` are hand-drawn (filled triangle, two bars, three
horizontal lines): the bitmap font has no such glyphs, so these are vector primitives, not
characters. `☰` never changes shape or meaning.
`▶`/`⏸` is the one hint icon with two states, since BTN1 doubles as the actual start/stop
toggle on Main and Ping-Pong: `▶` (play) when idle — pressing starts the move — `⏸` (pause)
while running — pressing stops it. This redraws the instant the running state flips, not
just on screen entry, so it can't go stale while sitting on the same screen.

Icon colour is normally `TEXT_SEC` (dimmed further to `TEXT_DIM` when the action is
unavailable, e.g. during homing), but a few screens tint one or both to the accent that
matches the action: Main (`▶`/`⏸` cyan, `☰` amber), Ping-Pong (`▶`/`⏸` cyan), Error (`☰` red
for "HOME").

Rotation is never shown in the hint bar — it is always "adjust the thing on screen".

General rules, unless a screen overrides them:

- **BTN1** — back one level / cancel; on Main and Ping-Pong, start/stop instead (see above).
- **BTN2** — jump to the main screen. Held for 800 ms — start homing (via a confirm dialog).
- **Encoder rotate** — change the value or move the selection; **press** — confirm/select.

One detent of the encoder equals one step. (The quadrature decoder counts four transitions
per detent and divides them down; sub-detent jitter never reaches the UI.)

---

## 3. Screen map

```
MAIN ─┬─ (o) ──────────────► GO TO POSITION ──► (back to MAIN)
      ├─ 2 ────────────────► MENU
      └─ 2 held ───────────► HOMING CONFIRM ──► CALIBRATION

MENU (full-screen carousel, one item at a time, dots show position)
 ├─ Manual Move  ─────────► MANUAL MOVE
 ├─ Position     ─────────► GO TO POSITION
 ├─ Ping Pong    ─────────► PING PONG
 ├─ Calibration  ─────────► CALIBRATION
 └─ Settings     ─────────► SETTINGS
                             ├─ Motion    ──► speed / ramp / microsteps /
                             │                endstop mode / homing speed /
                             │                pingpong start           → VALUE EDIT
                             ├─ Sleep     ──► sleep timeout / ADXL sensitivity → VALUE EDIT
                             ├─ System    ──► motor current / reset calibration /
                             │                reset error / theme / brightness /
                             │                speaker on-off            → VALUE EDIT
                             │                ├─ Diagnostics ──► DIAGNOSTICS
                             │                └─ Motor Test  ──► MOTOR TEST
                             └─ Wireless  ──► Bluetooth (On/Off)  → VALUE EDIT
                                              WiFi ──► WIFI MODE
                                                        ├─ Off
                                                        ├─ Connect ──► SCAN ──► PASSWORD ──► CONNECTING
                                                        └─ Hotspot ──► PASSWORD

ERROR      — entered automatically from any screen when a fault occurs
SLEEP      — backlight off entirely; nothing is drawn
OTA UPDATE — takes over the screen while firmware is being flashed over WiFi
```

---

## 4. Screens

### 4.1 Main

![main](ui/main.png)

The default screen. Big number is the current position in steps; the bar above it shows
where the carriage is between the two endstops (`E1` … `E2`), animated with easing.

| Input | Action |
|---|---|
| Rotate | Speed 1–100 % |
| Press | Open Go to Position |
| BTN1 (`▶`/`⏸`, cyan) | Start / stop movement in the last direction |
| BTN2 (`☰`, amber) | Open the menu |
| BTN2 held | Start homing (via confirm) |

States shown in the status line, each with its own colour: `IDLE` (green), `MOVING` /
`GO TO` (cyan), `HOMING` / `PARKING` (amber), `ERROR` (red), `SLEEP` (dim), `PING PONG`
(cyan).

| Moving | Not calibrated |
|---|---|
| ![main moving](ui/main-moving.png) | ![main uncalibrated](ui/main-uncalibrated.png) |

While moving, a direction marker (`>>` / `<<`) appears and the position bar turns cyan.
Until the slider has been homed, the position is suffixed with `*` and the bar stays empty
— travel is unknown, so there is nothing to show a fraction of.

### 4.2 Menu

![menu](ui/menu.png)

Full-screen carousel, one item at a time: a hand-drawn vector icon (up to 60×60 px, primitives only
— no such bitmap font glyphs exist) centred in the content area, the item's name below it,
`<`/`>` chevrons at the content edges (dimmed at the first/last item — navigation itself
still wraps around, the dim chevron is just a "you're at an end" cue), and pagination dots
in the footer showing position among 5 items.

Items, in order: **Manual Move**, **Position**, **Ping Pong** (cyan icon), **Calibration**
(amber icon), **Settings** (purple icon).

| Input | Action |
|---|---|
| Rotate | Move between items |
| Press | Open the item |
| BTN1 / BTN2 | Back to main |

### 4.3 Ping Pong

No screenshot yet. Bounces the carriage between the two endstops forever, starting from a
configurable reference point (Settings → Motion → PingPong Start: Center / Endstop 1 /
Endstop 2) — this is a plain reskin of the existing bounce behavior, **not** the bounded-run
(A/B markers inside travel, dwell pause at each end, lap counter) concept sketched in the
2026-08 design handoff; that needs its own bounds-setting screen, which the handoff itself
left undesigned, and was explicitly deferred.

| Input | Action |
|---|---|
| Rotate | Speed, live |
| BTN1 (`▶`/`⏸`, cyan) | Start (heads to the reference point, then bounces) / stop |
| BTN2 | Back to the menu |

The screen uses the turn-5 hierarchy: full-width travel bar, large live position, direction
or approach state at right, and a compact speed/start/run row. Because the implemented mode
still uses the physical endstops as its bounds, its green markers sit at the two ends rather
than pretending that configurable A/B bounds exist.

### 4.4 Manual Move

| Stopped | Running |
|---|---|
| ![manual move](ui/manual-move.png) | ![manual move running](ui/manual-move-running.png) |

| Input | Action |
|---|---|
| Rotate | Speed, live |
| Press | Reverse direction (reverses on the fly if already moving) |
| BTN1 | Back to the menu |
| BTN2 | Main screen |

### 4.5 Go to Position

| Calibrated | Not calibrated |
|---|---|
| ![go to position](ui/go-to-position.png) | ![go to position uncalibrated](ui/go-to-position-uncal.png) |

| Input | Action |
|---|---|
| Rotate | Target position, ~1 % of total travel per detent |
| Press | Start moving to the target |
| BTN1 | Back to wherever this was opened from |
| BTN2 | Main screen |

Without calibration the slider has no coordinate system, so the screen offers nothing but
a warning.

### 4.6 Calibration / homing

| Idle | Homing in progress |
|---|---|
| ![calibration](ui/calibration.png) | ![calibration homing](ui/calibration-homing.png) |

Homing runs in six phases (seek END1 → back off → seek END2 → back off → centre → done),
shown as a phase name and an amber progress bar. During homing every control is locked
except **BTN2 held = abort**; the measured travel and centre are stored afterwards.

### 4.7 Settings

| Settings | Motion | Sleep |
|---|---|---|
| ![settings](ui/settings.png) | ![motion](ui/settings-motion.png) | ![sleep](ui/settings-sleep.png) |

| System | Wireless |
|---|---|
| ![system](ui/settings-system.png) | ![wireless](ui/settings-wireless.png) |

All settings screens share one list widget: 20 px rows, label left, current value right.
The selected row is a full-row highlight with a border in the selection colour and a
leading `>` marker before the label (label/value text itself stays plain white/gray, not
tinted — this replaced an earlier style that coloured the selected row's text and marked it
with a 1 px accent bar on the left edge instead). **Five rows fit at a time**; longer lists
scroll (System has eight entries as of the Speaker setting).

### 4.8 Value editor

| Numeric | Named options |
|---|---|
| ![value edit numeric](ui/value-edit-numeric.png) | ![value edit named](ui/value-edit-named.png) |

Numeric values are centred at text size 3, with a short unit suffix (`mA`, `%`, `us`, `min`)
in `TEXT_SEC` at size 1 right after the number where the setting has an obvious one (Speed,
Homing Speed, Sleep Timeout, Motor Current, Brightness — Ramp Steps and Microsteps are
unitless). Below that, bare min/max numbers flank a progress bar directly (no "MIN"/"MAX"
caption words — one compact row instead of two). Named values (endstop mode, theme, ADXL
sensitivity, PingPong Start, Bluetooth/Speaker on-off) use size 2 and no bar — the longest
name in use, `High Contrast`, is 13 characters and only fits at that size.

| Input | Action |
|---|---|
| Rotate | Change value (microsteps step ×2 / ÷2) |
| Press | Save |
| BTN1 | Cancel |
| BTN2 | *Suppressed* — prevents discarding an edit by reflex |

### 4.9 Wireless

| WiFi mode | Scanning | Results |
|---|---|---|
| ![wifi mode](ui/wifi-mode.png) | ![scanning](ui/wifi-scanning.png) | ![scan results](ui/wifi-scan-results.png) |

Open networks are labelled `open` next to their signal strength. The last row is always
`Rescan`.

**Password entry** — the encoder is the only text input, so characters are picked from a
74-character wheel:

![password entry](ui/password-entry.png)

The cyan block is the cursor showing the pending character. Existing saved passwords are
pre-filled, so the common case is simply pressing BTN2.

| Input | Action |
|---|---|
| Rotate | Cycle the pending character |
| Press | Commit it and advance |
| BTN1 | Delete a character (cancels the screen when empty) |
| BTN2 | Submit (blocked for 1–7 characters: WPA2 needs at least 8) |

| Connecting | Failed |
|---|---|
| ![connecting](ui/wifi-connecting.png) | ![failed](ui/wifi-failed.png) |

Connection attempts time out after 15 s.

### 4.10 Dialogs and system screens

| Homing confirm | Error |
|---|---|
| ![homing confirm](ui/homing-confirm.png) | ![error](ui/error.png) |

Homing Confirm is the one screen that asks a real yes/no question, so its footer is two
bordered "chip" buttons (`CANCEL` / `START`, amber border on the latter) instead of the
usual icon+hint row every other screen uses.

The error screen is entered automatically from any screen and its background pulses between
the base colour and dark red at 2.5 Hz. Errors: BLE connection lost, homing failed,
unexpected endstop. Footer: `▶ RESET` (dim) / `☰ HOME` (red).

**OTA update** — shown while firmware is flashed over WiFi. The main loop is fully blocked
during the transfer, so this screen is painted directly from the update callbacks:

![ota update](ui/ota-update.png)

### 4.11 Service screens

Reached from Settings → System. Both suppress all normal navigation, so nothing can be
triggered by accident while probing hardware; **hold BTN1+BTN2 for 3 s to leave**.

| Diagnostics | Motor test |
|---|---|
| ![diagnostics](ui/diagnostics.png) | ![motor test](ui/motor-test.png) |

**Diagnostics** reads the GPIO pins directly, bypassing debouncing and the menu entirely,
so a dead switch can be told apart from a software fault. Both status LEDs mirror the
endstops here so they can be verified too.

**Motor test** jogs the motor on the bench: BTN1/BTN2 tap to run backward/forward, tapping
again (or the encoder press) stops, rotation sets speed live. Endstops are still respected.

---

## 5. Themes

Selectable in Settings → System → Theme, applied instantly and remembered.

| | Dark (default) | Light | High Contrast |
|---|---|---|---|
| Main | ![dark](ui/main.png) | ![light](ui/main--light.png) | ![hc](ui/main--high-contrast.png) |
| Menu | ![dark](ui/menu.png) | ![light](ui/menu--light.png) | ![hc](ui/menu--high-contrast.png) |
| System | ![dark](ui/settings-system.png) | ![light](ui/settings-system--light.png) | ![hc](ui/settings-system--high-contrast.png) |

### Palette

Colours are RGB565. Note the display cannot show more than 5/6/5 bits per channel, so
fine gradients between near neighbours will band.

| Role | Dark | Light | High Contrast |
|---|---|---|---|
| `BG_BASE` — page background | `#0A0C14` | `#F4F6FA` | `#000000` |
| `BG_PANEL` — status/hint bars | `#111827` | `#E7ECF5` | `#000000` |
| `BG_CARD` — tiles, bar troughs | `#1C2540` | `#FFFFFF` | `#000000` |
| `BG_HIGHLIGHT` — selected row/tile | `#283255` | `#D6E4FF` | `#FFFFFF` |
| `COL_SEL` — selected text/border | `#00D4FF` | `#0088B3` | `#000000` |
| `COL_CYAN` — motion / active | `#00D4FF` | `#0088B3` | `#00FFFF` |
| `COL_GREEN` — idle / success | `#00E87A` | `#128A4A` | `#00FF00` |
| `COL_AMBER` — homing / warning | `#FFA020` | `#B36B00` | `#FFEA00` |
| `COL_RED` — error / endstop hit | `#FF2840` | `#C81E3A` | `#FF0000` |
| `TEXT_PRI` — primary text | `#FFFFFF` | `#12141C` | `#FFFFFF` |
| `TEXT_SEC` — secondary text | `#7A90BB` | `#4A5670` | `#E6E6E6` |
| `TEXT_DIM` — hints, disabled | `#3A4A66` | `#8B96AC` | `#AAAAAA` |
| `DIVIDER` — 1 px rules | `#1E2E48` | `#D0D8E4` | `#666666` |

High Contrast deliberately **inverts the selection** (white block, black text) instead of
tinting it, because every accent there is already a saturated colour on black.

A 2026-08 external UI redesign handoff (mockups at real 160×128, turns 3-5) specified this
exact Dark palette independently, hex-for-hex — confirmed by converting every value back to
RGB565 and comparing. So the redesign changed screen layouts, icons, and content density,
not colour.

---

## 6. Notes for redesign

Things worth knowing before proposing changes — constraints first, then existing weak spots.

**Hard constraints**

1. **One bitmap font, three sizes.** There is no proportional font and no font scaling
   beyond integer multiples. Any type hierarchy must come from sizes 1/2/3, colour and
   spacing. Adding a font costs flash, and the build is at ~86 % of its partition.
2. **No transparency, no antialiasing.** Everything is hard-edged; overlapping elements
   must be composed by drawing order.
3. **The frame buffer is finite.** A full RGB565 `GFXcanvas16` consumes 40 KB of heap. All
   widgets compose into it, then `flushFrame()` transfers the finished frame in one SPI
   transaction, so intermediate clears are never visible. A second full buffer would be an
   unjustified extra 40 KB on this non-PSRAM ESP32.
4. **Icons should stay as primitives, not bitmaps or text glyphs.** Photographic/AI-rendered
   icons were illegible at 32 px and cannot follow the theme; the bitmap font also has none
   of the glyphs (`▶ ⏸ ☰ ‹ › ✓`) a design mockup might specify as literal Unicode text. The
   current UI hand-draws all of it with `drawLine`/`drawRect`/`drawCircle`/`fillTriangle` —
   button hints, the 5 menu carousel icons, page dots, chevrons.

**Deviations from the 2026-08 redesign handoff**
- **Ping-Pong stayed the existing simple bounce** (reskinned to the new visual language, see
  §4.3) instead of the handoff's bounded-run concept (settable A/B markers, dwell pause,
  lap counter) — a real new feature, and the handoff's own README admits the screen for
  setting A/B was never designed. Deferred by explicit decision, not an oversight.
- **Per-icon hint colour is applied only where the handoff was unambiguous** (Main, Error,
  Ping-Pong — see §2), not hex-matched screen-by-screen; the handoff's own colour choices
  there read as bespoke per screen rather than a strict system (e.g. BTN2 is amber on Main
  but plain gray on Ping-Pong for the same "go back" action).
- **"10 px" text was approximated at size 1 (6×8).** The bitmap font only scales in integer
  multiples of 6×8 — there is no size between 1 (8 px-ish) and 2 (16 px) to match what the
  mockup's CSS called "10px" for menu titles/secondary values.

**Known weak spots**

- **High Contrast flattens low-priority fills.** `BG_CARD` and `BG_BASE` are both pure black
  there, so hierarchy depends more heavily on outlines and the selected carousel icon.
- **The hint bar is still width-constrained.** The 16 px band gives the glyphs enough
  height, but the right-aligned encoder action text can still collide with a long menu
  title on the busiest screens.
- **Mixed information density.** The service screens are deliberately dense text dumps,
  while the main screen is very sparse. They were designed at different times and do not
  share a visual system.
- **No transition animation.** Screens swap instantly. Only the position bar eases, and the
  spinner/error pulse animate.

**Regenerating the screenshots**

```bash
python3 tools/ui_preview/extract_assets.py       # only if the font or icons changed
node tools/ui_preview/render_node.js              # regenerate every 1:1 docs/ui PNG
```

`tools/ui_preview/index.html` re-implements the Adafruit GFX primitives the firmware draws
with, so it must be updated alongside `display.cpp` for these images to stay truthful.
