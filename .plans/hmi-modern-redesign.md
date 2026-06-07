# Plan — Redesign the HMI placar as a modern sports app

**Date:** 2026-06-07
**User prompt:** *"use frontend design skills to redesign the interface
into a modern sports app"*

The current placar UI is functional but reads as a debug surface: a
flat black background, big bitmap digits, eight pill buttons arranged
in two rows. This plan rebuilds the main view as a broadcast-style
scoreboard with team identity, status badges, a hierarchy of
typography, and a control ribbon — the kind of look a sports app
(ESPN, FotMob, FIFA Companion) would ship.

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

### Target (broadcast-style two-team layout)
```
┌──────────────────────────────────────────────────────────────┐
│ ⚽ gol-cam · futebol de botão              ● LIVE  · 03:24   │   60 px header
├──────────────────────────────────┬───────────────────────────┤
│                                  │                           │
│   HOME                           │                AWAY       │
│                                  │                           │
│      ┌─────────────┐             │   ┌─────────────┐         │   team cards
│      │             │             │   │             │         │   330 px tall
│      │      0      │             │   │      0      │         │
│      │             │             │   │             │         │
│      └─────────────┘             │   └─────────────┘         │
│                                  │                           │
│   ●  READY                       │   ●  READY                │
│   cam @ 192.168.40.90            │   cam @ 192.168.40.91     │
│                                  │                           │
├──────────────────────────────────┴───────────────────────────┤
│  ─  +    CAL A    ▶ START    ⟲ RESET    CAL B    ─  +        │   90 px footer
└──────────────────────────────────────────────────────────────┘
```

Net visual changes:
- **Header band** with a small app icon, project subtitle, live-state
  badge (replaces the single PLACAR pill).
- **Two team cards** with distinct background tints, MASSIVE score
  digits (size 14 instead of 12), team labels in caps above, status
  dot + cam IP below. Cards have rounded corners and a subtle
  border.
- **Match-state divider** ("VS" or just the colon-separator) between
  the cards. Tiny match-timer placeholder (`03:24`) in the header.
- **Single-row footer ribbon** for ALL controls (today there are
  two rows). Score adjusters become smaller "− / +" icons hugging
  each team's edge of the ribbon; CAL / START / RESET sit in the
  middle, more prominent.
- **Status pills** become badges: a coloured dot + label + cam IP,
  inside each team card. The HOME/AWAY headings move into the card
  itself.

---

## 2. Color palette — pick one

Three directions, all RGB565-safe on the panel. Render the score
digits in white on coloured card backgrounds for max readability
across the room.

### Option A — "Stadium night"
- Background: `0x0841` very dark navy
- Header strip: `0x18C3` slate
- HOME card: `0x044D` teal-blue
- AWAY card: `0xC183` deep amber
- Score digits / labels: `0xFFFF` white
- Accent (LIVE pill, action buttons): `0x07E0` green
- Destructive (RESET, − minus): `0xC000` brick red
- *Reads like a night-game broadcast. Best contrast on the panel.*

### Option B — "Brasileirão" *(my recommendation)*
- Background: `0x0260` deep green felt (nod to button-soccer's
  fabric tables)
- Header: `0x0481` slightly lighter green
- HOME card: `0x041F` cobalt blue
- AWAY card: `0xFE60` warm yellow (digits in black on this card
  for contrast)
- Score (HOME): white
- Score (AWAY): black
- Accent: `0xFFFF` white
- Destructive: `0xF800` red
- *Direct nod to the Brazilian flag + classic green table. Most
  on-brand for a futebol-de-botão device.*

### Option C — "Modern minimal"
- Background: `0x10A2` charcoal
- Header: `0x18C3` slate
- HOME + AWAY cards both: `0xFFFF` white with thin colored border
  (HOME blue, AWAY orange)
- Score: HOME blue / AWAY orange digits ON the white cards
- Accent: `0x07E0` green for primary buttons
- Destructive: `0xF800` red
- *Cleanest, closest to a modern desktop sports app. Less
  "celebrate the sport" energy than A or B.*

---

## 3. Typography hierarchy

Arduino_GFX scales by integer multiples of its bitmap font (~6×8
base glyph). Available size tiers in use today: 2 (12×16), 3
(18×24), 4 (24×32). Cleaner if we use these consistently:

| Element | Size | Pixels per glyph |
|---|---|---|
| Score digits | 14 | 84×112 (target: 1 wide-digit ~84 px, 2 wide ~168 px). Matches today's "0 x 0". |
| Team label ("HOME", "AWAY") | 4 | 24×32 |
| Header title ("gol-cam · futebol de botão") | 2 | 12×16 |
| Status badge ("READY", "OFFLINE") | 3 | 18×24 |
| Cam IP / metadata | 2 | 12×16 |
| Button labels | 3 | 18×24 |

---

## 4. Component spec

### Header strip (60 px, y=0–60)
- Background: header color from palette.
- Left: small `⚽` glyph (could be a fillCircle + drawTriangle for a
  cheap ball icon) + "gol-cam · futebol de botão" in size 2.
