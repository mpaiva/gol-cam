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
            if key in ("WIFI_SSID", "WIFI_PASSWORD", "WIFI_STATIC_IP", "WIFI_GATEWAY", "WIFI_SUBNET"):
                env.Append(CPPDEFINES=[(key, env.StringifyMacro(val))])
