# Plan — Redesign the HMI placar as a modern sports app

**Date:** 2026-06-07
**User prompt:** *"use frontend design skills to redesign the interface
into a modern sports app"* — then, after a first attempt was
rejected: *"this modern scoreboard show the timer, which we dont have,
but we need to add to the plan."* with a screenshot of a 2014 FIFA
World Cup ARG vs GER scoreboard widget as reference.

The current placar UI is functional but reads as a debug surface: a
flat black background, big bitmap digits, eight pill buttons arranged
in two rows. This plan rebuilds the main view as a **compact, card-
based scoreboard** modelled on the user's reference: pill-shaped
container with a circular minute-timer at the centre, score-on-dark
rectangles per team, status row of controls underneath.

**First attempt (rejected):** the Brasileirão palette + full-screen
split-card layout was a different direction from what the user wanted
(see git stash @{0} for the implementation). Keep the failed attempt
around as a colour-palette reference but discard the layout.

The calibration overlay is intentionally **out of scope** for this
plan — it was just redesigned (white panel + cam-snapshot preview)
and its own redesign would land in a phase 2.

---

## 1. Current vs target

### Today
```
┌──────────────────────────────────────────────────────────────┐
│ gol-cam                                       [PLACAR pill]  │
│                                                              │
│                       0  x  0                                │   ← bitmap digits
│                                                              │
│ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐                          │   ← A+/A-/B-/B+
│ │ A +  │ │ A -  │ │ B -  │ │ B +  │                          │
│ └──────┘ └──────┘ └──────┘ └──────┘                          │
│ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐                          │   ← CAL/START/RESET/CAL
│ │CAL A │ │START │ │RESET │ │CAL B │                          │
│ └──────┘ └──────┘ └──────┘ └──────┘                          │
│                                                              │
│   HOME [IDLE pill]              AWAY [IDLE pill]             │
└──────────────────────────────────────────────────────────────┘
```

### Target (inspiration-driven card scoreboard)

Modelled directly on the user's reference image — a pill-shaped main
scoreboard with a circular timer at the centre, score-on-dark
rectangles for each team. On our 800×480 canvas we have room for the
scoreboard pill on top AND a second pill or row underneath for the
control surface.

```
        ┌─────────────────────────────────────────────┐
        │                                             │
        │  ╔══╗         ╭─────╮          ╔══════════╗ │
        │  ║●●║         │ 12' │   HOME ◆ ║    0     ║ │
        │  ║●●║         │ 45" │          ╚══════════╝ │   ← scoreboard pill
        │  ╚══╝         ╰─────╯          ╔══════════╗ │      y=30..200 (h=170)
        │                                ║   AWAY   ║ │
        │  app icon     timer ring  AWAY ◆ ║    0     ║ │
        │  / logo                          ╚══════════╝ │
        │                                             │
        └─────────────────────────────────────────────┘

        ┌────────────────────┐  ┌──────────────────────┐
        │  ● HOME READY      │  │  ● AWAY READY        │  ← status pills
        │  cam @ .40.90      │  │  cam @ .40.91        │      y=220..300
        └────────────────────┘  └──────────────────────┘

   ┌──┐ ┌──┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌──┐ ┌──┐
   │− │ │+ │ │ CAL A  │ │ ▶ START│ │ RESET  │ │ CAL B  │ │− │ │+ │   ← controls
   └──┘ └──┘ └────────┘ └────────┘ └────────┘ └────────┘ └──┘ └──┘      y=330..460
   A    A                                                  B    B
```

Net visual changes from today:
- **Pill-shaped scoreboard** instead of a flat header + huge digits.
  Rounded ~25–30 px corners. Cream/off-white background mimicking the
  reference. Sits at the top of the screen as the dominant element.
- **Circular minute-timer** (~140 px diameter) centred in the pill.
  Shows `M' SS"` (minute and seconds since START tap). A ring around
  the disc fills in as the timer advances — emerald arc on the right
  side per the reference. **This is new — we have no timer today.**
