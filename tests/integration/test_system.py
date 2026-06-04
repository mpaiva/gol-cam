#!/usr/bin/env python3
"""
gol-cam integration tests — exercises the live REST endpoints exposed
by the placar (MAX7219 board) and both cameras over the project's
shared WiFi network. No dependencies beyond Python stdlib.

Usage:
    python3 tests/integration/test_system.py
    python3 tests/integration/test_system.py --quick      # skip slow tests
    python3 tests/integration/test_system.py --only placar
    python3 tests/integration/test_system.py --placar 192.168.1.50 --cam-a 192.168.1.51 ...

Exits 0 if all expected tests pass, non-zero on any failure.

The suite is **state-aware**: each test resets the relevant counters
before asserting, so the order tests run in doesn't matter and you can
re-run individual groups without polluting the next.
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request


# ---------------------------------------------------------------------------
# Default board addresses (the .env-documented IP scheme)
# ---------------------------------------------------------------------------
DEFAULTS = {
    "placar": "192.168.40.89",
    "cam_a":  "192.168.40.90",
    "cam_b":  "192.168.40.91",
}


# ---------------------------------------------------------------------------
# Test runner — pure-functional, accumulates results into a Results object
# rather than relying on a framework. Keeps the script single-file and
# stdlib-only.
# ---------------------------------------------------------------------------
class Results:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.skipped = 0
        self.current_test = None

    def begin(self, name):
        self.current_test = name
        print(f"\n--- {name} ---")

    def ok(self, label):
        self.passed += 1
        print(f"  ✓ {label}")

    def fail(self, label, detail=""):
        self.failed += 1
        msg = f"  ✗ {label}"
        if detail:
            msg += f"  ({detail})"
        print(msg)

    def skip(self, label, reason):
        self.skipped += 1
        print(f"  - skip {label}  ({reason})")

    def assert_eq(self, actual, expected, label):
        if actual == expected:
            self.ok(label)
        else:
            self.fail(label, f"got {actual!r}, expected {expected!r}")

    def assert_true(self, cond, label, detail=""):
        if cond:
            self.ok(label)
        else:
            self.fail(label, detail)

    def assert_eventually(self, fetch, expected, label, timeout=5.0, interval=0.25):
        """Poll `fetch()` until it returns `expected`, up to `timeout`.
        Tolerates async pushes (cam → placar etc.) that don't land in
        one fixed sleep — important on a busy 2.4 GHz network."""
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            try: last = fetch()
            except Exception: last = None
            if last == expected:
                self.ok(label)
                return
            time.sleep(interval)
        self.fail(label, f"got {last!r}, expected {expected!r} (within {timeout}s)")

    def summary(self):
        total = self.passed + self.failed
        print(f"\n{'=' * 60}")
        print(f"  {self.passed}/{total} passed",
              f"({self.failed} failed, {self.skipped} skipped)")
        return self.failed == 0


# ---------------------------------------------------------------------------
# HTTP helpers — short timeouts so a hung board doesn't block the whole run.
#
# Per-host persistent HTTP/1.1 connections (keep-alive) instead of one fresh
# TCP socket per call. The ESP32's LWIP has only ~10 sockets total and each
# closed socket holds TIME_WAIT for 60 s, so the previous fresh-socket-per-
# request pattern exhausted the pool during cross-board burst polling
# (~130 sockets in 30 s). One persistent connection per host means a single
# socket carries the whole test run. Matches what a real browser dashboard
# does (keep-alive polling).
# ---------------------------------------------------------------------------
import http.client
from urllib.parse import urlsplit

_conn_cache = {}  # host -> http.client.HTTPConnection

def _get_conn(host_port):
    c = _conn_cache.get(host_port)
    if c is None:
        host, _, port = host_port.partition(":")
        c = http.client.HTTPConnection(host, int(port) if port else 80, timeout=4)
        _conn_cache[host_port] = c
    return c

def http_get(url, timeout=4):
    parts = urlsplit(url)
    host_port = parts.netloc
    path = parts.path + (f"?{parts.query}" if parts.query else "")
    # Two attempts so a stale keep-alive socket can be retried fresh.
    for attempt in range(2):
        try:
            conn = _get_conn(host_port)
            conn.timeout = timeout
            conn.request("GET", path)
            r = conn.getresponse()
            body = r.read().decode("utf-8", errors="replace")
            return r.status, body
        except (OSError, http.client.HTTPException, TimeoutError) as e:
            # Drop the dead connection and let the next iteration rebuild it.
            try: conn.close()
            except Exception: pass
            _conn_cache.pop(host_port, None)
            if attempt == 1:
                return 0, str(e)


def http_get_json(url, timeout=4):
    code, body = http_get(url, timeout)
    if code != 200:
        return code, None
    try:
        return code, json.loads(body)
    except json.JSONDecodeError:
        return code, None


def reachable(host, path="/status"):
    code, _ = http_get(f"http://{host}{path}", timeout=2)
    return code == 200


# ---------------------------------------------------------------------------
# Placar tests
# ---------------------------------------------------------------------------
def test_placar(r: Results, placar: str):
    r.begin("placar /status")
    code, d = http_get_json(f"http://{placar}/status")
    if d is None:
        r.fail("/status returns 200 JSON", f"code={code}")
        return
    r.ok("/status returns 200 JSON")
    r.assert_eq(d.get("role"), "scoreboard", "role == scoreboard")
    r.assert_true("a" in d and "b" in d, "has a and b counters")
    r.assert_true(isinstance(d.get("a"), int) and isinstance(d.get("b"), int),
                  "a and b are ints")
    r.assert_eq(d.get("ip"), placar, "ip field matches target")

    r.begin("placar /api/reset clears both counters")
    http_get(f"http://{placar}/goal?side=a")
    http_get(f"http://{placar}/goal?side=b")
    _, d = http_get_json(f"http://{placar}/api/reset")
    if d is None:
        r.fail("/api/reset returns JSON")
    else:
        r.assert_true(d.get("ok") is True, "ok=true")
        r.assert_eq(d.get("a"), 0, "a reset to 0")
        r.assert_eq(d.get("b"), 0, "b reset to 0")

    r.begin("placar /goal?side=a increments a only")
    http_get(f"http://{placar}/api/reset")
    _, d = http_get_json(f"http://{placar}/goal?side=a")
    r.assert_eq(d.get("a"), 1, "a goes 0 → 1")
    r.assert_eq(d.get("b"), 0, "b stays 0")
    _, d = http_get_json(f"http://{placar}/goal?side=a")
    r.assert_eq(d.get("a"), 2, "a goes 1 → 2 on second hit")

    r.begin("placar /goal?side=b increments b only")
    http_get(f"http://{placar}/api/reset")
    _, d = http_get_json(f"http://{placar}/goal?side=b")
    r.assert_eq(d.get("b"), 1, "b goes 0 → 1")
    r.assert_eq(d.get("a"), 0, "a stays 0")

    r.begin("placar /goal rejects invalid side")
    code, d = http_get_json(f"http://{placar}/goal?side=x")
    r.assert_eq(code, 400, "HTTP 400 for invalid side")
    r.assert_true(d is None or d.get("ok") is False, "ok=false on error")

    r.begin("placar /goal-undo?side=a decrements a")
    http_get(f"http://{placar}/api/reset")
    http_get(f"http://{placar}/goal?side=a")
    http_get(f"http://{placar}/goal?side=a")
    _, d = http_get_json(f"http://{placar}/goal-undo?side=a")
    r.assert_eq(d.get("a"), 1, "a goes 2 → 1")
    _, d = http_get_json(f"http://{placar}/goal-undo?side=a")
    r.assert_eq(d.get("a"), 0, "a goes 1 → 0")

    r.begin("placar /goal-undo clamps at zero")
    http_get(f"http://{placar}/api/reset")
    _, d = http_get_json(f"http://{placar}/goal-undo?side=a")
    r.assert_eq(d.get("a"), 0, "undo at 0 stays 0")
    _, d = http_get_json(f"http://{placar}/goal-undo?side=b")
    r.assert_eq(d.get("b"), 0, "undo at 0 stays 0 for b too")


# ---------------------------------------------------------------------------
# Camera tests
# ---------------------------------------------------------------------------
def test_camera(r: Results, label: str, ip: str, expected_side: str):
    r.begin(f"{label} /status shape")
    code, d = http_get_json(f"http://{ip}/status")
    if d is None:
        r.fail("/status 200 JSON", f"code={code}")
        return
    r.ok("/status returns 200 JSON")
    r.assert_eq(d.get("side"), expected_side, f"side == {expected_side}")
    for key in ("goals", "fps", "state", "calibrated",
                "motionTh", "motion", "colorTh",
                "scoreboardIp", "hasGoalSnap", "hasSnapPrev", "hasCalSnap"):
        r.assert_true(key in d, f"/status has '{key}'")
    r.assert_true(isinstance(d.get("goals"), int), "goals is int")
    r.assert_true(d.get("fps", 0) > 0, "fps > 0 (detector running)")

    r.begin(f"{label} /reset zeros goalCount")
    # Force a goal first so we know reset actually does work.
    http_get(f"http://{ip}/test-fire")
    time.sleep(1)
    http_get(f"http://{ip}/reset")
    time.sleep(0.5)
    _, d = http_get_json(f"http://{ip}/status")
    r.assert_eq(d.get("goals"), 0, "goals == 0 after /reset")

    r.begin(f"{label} /deduct on goal=0 stays at 0")
    http_get(f"http://{ip}/deduct")
    _, d = http_get_json(f"http://{ip}/status")
    r.assert_eq(d.get("goals"), 0, "goals stays at 0 (clamp)")


def test_camera_calibrate(r: Results, label: str, ip: str):
    """Calibration doesn't depend on a real ball — we only assert that
    the request is accepted and the state transitions cycle through
    STATE_CALIBRATING and back to STATE_IDLE. The actual cal pass/fail
    depends on what's in front of the camera."""
    r.begin(f"{label} /calibrate triggers CALIBRATING then returns to IDLE")
    _, d = http_get_json(f"http://{ip}/calibrate")
    r.assert_true(d is None or d.get("ok") is True, "/calibrate returns ok")
    # Poll for state change (STATE_CALIBRATING == 1). The whole flow
    # finishes in well under 3 s including the motion-noise sampling.
    saw_cal = False
    final_state = None
    for _ in range(80):           # 80 * 50 ms = 4 s max
        time.sleep(0.05)
        _, d = http_get_json(f"http://{ip}/status")
        if not d: continue
        if d.get("state") == 1: saw_cal = True
        final_state = d.get("state")
        if saw_cal and final_state == 0: break
    # Calibration on the ESP32-S3 with a simple scene can finish faster
    # than 50 ms — accept either "saw the transition" OR "calMsg was
    # updated this poll cycle" as evidence the request was honoured.
    if not saw_cal:
        _, d = http_get_json(f"http://{ip}/status")
        msg = (d or {}).get("calMsg") or ""
        if any(tag in msg for tag in ("OK!", "FAILED", "Analyzing")):
            r.ok("state transitioned through CALIBRATING (inferred from calMsg)")
        else:
            r.fail("state transitions to CALIBRATING (1)",
                   f"never saw state=1; final calMsg={msg!r}")
    else:
        r.ok("state transitions to CALIBRATING (1)")
    _, d = http_get_json(f"http://{ip}/status")
    r.assert_eq(d.get("state"), 0, "state returns to IDLE (0)")


