# gol-cam

An ESP32-S3 camera system that detects goals in **futebol de botao** (button soccer) and triggers celebrations. Built for the **Dadinho** variant, it uses computer vision to spot the small yellow cube crossing the goal line.

## How It Works

A DFRobot DFR1154 (ESP32-S3 + OV3660 camera) watches the goal area. You calibrate by placing the dadinho in view, and the system learns its color and size. During play, it detects the cube entering the goal, captures a snapshot, and keeps score — all through a web UI served over WiFi.

Features:
- Live MJPEG camera stream
- One-click calibration with visual feedback
- Automatic goal detection with 10-second cooldown
- Goal photo log with timestamps
- VAR (Video Assistant Referee) review system
- Game controls: pause, resume, reset, end game

## Futebol de Botao and the Dadinho in Rio de Janeiro

**Futebol de botao** (button football) is a tabletop sport born in Brazil in the 1930s, where players flick round discs to move a smaller piece — the ball — across a miniature pitch. The game emerged from the creativity of Brazilian kids who fashioned buttons, bottle caps, and coins into teams, playing on dining tables and wooden boards across the country.

### Origins

The sport's roots trace to the 1930s and 1940s, when Geraldo Decourt of Sao Paulo published the first formal rules in 1930. What started as a children's pastime quickly grew into an organized competitive activity. By the mid-20th century, clubs and federations had formed, and regional rule variations flourished.

### Rio de Janeiro and the Regra Carioca

Rio de Janeiro became one of the sport's most important centers, developing its own distinctive style known as the **Regra Carioca** (Carioca Rules) or **3 Toques** (3 Touches). Under these rules, a player gets up to three flicks per turn to advance the ball, creating a faster, more tactical game compared to other regional variants.

The city's competitive scene thrived through decades of neighborhood rivalries, club tournaments, and state championships. Carioca players became known for their technical precision and creative play, mirroring the style of Rio's real football culture.

### The Dadinho

The **Dadinho** (little dice) variant represents a distinct branch of button football. Instead of traditional round discs, players use small dice — typically 6mm yellow cubes — as the playing pieces. The name comes from "dado" (dice) with the diminutive "-inho" suffix.

Dadinho was officially recognized by the Confederacao Brasileira de Futebol de Mesa (CBFM) in 2010, giving it the same institutional standing as the traditional disc-based modalities. The variant has its own dedicated competitive circuit, with the Brazilian Dadinho Championship reaching its 16th edition in 2025.

### Cultural Significance

Futebol de botao occupies a unique place in Brazilian culture. It combines the country's passion for football with the social tradition of tabletop gaming. For generations of Brazilians — especially Cariocas — it was a gateway into the world of sport, played on kitchen tables before school, at club social halls on weekends, and in serious competitive tournaments.

The sport continues to evolve, with active federations, regular tournaments, and a dedicated community that preserves its traditions while welcoming innovations — like this project, which brings computer vision to the goal line.

## Hardware

- **DFRobot DFR1154** — ESP32-S3 with OV3660 camera, 16MB flash, 8MB PSRAM
- Standard micro USB-C for power and programming

## Getting Started

### Prerequisites

- [Python 3](https://www.python.org/downloads/)
- [PlatformIO Core (CLI)](https://platformio.org/install/cli)
- DFRobot DFR1154 board connected via USB-C

### Step 1 — Clone the repo

```bash
git clone https://github.com/mpaiva/gol-cam.git
cd gol-cam
```

### Step 2 — Create a Python virtual environment

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install platformio
```

### Step 3 — Set your WiFi credentials

Create a `.env` file in the project root (this file is git-ignored):

```bash
echo "WIFI_SSID=your-network-name" > .env
echo "WIFI_PASSWORD=your-password" >> .env
```

**Optional — Static IP:** Add these lines to `.env` if you want a fixed address on your network:

```
WIFI_STATIC_IP=192.168.0.100
WIFI_GATEWAY=192.168.0.1
WIFI_SUBNET=255.255.255.0
```

### Step 4 — Build the firmware

```bash
pio run
```

### Step 5 — Upload to the board

Plug in the DFR1154 via USB-C, then:

```bash
pio run -t upload
```

### Step 6 — Find the IP address

Open the serial monitor to see the board's IP:

```bash
pio device monitor
```

Look for the line:

```
Dashboard: http://192.168.x.x
```

Press `Ctrl+C` to exit the monitor.

### Step 7 — Open the dashboard

Open the IP address from the serial monitor in your browser. From there you can:

1. **Calibrate** — Place the dadinho in the goal area and click "Calibrate"
2. **Start Game** — Click "Start Game" to begin detection
3. **Play** — The system detects goals automatically, captures snapshots, and keeps score
4. **VAR Review** — Click "VAR" on any goal to review and confirm or annul it
5. **Game Controls** — Pause, resume, reset, or end the game at any time

## License

MIT