- **Score-on-dark blocks** per team. Each team gets a horizontal row
  with: team label ("HOME" / "AWAY") + small colored "flag" disc
  (no national flags — just a coloured pill to identify the side) +
  a dark rectangle holding the score in large white digits. Matches
  the reference's "ARG 🏳️ 3" / "GER 🏳️ 1" stacked rows.
- **Separate status pills** for each team below the scoreboard,
  showing cam-state dot + label + cam IP. Replaces the in-card
  status row from the rejected attempt.
- **Control row** of pill-shaped buttons at the bottom — same 8
  controls as today (A−/A+, CAL A, START, RESET, CAL B, B−/B+) but
  styled as discrete pills rather than a ribbon.

---

## 2. Color palette — match the reference

Pulled directly from the user's reference image. RGB565-safe.

- Page background: `0x0500` deep green field (kept from the Brasileirão
  attempt — it complements the cream pills well, same as the
  reference's blurry green stadium backdrop).
- Pill background (scoreboard, status pills, control buttons):
  `0xEF7D` warm cream (slight yellowish tint, soft).
- Score-block background (the dark rectangle holding each digit):
  `0x2104` near-black charcoal.
- Score digits: `0xFFFF` white.
- Team label text on pill: `0x4208` dark slate.
- Timer numerals: `0x2104` near-black.
- Timer ring (progress arc): `0x07E2` emerald green; track:
  `0xC638` light grey.
- Status dots: same as today (green READY, orange CAL, yellow PAUSE,
  red OFFLINE, grey IDLE).
- Action button (START): green `0x0640` bg + white text.
- Destructive button (RESET, `−`): red `0xC000` bg + white text.
- CAL buttons: amber `0xFD20` bg + black text.
- Score-adjust `+`: green; `−`: red. Both narrow.

---

## 3. Typography hierarchy

Arduino_GFX scales by integer multiples of its bitmap font (~6×8
base glyph). Available size tiers: 2 (12×16), 3 (18×24), 4 (24×32),
5 (30×40), 6 (36×48), 8 (48×64).

| Element | Size | Pixels per glyph |
|---|---|---|
| Score digit (in dark block) | 6 | 36×48 — single digit fits in a 70 px wide block |
| Timer minutes ("12'") | 6 | 36×48 |
| Timer seconds ("45\"") | 3 | 18×24 — sits below the minute number, smaller |
| Team label ("HOME", "AWAY") | 3 | 18×24 |
| Status badge ("READY", "OFFLINE") | 2 | 12×16 |
| Cam IP / metadata | 2 | 12×16 |
| Button label | 3 | 18×24 |
| Score-adjuster `+ / −` | 4 | 24×32 (big enough to read) |

---

## 4. Component spec

### Scoreboard pill (y=20–200, x=40–760, w=720, h=180, r=30)
- `fillRoundRect` cream background, white border 2 px.
- Internal layout (left to right):
  - **App badge** (~x=70, y=100, r=50): green-felt circle with
    a tiny soccer-ball glyph (overlapping fillCircles drawn in
    a pentagon-ish pattern — cheap stylization). Replaces the
    reference's FIFA World Cup logo.
  - **Timer disc** (~x=300, y=110, r=70): white circle with
    minute number ("12'") in size-6 black, seconds ("45\"") in
    size-3 grey underneath. Outside the circle, a partial ring
    (drawn via many short arcs) shows progress through the
    notional half (full ring at 45'). Track grey, progress
    emerald.
  - **Score column** (x=470 to 720): two stacked rows.
    - Row 1 (y=45..115): `HOME ● ║ 3 ║` — label size 3 black,
      colored disc (HOME = cobalt), then a dark rectangle
      (x=600..720, h=70) with white size-6 digit centred.
    - Row 2 (y=125..195): `AWAY ● ║ 1 ║` — same layout, AWAY
      colored disc (yellow).

### Match timer state (NEW)
- Tracked in three new globals: `uint32_t matchStartMs`,
  `uint32_t matchPausedAccumMs`, `enum MatchState
  {MS_IDLE, MS_PLAYING, MS_PAUSED}`.
- Behaviour:
  - `MS_IDLE`: display "00' 00\""; ring empty.
  - START tap → `matchStartMs = millis()`, state = MS_PLAYING.
  - PAUSE tap → `matchPausedAccumMs += millis() - matchStartMs`,
    state = MS_PAUSED.
  - RESUME tap → `matchStartMs = millis()`, state = MS_PLAYING
    (the previous elapsed time lives in `matchPausedAccumMs`).
  - RESET tap → `matchPausedAccumMs = 0`, state = MS_IDLE.
  - Display: `elapsedMs = (state == MS_PLAYING)
      ? (millis() - matchStartMs + matchPausedAccumMs)
      : matchPausedAccumMs;`
- Timer redraw runs on a dirty flag set when (a) state changes or
  (b) the displayed minute or second value advances. A 500 ms loop
  poll on the timer is plenty.
- The cams' state is independent of `MatchState`; the timer is
  HMI-local.

### Status pills (y=220–300, two side-by-side)
- Left pill: HOME, x=40..390 (w=350, h=70, r=20).
  Inside: `● READY` size 3 + cam IP size 2 below.
- Right pill: AWAY, x=410..760.
- Same cream pill style as the scoreboard.

### Control row (y=330–460, h=130)
- Eight pill buttons, single row, similar widths to the
  rejected-attempt's footer ribbon:
  - `−` (A score down): x=10, w=60, h=110, r=20, red bg
  - `+` (A score up): x=80, w=60, red bg
  - `CAL A`: x=160, w=130, amber bg, black text
  - `▶ START`: x=300, w=160, green bg, white text (label flips
    to `⏸ PAUSE` / `▶ RESUME` per cam state, same as today)
  - `⟲ RESET`: x=470, w=130, red bg, white text
  - `CAL B`: x=610, w=130, amber bg, black text
  - `−`: x=750, w=60... actually we run out of width here. Tune
    spacing — likely shrink the inner buttons to 110 px or move
    score adjusters to the very edges with smaller widths.

### Touch hit-test
- Score adjusters become 60 px wide × 110 px tall. Still well
  above the GT911's effective resolution.
- CAL buttons at 130 px wide — comfortably tappable.

---

## 5. Implementation breakdown

All work in `src_hmi/main.cpp`. Estimated ~600 LOC churn (more than
the rejected attempt because of the timer state machine + circular
timer rendering).

1. **Palette constants** (~`src_hmi/main.cpp:430`): cream pill bg,
   charcoal score-block bg, slate text, emerald + grey ring colors.
2. **Match-timer state machine** — three new globals
   (`matchStartMs`, `matchPausedAccumMs`, `MatchState matchState`)
   and a helper `matchElapsedMs()` that returns the live elapsed.
   Wire `START`, `PAUSE`/`RESUME`, `RESET` action handlers to
   transition this state. Add a `dirtyTimer` flag set every 500 ms
   from `loop()` so the timer text refreshes once per second
   without redrawing the whole pill.
3. **Replace `drawHeader()`** → no longer needed; replaced by the
   scoreboard pill which IS the top of the screen. Remove the
   header-strip code path entirely.
4. **New `drawScoreboardPill()`** — paints the cream pill, the app
   badge circle, the timer disc with ring (helper
   `drawTimerRing(cx, cy, r, progress01)`), and the two team rows
   (label + flag-disc + dark score-block + white digit).
5. **New `drawStatusPills()`** — two cream pills with status dot,
   state label, cam IP. Replaces `drawSideStatus`.
6. **Reshape the `buttons[]` array** to the new control-row
   coordinates from §4.
7. **`drawButton(idx)`** — keep mostly as-is. Use rounded-rect r=20
   for the pill aesthetic. Already handles START label flip.
8. **`renderFull()`** — clear bg, scoreboard pill, status pills,
   buttons. Drop `drawHeader` + `drawScoreDigits` + `drawSideStatus`
   from the rotation since they're folded into the new helpers.
9. **Dirty-flag wiring** — `dirtyScore` triggers
   `drawScoreboardPill` (score lives inside it). `dirtyTimer`
   redraws ONLY the timer disc, not the whole pill (small
   sub-region repaint).
10. **Glyph helpers** — small drawn primitives for play triangle,
    pause bars, reset spiral. ~30 LOC.

---

## 6. Acceptance criteria

| # | Criterion | How to verify |
|---|---|---|
| AC1 | Scoreboard pill is the dominant element at the top, score digits visible from across the room. | Manual eyeball test |
| AC2 | Timer counts up by seconds after tapping START; pauses on PAUSE; resumes on RESUME; resets on RESET. | Tap START, wait 5 s, tap PAUSE, verify display freezes; tap RESUME, verify resumes; tap RESET, verify back to `00' 00"`. |
| AC3 | Timer ring fills visibly as the timer advances (track grey, fill emerald). | Manual; check both the disc and the ring update. |
| AC4 | HOME / AWAY status pills show correct state (READY / IDLE / CAL / PAUSE / OFFLINE) per cam. | Manual + power-cycle a cam to see the state changes. |
| AC5 | All 8 control buttons fit in the row and respond to taps. | Manual + tap each button. |
| AC6 | Calibration overlay still works (it's drawn on top of the new placar). | Tap CAL A → overlay appears, dismisses cleanly. |
| AC7 | All 70 integration tests still pass (REST surface unchanged). | `python3 tests/integration/test_system.py` |
| AC8 | HMI `/status` soak stays ≥ 80/90 (no regression from the pclk fix). | 90 s × 1 Hz triple-board soak |

---

## 7. Risks

- **Glyph rendering.** Arduino_GFX's default font doesn't carry
  `▶ ⟲ ⏸ ●`. We draw these as primitives (filled triangle,
  arc-segment, two vertical rects, filled circle). Small functions,
  ~10 LOC each.
- **Button text wrapping.** With buttons at 110 px wide and size 3
  text (18 px / char), `CAL A` = 90 px fits, `START` = 90 px fits,
  `RESET` = 90 px fits. Safe.
- **Card border vs touch.** A 14 px rounded corner means the
  hit-test rect overlaps the card visual by ~14 px in the corner.
  Buttons sit OUTSIDE the cards in the footer, so no conflict.
- **Refresh rate.** At 8 MHz pclk (~17 Hz), large fillRounded calls
  on every dirty flag could look sluggish. Audit which areas
  actually need to redraw on score changes (only the inner score
  digit, not the whole card).

---

## 8. Open questions

- **Timer convention.** A button-soccer match isn't a standard
  90-min football match. Options: (a) free-running count-up
  showing `M' SS"`; (b) bounded countdown of N minutes (configurable
  later); (c) showing periods/halves. Default in this plan: free
  count-up — minimal state, no settings UI needed.
- **Timer ring scale.** Default: full ring at 5 min match length
  (which makes the ring visually advance fast enough to feel alive
  in a typical 2-3 min play). The reference image shows 45' full,
  but our matches are much shorter.
- **Team names beyond HOME/AWAY?** Defer — would need a settings
  screen.
- **Glyph rendering for the soccer-ball app badge.** Cheap option:
  just a green-felt circle with a white "G" text overlay. Skip the
  pentagon-pattern complexity.

---

## 9. Pickup steps

1. Confirm palette choice (A / B / C / mix).
2. Add the new color constants.
3. Replace `drawHeader()`, `drawScoreDigits()`, `drawSideStatus()`,
   and the button array.
4. Add the glyph helpers (`drawPlayTriangle`, `drawResetArc`,
   `drawStatusDot`).
5. Build + flash, eyeball each AC.
6. Run integration tests + soak.
7. Commit + push.

---

## 10. Out of scope (deferred)

- Calibration overlay restyle. Currently white panel + cam
  snapshot; works well. Could harmonize with the new palette in
  a phase 2 after live-stream lands.
- Settings screen / team names.
- Real match timer.
- Custom font (would require porting a TTF to a GFXfont header
  and bundling, which is doable but +30 KB of code).
- Animations beyond the existing button-press color flip.