# ---------------------------------------------------------------------------
# Cross-board tests — these are the most valuable because they catch
# protocol-level drift between camera + placar.
# ---------------------------------------------------------------------------
def test_cross_board(r: Results, placar: str, cam_a: str, cam_b: str):
    def placar_a():
        _, d = http_get_json(f"http://{placar}/status")
        return d.get("a") if d else None
    def placar_b():
        _, d = http_get_json(f"http://{placar}/status")
        return d.get("b") if d else None
    def cam_goals(ip):
        def f():
            _, d = http_get_json(f"http://{ip}/status")
            return d.get("goals") if d else None
        return f

    r.begin("cross: cam A /test-goal increments placar a")
    http_get(f"http://{placar}/api/reset")
    http_get(f"http://{cam_a}/test-goal")
    r.assert_eventually(placar_a, 1, "placar a → 1 within 3s", timeout=3.0)
    r.assert_eq(placar_b(), 0, "placar b unchanged")

    r.begin("cross: cam B /test-goal increments placar b")
    http_get(f"http://{placar}/api/reset")
    http_get(f"http://{cam_b}/test-goal")
    r.assert_eventually(placar_b, 1, "placar b → 1 within 3s", timeout=3.0)
    r.assert_eq(placar_a(), 0, "placar a unchanged")

    r.begin("cross: cam A /test-fire bumps placar a + cam A goalCount")
    http_get(f"http://{placar}/api/reset")
    http_get(f"http://{cam_a}/reset")
    http_get(f"http://{cam_a}/test-fire")
    r.assert_eventually(cam_goals(cam_a), 1, "cam A goals → 1", timeout=3.0)
    r.assert_eventually(placar_a, 1, "placar a → 1", timeout=3.0)
    _, d = http_get_json(f"http://{cam_a}/status")
    r.assert_true(d.get("hasGoalSnap") is True, "hasGoalSnap == true")

    r.begin("cross: cam A /deduct decrements placar a (VAR annul sync)")
    http_get(f"http://{cam_a}/deduct")
    r.assert_eventually(cam_goals(cam_a), 0, "cam A goals → 0", timeout=3.0)
    r.assert_eventually(placar_a, 0, "placar a → 0 (goal-undo landed)", timeout=3.0)

    r.begin("cross: cam B same VAR-annul sync")
    http_get(f"http://{placar}/api/reset")
    http_get(f"http://{cam_b}/reset")
    http_get(f"http://{cam_b}/test-fire")
    r.assert_eventually(placar_b, 1, "placar b → 1 after cam B fire", timeout=3.0)
    http_get(f"http://{cam_b}/deduct")
    r.assert_eventually(placar_b, 0, "placar b → 0 after cam B /deduct", timeout=3.0)


