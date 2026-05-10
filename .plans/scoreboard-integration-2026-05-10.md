# Scoreboard Integration Plan — 2026-05-10

User asks: import the `placar-eletronico-wifi` (electronic scoreboard) repo
into this project, connect both gol-cams to the scoreboard via Wi-Fi, and
have each detected goal automatically push a "GOL" signal to the scoreboard
to advance the displayed score. Iterate until working.

## Inventory

### Gol-cam (this repo)
- 2 boards (DFR1154 / ESP32-S3) connected to USB at `/dev/cu.usbmodem1101`
  (MAC `86:64`, IP `192.168.1.107`) and `/dev/cu.usbmodem101`
  (MAC `87:ac`, IP `192.168.1.106`).
- Each detects a yellow dadinho dropping into the goal and increments its
  own goal counter.

### Scoreboard (the new repo)
- 1 board: ESP32 DevKit on `/dev/cu.usbserial-0001`.
- 4× MAX7219 8×8 LED matrices (2 per side, A/B, 0–99).
- 4 push buttons (UP/RESET per side).
- **Currently runs in Wi-Fi AP mode**: SSID `PLACAR_WIFI`, IP `192.168.4.1`.
- HTTP endpoints today: `/`, `/a+`, `/b+`, `/az`, `/bz`, `/reset`.
- Sketch is a single Arduino .ino file, MD_MAX72xx + WiFi + WebServer.

## Architecture decision

The scoreboard's standalone-AP design conflicts with the gol-cams already
being on the home network (`RI69`). Three options:

| Option | Pros | Cons |
|---|---|---|
| A. Scoreboard joins home Wi-Fi (STA) | one network, phone keeps internet, gol-cams reach scoreboard normally | loses standalone fallback |
| B. Gol-cams join PLACAR_WIFI | no scoreboard change | dashboards on AP-DHCP IPs, phone loses internet |
| C. Scoreboard runs AP+STA | best of both | rarely-tested mode, can be flaky |

**Going with A.** Scoreboard becomes a normal Wi-Fi client on the home
network. Drops the standalone feature but cleanest for everything else.
We can add AP fallback later if needed.

## What we'll build

### Project layout

```
gol-cam/
├── src/                    ← gol-cam firmware (existing)
├── src_scoreboard/         ← NEW: scoreboard firmware
│   └── scoreboard.cpp
├── platformio.ini          ← add [env:scoreboard]
└── .env                    ← add SCOREBOARD_IP
```

Two PlatformIO environments share the same .env / monitor settings:
- `[env:dfr1154]` → builds `src/` for the gol-cam
- `[env:scoreboard]` → builds `src_scoreboard/` for the scoreboard ESP32

### Scoreboard firmware changes

1. **WiFi STA mode** — same SSID/password from `.env` as the gol-cams.
2. **Optional static IP** so the gol-cams know where to send goals
   without mDNS (`SCOREBOARD_STATIC_IP` in .env, or DHCP and we add the
   IP manually).
3. **New endpoints:**
   - `GET /goal?side=a|b` → equivalent of `/a+` / `/b+` (the existing
     `/a+` etc. continue to work for the manual web UI).
   - `GET /status` → JSON `{"a":N,"b":N,"role":"scoreboard"}` for the
     gol-cam dashboards.
4. **CORS header** on JSON endpoints so dashboards can fetch cross-origin.
5. **Boot heartbeat** to Serial so we can confirm the IP after flashing.

### Gol-cam firmware changes

1. Read `SCOREBOARD_IP` from `.env` via `load_env.py`.
2. New helper `pushGoalToScoreboard(side)` — fires an `HTTPClient` GET to
   `http://<SCOREBOARD_IP>/goal?side=<a|b>` from a short-lived FreeRTOS
   task so the goal-detection loop doesn't block on the network.
3. **Side mapping** (which counter to increment):
   - Match mode (`BOARD_ROLE=home`): goal detected here → opponent (away)
     scores → side **B** of the scoreboard.
   - Match mode (`BOARD_ROLE=away`): goal detected here → home scores →
     side **A** of the scoreboard.
   - Single-camera training: default to side **A**.
4. Add `SCOREBOARD_GOAL_SIDE` override in `.env` for special setups.
5. Existing whistle + visual flash continue to fire as today.

### Dashboard changes

1. Match dashboard polls `http://<SCOREBOARD_IP>/status` (best-effort)
   and shows a badge in the header: ✅ Scoreboard online / ⚠ offline.
2. Add a "Reset Scoreboard" button next to the Reset Match button.
3. Optional: show the scoreboard's current A×B numbers as a verification
   that they match the dashboard's tally.

## Acceptance criteria

1. Scoreboard boots, connects to home Wi-Fi, prints its IP on Serial.
2. Web UI at `http://<SCOREBOARD_IP>/` works as before (manual buttons).
3. `curl http://<SCOREBOARD_IP>/goal?side=a` increments the A display.
4. With both gol-cams running and the dadinho dropped into the home goal,
   side B of the scoreboard increments within 1 s, the whistle plays,
   and the gol-cam dashboard registers the goal.
5. Same for the away-goal camera, side A increments.
6. The scoreboard's physical UP/RESET buttons still work.
7. Reset on the dashboard clears both gol-cam counters AND the
   scoreboard.

## Execution order

1. Add `[env:scoreboard]` to platformio.ini, copy/port the .ino into
   `src_scoreboard/scoreboard.cpp`. Verify it still builds standalone.
2. Convert AP → STA, wire .env, add `/goal` + `/status` endpoints.
3. Flash scoreboard, capture IP via Serial, write it to `.env`.
4. Add `pushGoalToScoreboard` in gol-cam main.cpp; call it from the
   goal-detection block alongside the existing whistle.
5. Flash both gol-cams.
6. Smoke test:
   - `curl /goal?side=a` directly → display advances.
   - Trigger a fake goal by waving the dadinho across the calibrated ROI.
   - Verify scoreboard advances and stays in sync with the gol-cam's
     internal goal count.
7. Update dashboards (status badge + reset button).
8. Commit each phase. Ask user to validate end-to-end before closing.