- Right: live-state badge = filled rounded rect, ~140 px wide, with
  `●` indicator dot + state text (`LIVE` / `IDLE` / `PAUSE` /
  `OFFLINE`). State derived from `camA.state` and `camB.state`
  the same way the current "STARTPAUSE" label is.

### Team cards (330 px tall, y=70–400; left card x=10–390, right card x=410–790)
- Background: card color from palette (HOME / AWAY).
- 14 px rounded corners (`fillRoundRect`).
- Vertical centerline at x=400 with a thin divider strip
  (10 px wide).
- Inside each card:
  - Team label ("HOME" / "AWAY") centred, size 4, top of card
    (y=90).
  - Score digit centred, size 14, ~y=140 (huge centerpiece).
  - Status row at bottom (y=340): `●` dot in status color (green
    READY, orange CAL, yellow PAUSE, red OFFLINE, grey IDLE) +
    status text size 3 + cam IP size 2 below.

### Footer ribbon (60 px, y=410–470)
- Background: header color from palette.
- Eight buttons in a SINGLE row instead of two:
  - `A −` `A +` at far left (each 60 px wide).
  - `CAL A` (110 px) next.
  - `▶ START` (130 px, with the unicode play-glyph rendered as a
    drawn triangle since the GFX font likely can't print U+25B6).
  - `⟲ RESET` (110 px, glyph rendered as an arc).
  - `CAL B` (110 px).
  - `B −` `B +` at far right (60 px each).
- Total width budget: 60+60+110+130+110+110+60+60 = 700 px + 7
  gaps × ~13 px = 791 px. Tight; tune to 600 px row centred at
  x=400 with 100 px margins on each side.

### Touch hit-test
- Score adjusters become smaller targets (60 px wide × 50 px tall).
  Still well above the GT911's effective resolution.
- CAL buttons stay 110 px wide (current 170) — slightly smaller but
  still comfortable.

---

## 5. Implementation breakdown

All work in `src_hmi/main.cpp`. Estimated ~500 LOC churn.

1. **Add palette constants** at the top of the colors block (~`src_hmi/main.cpp:430`):
   - Define `COL_BG_HEADER`, `COL_CARD_HOME`, `COL_CARD_AWAY`,
     `COL_SCORE_HOME`, `COL_SCORE_AWAY`, `COL_ACCENT`,
     `COL_DESTRUCTIVE`, `COL_LIVE`, `COL_OFFLINE_DOT`,
     `COL_READY_DOT`, `COL_IDLE_DOT`, `COL_CAL_DOT`.
   - Pick from the chosen palette option.
2. **Replace `drawHeader()`** with the new strip layout (ball glyph,
   subtitle, live-state badge).
3. **Replace `drawScoreDigits()`** with `drawTeamCards()` that draws
   both cards in one pass (or two helpers `drawTeamCard(int side)`).
   The score digit is now inside the card.
4. **Replace `drawSideStatus()`** with the in-card status row (dot +
   READY / OFFLINE / CAL / PAUSE / IDLE + cam IP).
5. **Reshape the buttons array** (~`src_hmi/main.cpp:340`):
   - Recalculate `x, y, w, h` for the single-row footer.
   - Add `glyph` field to `Button` struct for the `▶` and `⟲`
     symbols (rendered as primitives, not text).
6. **Reroute `drawButton(idx)`** to handle the new sizes + the
   optional glyph render.
7. **Adjust `dirtyButtons` loop dispatcher**: today it only redraws
   `drawButton(5)` (START) because that's the only label that
   changes. With the new layout, the START button can change to
   `▶ START` / `⏸ PAUSE` / `▶ RESUME`, but `drawButton(5)` still
   covers it. No change here.
8. **Touch hit-test sanity**: `hitTest(x, y)` already iterates the
   `buttons[]` array, so the new positions just work.

---

## 6. Acceptance criteria

| # | Criterion | How to verify |
|---|---|---|
| AC1 | Score digits are visibly larger than today and readable from across the room. | Manual eyeball test |
| AC2 | Team identity is immediately clear (HOME and AWAY visually distinct). | Manual; covered by the card background tint |
| AC3 | Live-state badge in the header shows LIVE / IDLE / PAUSE based on actual cam state. | Touch START, watch badge change |
| AC4 | All 8 buttons fit in a single row of the footer and respond to taps. | Manual + tap each button |
| AC5 | Calibration overlay still works (it's drawn on top of the new placar). | Tap CAL A → overlay appears as before |
| AC6 | All 70 integration tests still pass (REST surface unchanged). | `python3 tests/integration/test_system.py` |
| AC7 | HMI `/status` soak stays ≥ 80/90 (no regression from the pclk fix). | 90 s × 1 Hz triple-board soak |

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

- **Which palette?** Pick A, B, or C (or mix).
- **Match timer in header?** Real timer derived from when START was
  tapped, or just a static placeholder for now? Defer to a small
  follow-up since the cam doesn't expose a match-start timestamp.
- **Team names beyond HOME/AWAY?** Defer — would need a settings
  screen.
- **Light icons (`⚽ ●`) — cheap primitives or skip entirely?**
  Default in this plan: render as primitives. Less effort to skip.

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
