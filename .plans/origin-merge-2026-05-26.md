---
date: 2026-05-26
prompt: "please check this repo agains origin and evaluate what needs to be updated."
status: complete — merge commit 1aa4619
---

## Outcome

Merge committed as `1aa4619`. Both `pio run -e dfr1154` and
`pio run -e placar` succeed. Branch is 14 commits ahead of origin/master
(13 local + merge). Not yet pushed — awaiting user.

Notes from execution:
- Git auto-merged most of `src/match_dashboard.h` JS additions cleanly
  outside the conflict regions (boards.placar, pollPlacar, connect()
  changes); only the conflict regions needed manual resolution.
- Remote's added `goalPushTask` + `initScoreboardPush` survived
  auto-merge in `src/main.cpp` outside the conflict region and caused
  a "redefinition of pushGoalToScoreboard" build error; deleted the
  remote block (lines 302–351) since local has its own debugged push.
- `src/main_placar.cpp` + `src/placar_*.cpp` + `include/placar_*.h`
  + `include/pins_placar.h` were kept in the tree as documentation
  but excluded from both PlatformIO envs via `build_src_filter`.



# Origin Merge Plan — 2026-05-26

Merge `origin/master` (consolidation work, May 24) into `master` (hardware-debugged scoreboard work, May 9–10).

## Strategy

- Keep **local code** in all 5 conflicting files (it has hardware-debugged fixes).
- Re-graft **small remote additions** (Placar status field, `side` in `/status`, env-var union).
- Accept **all non-conflicting remote additions** (README, `hardware/`, subtree history, `.gitignore`, `CLAUDE.md`, F6/F7 feature requests).
- For the **scoreboard implementation conflict** (local single-file `src_scoreboard/scoreboard.cpp` vs remote modular `src/placar_*.cpp`):
  - **Decision: keep local's `src_scoreboard/scoreboard.cpp`** — it is debugged hardware firmware. Delete remote's untested `src/main_placar.cpp` + `placar_*.cpp` (the consolidation plan acknowledges the modular version is "untested on hardware").
  - Adopt remote's cleaner `[env:placar]` naming so it matches the project plan; point it at `src_scoreboard/`.
  - Keep remote's `include/placar_*.h` since they document the wiring even if we don't compile them.

## Per-file resolution

### `load_env.py`
**Union** both env-var sets:
- Local adds: `SCOREBOARD_SIDE`, `SCOREBOARD_IP`, `SCOREBOARD_STATIC_IP`, `SCOREBOARD_GATEWAY`, `SCOREBOARD_SUBNET`
- Remote adds: `CAMERA_IP`, `SCOREBOARD_IP`
- Final superset: all of the above (CAMERA_IP + SCOREBOARD_* family + existing WIFI/BOARD vars).

### `platformio.ini`
Take remote's structure (header comments + `[env]` shared section + dual-env layout), but:
- Env `placar` points at `src_scoreboard/scoreboard.cpp` (local's working firmware) via `build_src_filter`.
- Keep local's upload-port pin for the scoreboard (`/dev/cu.usbserial-0001`).
- Keep `lib_deps = majicdesigns/MD_MAX72XX@^3.5.0` for the scoreboard env.
- Camera env excludes `src_scoreboard/`.

### `src/main.cpp`
**Keep local entirely.** Drop remote's `goalPushTask` — local has its own better push (uses `SCOREBOARD_SIDE` env override, derives side from `BOARD_ROLE`, GET `/goal?side=` matches the scoreboard.cpp HTTP server).

### `src/web_stream.cpp`
**Keep local** (stack-overflow fix + memory stats). **Re-apply remote's 3 lines**: add `side` field derivation + `"side":"%s"` in the status JSON. Local already includes `BOARD_ROLE`; remote derives side from it.

### `src/match_dashboard.h`
**Keep local UI redesign** (hierarchy, expert toggle, client-side overlays). **Re-graft remote's Placar additions** into the new layout:
- `<input id='ip-placar'>` field in config row
- `<div id='placar-status'>` line under the score
- `boards.placar` object + `pollPlacar()` function
- i18n keys: `match.placar`, `match.placar_ok`, `match.placar_off`, `match.placar_none`
- localStorage save/restore for placar IP
- Add Placar IP to `connect()`

## Steps

1. ✅ Plan written.
2. `git merge origin/master --no-commit --no-ff` — get the 5 conflicts.
3. Resolve each per above.
4. Decide on `src/main_placar.cpp` + `src/placar_*.cpp` — delete (untested, redundant with `src_scoreboard/`).
5. Build test: `pio run -e dfr1154` must succeed.
6. Build test: `pio run -e placar` must succeed.
7. Commit the merge.
8. Update `.about/feature-requests.md` if F6/F7 acceptance criteria changed.

## Build risk

The camera env should build fine (changes are local). The scoreboard env requires `MD_MAX72XX` library and a different toolchain — first build may pull dependencies.
