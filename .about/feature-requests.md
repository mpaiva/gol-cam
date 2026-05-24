# Feature Requests & Requirements

## F1: Ball Detection (Core)
**Status:** Not started
**Description:** Detect when a 4mm or 6mm ball enters the goal area using camera vision.
**Requirements:**
- Camera captures the goal area continuously
- Image processing identifies ball presence in the goal zone
- Works under typical indoor lighting conditions
**Acceptance Criteria:**
- [ ] Ball crossing goal line is detected within 500ms
- [ ] False positive rate < 5%
- [ ] Works with both 4mm and 6mm balls

## F2: Goal Event Trigger
**Status:** Not started
**Description:** When a goal is detected, trigger a celebratory event.
**Requirements:**
- Visual and/or audio feedback on goal detection
**Acceptance Criteria:**
- [ ] Goal event fires within 1 second of ball detection
- [ ] Clear, unmistakable signal (LED, buzzer, or sound)

## F3: Score Tracking
**Status:** Not started
**Description:** Keep track of the score for both teams.
**Requirements:**
- Increment score on goal detection
- Display current score
- Allow score reset
**Acceptance Criteria:**
- [ ] Score increments correctly on goal
- [ ] Score is visible to players
- [ ] Reset mechanism available

## F4: Two-Camera Match Mode
**Status:** Implemented (untested on hardware)
**Description:** Two boards (one per goal) with a unified match dashboard that aggregates both feeds into a single scoreboard.
**Requirements:**
- Mode selection page at `/` (Treino vs Jogo)
- Match dashboard at `/match` polls both boards' `/status` endpoints
- Browser acts as aggregator (no inter-board communication needed)
- CORS headers already present on all endpoints
- Configurable board IPs with localStorage persistence
- Auto-detection via `/status` role/peer fields
**Acceptance Criteria:**
- [ ] Mode selector page shows two options at `/`
- [ ] Training mode at `/training` works identically to previous `/`
- [ ] Match dashboard shows two camera feeds side by side
- [ ] Scoreboard updates correctly when either board detects a goal
- [ ] Unified controls (start/pause/resume/reset/stop) reach both boards
- [ ] VAR review fetches snapshot from correct board and deducts from correct side
- [ ] Dashboard remains functional when one board goes offline

## F5: Multi-Language Support (i18n)
**Status:** Implemented (untested on hardware)
**Description:** Client-side translation system with language picker, browser auto-detection, and localStorage persistence. English and Portuguese supported; extensible to more languages.
**Requirements:**
- Language picker (EN/PT buttons) fixed top-right on all pages
- Auto-detect browser language via `navigator.language`, fallback to `en`
- Persist language choice in `localStorage('gol-lang')` across pages
- Static HTML elements use `data-i18n` attributes, dynamic strings use `t()` function
- `t(key, ...args)` supports `%d`/`%s` substitution for dynamic values
**Acceptance Criteria:**
- [x] All 3 pages (mode select, training, match) have language picker
- [x] Clicking EN/PT switches all visible text immediately
- [x] Language persists across page navigations and refreshes
- [x] Browser language auto-detection works (Portuguese browsers get PT)
- [x] Dynamic content (goal log entries, state badges, countdown) uses translated strings
- [x] Adding a new language requires only adding translations to each `I18N` object + a button
- [x] Build succeeds with all i18n changes

## F6: Physical LED Scoreboard (Placar)
**Status:** Implemented (untested on hardware)
**Description:** Standalone ESP32 board drives two pairs of MAX7219 8x8 LED matrices showing match score (0–99 per side). Synchronises with one or two camera boards over WiFi: camera pushes goals on detection, placar polls as fallback. Four physical buttons remain available for manual override.
**Requirements:**
- ESP32 DevKit V1 + 4× MAX7219 modules + 4 push buttons
- Lives in the same PlatformIO project as gol-cam (env: `placar`)
- Camera pushes `POST /goal {"side":"A|B"}` to the placar on goal detection
- Placar polls `http://${CAMERA_IP}/status` every 500 ms as fallback
- Physical buttons (UP/DW per side) act as manual override
- Web UI on `:80` mirrors the buttons + shows connection status
**Acceptance Criteria:**
- [x] `pio run -e placar` builds successfully
- [ ] Boots, connects to WiFi STA, shows 0×0 on the LEDs
- [ ] Physical UP_A increments side A and reflects on display + web UI
- [ ] Physical DW_A resets side A
- [ ] Camera goal triggers placar increment within 1 s (push path)
- [ ] Placar still increments correctly even if push fails (polling fallback)
- [ ] Match dashboard shows "Placar: online (A x B)" when configured

## F7: Placar Standalone Operation
**Status:** Implemented (untested on hardware)
**Description:** When the camera is unreachable (WiFi STA fails or no camera flashed), the placar must remain useful as a manual scoreboard with physical buttons.
**Requirements:**
- WiFi STA attempt times out after ~15 s
- On STA failure, switch to AP mode `PLACAR_WIFI` / `12345678` at 192.168.4.1
- All four buttons keep working in AP mode
- Web UI accessible from any device joining the AP
- Connection state visible in the UI ("STA 192.168.x.x" vs "AP PLACAR_WIFI")
**Acceptance Criteria:**
- [x] Implementation complete (`startWiFi()` in `main_placar.cpp`)
- [ ] STA timeout falls back to AP without rebooting
- [ ] Buttons increment/reset in AP mode
- [ ] Web UI loads from `http://192.168.4.1` when in AP mode
- [ ] Returning STA on next reboot recovers normal mode
