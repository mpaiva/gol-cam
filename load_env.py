import os

Import("env")

# Load .env file and pass WIFI_SSID / WIFI_PASSWORD as build defines
env_file = os.path.join(env["PROJECT_DIR"], ".env")
if os.path.exists(env_file):
    with open(env_file) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, val = line.split("=", 1)
            key, val = key.strip(), val.strip()
            allowed = {"WIFI_SSID", "WIFI_PASSWORD", "WIFI_STATIC_IP", "WIFI_GATEWAY", "WIFI_SUBNET",
                       "BOARD_ROLE", "PEER_IP", "CAMERA_IP",
                       "SCOREBOARD_IP", "SCOREBOARD_SIDE",
                       "SCOREBOARD_STATIC_IP", "SCOREBOARD_GATEWAY", "SCOREBOARD_SUBNET"}
            # Multi-AP slots: WIFI_SSID_1..4 + WIFI_PASSWORD_1..4. The
            # firmware uses WiFiMulti to scan + pick the strongest known
            # SSID at boot, falling back across slots. See CLAUDE.md.
            for i in range(1, 5):
                allowed.add(f"WIFI_SSID_{i}")
                allowed.add(f"WIFI_PASSWORD_{i}")
            if key in allowed:
                env.Append(CPPDEFINES=[(key, env.StringifyMacro(val))])
