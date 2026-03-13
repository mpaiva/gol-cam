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