# ---------------------------------------------------------------------------
# Main entry
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--placar", default=DEFAULTS["placar"])
    ap.add_argument("--cam-a",  default=DEFAULTS["cam_a"])
    ap.add_argument("--cam-b",  default=DEFAULTS["cam_b"])
    ap.add_argument("--quick", action="store_true",
                    help="Skip slow tests (calibration, full cross-board)")
    ap.add_argument("--only", choices=("placar", "cameras", "cross"),
                    help="Run only one group")
    args = ap.parse_args()

    print(f"Targeting placar={args.placar}  camA={args.cam_a}  camB={args.cam_b}")
    print(f"Quick mode: {args.quick}   Only: {args.only or 'all'}\n")

    # Reachability gate — bail with a clear message if anything is offline
    # so we don't drown the user in cascading failures.
    print("--- reachability ---")
    placar_up = reachable(args.placar)
    cam_a_up  = reachable(args.cam_a)
    cam_b_up  = reachable(args.cam_b)
    print(f"  placar @ {args.placar}  {'UP' if placar_up else 'DOWN'}")
    print(f"  cam A  @ {args.cam_a}  {'UP' if cam_a_up else 'DOWN'}")
    print(f"  cam B  @ {args.cam_b}  {'UP' if cam_b_up else 'DOWN'}")

    r = Results()

    only = args.only

    if only in (None, "placar"):
        if placar_up:
            test_placar(r, args.placar)
        else:
            r.skip("placar suite", "placar offline")

    if only in (None, "cameras"):
        if cam_a_up:
            test_camera(r, "cam A", args.cam_a, "A")
            if not args.quick:
                test_camera_calibrate(r, "cam A", args.cam_a)
        else:
            r.skip("cam A suite", "cam A offline")
        if cam_b_up:
            test_camera(r, "cam B", args.cam_b, "B")
            if not args.quick:
                test_camera_calibrate(r, "cam B", args.cam_b)
        else:
            r.skip("cam B suite", "cam B offline")

    if only in (None, "cross"):
        if placar_up and cam_a_up and cam_b_up:
            test_cross_board(r, args.placar, args.cam_a, args.cam_b)
        else:
            r.skip("cross-board suite", "one or more targets offline")

    ok = r.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
