# AGENTS.md

This is the main AI guidance document for this repository. It applies to all AI coding agents (Claude Code, Copilot, Cursor, etc.).

## Project Overview

A GIF player application for the **Seeed Studio XIAO ESP32C3/S3** paired with the **1.28" Round Display** (240×240 px, GC9A01 controller, CHSC6X touch). GIF files are read from an SD card and displayed with touch-based navigation controls.

## Build System

This project uses **PlatformIO**. The `platformio.ini` lives in the `feat/add_build_action` branch — if it's missing from the working tree, check out or cherry-pick from that branch.

### Setup

```bash
# Install Python dependencies (includes PlatformIO)
pip install -r requirements.txt

# Or activate the existing venv
source .venv/bin/activate   # Linux/macOS
.venv\Scripts\activate      # Windows
```

### Build Commands

```bash
# Build for ESP32C3 with Seeed touch display
platformio run -e app_seeed_xiao_esp32c3-seeed_touch_104030087

# Build for ESP32S3 variant
platformio run -e app_seeed_xiao_esp32s3-seeed_touch_104030087

# Build + upload + monitor
platformio run -e app_seeed_xiao_esp32c3-seeed_touch_104030087 --target upload
platformio device monitor --baud 115200
```

## Architecture

### Source Files

| File | Purpose |
|------|---------|
| `TFT_eSPI_GifPlayer.ino` | Main Arduino sketch — GIF playback loop, touch UI, SD scanning |
| `lv_xiao_round_screen.h` | Display + touch driver header (TFT_eSPI or Arduino_GFX backend, CHSC6X I²C touch) |

The `.ino` file is the entire application. There is no `src/` directory in the main/feat branches — the sketch lives at the project root.

### Display / Touch Abstraction (`lv_xiao_round_screen.h`)

- Conditionally compiles with `USE_TFT_ESPI_LIBRARY` or `USE_ARDUINO_GFX_LIBRARY` (the `.ino` defines `USE_TFT_ESPI_LIBRARY`)
- Exposes `tft` (TFT_eSPI object), `xiao_disp_init()`, `chsc6x_is_pressed()`, `chsc6x_get_xy()`
- LVGL integration functions: `lv_xiao_disp_init()`, `lv_xiao_touch_init()`

### Application Flow

1. `setup()`: reads NVS preferences (mode, last file index), inits display/touch/SD, scans `/data/` folder for GIFs
2. `loop()`: optional UI demo on first run, then cycles through GIF files using `gifPlay()`
3. `gifPlay()`: plays a single GIF file, polls `loopUI()` each frame to handle touch input
4. `loopUI()`: detects touch via CHSC6X, hit-tests three button regions, returns `DO_NEXT`, `DO_PREVIOUS`, or `DO_CHANGE_MODE`

### Operating Modes (persisted in NVS namespace `"gif-player"`)

- `PREF_MODE_STANDBY` (0): manual navigation only
- `PREF_MODE_PLAYER` (1): auto-advance after `maxGifDurationMs` (60 s)

### Touch Button Layout (240×240 screen)

- **Previous**: centered at (50, 120) — left triangle
- **Next**: centered at (190, 120) — right triangle
- **Mode switch**: centered at (120, 200) — pause/play icon

### Libraries (git submodules in `libraries/`)

- `Seeed_Arduino_RoundDisplay` — Seeed's round display HAL
- `SeeedStudio_lvgl` — LVGL v8.3.x port
- `SeeedStudio_TFT_eSPI` — TFT_eSPI tuned for XIAO round display

After cloning, initialise submodules:
```bash
git submodule update --init --recursive
```

### Custom Board & Variants

- `boards/esp32c3.json` — custom board definition (160 MHz, DIO flash, Tasmota partition table)
- `variants/xiao_esp32c3/` and `variants/xiao_esp32s3/` — pin mapping overrides
- The `platformio_env32.ini` (in `feat/add_build_action`) sets `board_build.variants_dir = variants`

### Post-Build Scripts (`pio-tools/`, present in `feat/add_build_action`)

Python scripts run by PlatformIO extra_scripts:
- `name-firmware.py` / `gzip-firmware.py` — rename and compress firmware output
- `post_build.py` — copies build artifacts to `build_output/firmware/` and `build_output/map/`
- `post_esp32.py` — ESP32-specific post-processing

## Commit Messages

This project uses [Conventional Commits](https://www.conventionalcommits.org/).

```text
<type>(<scope>): <short summary>
```

Common types used in this repo:

| Type       | When to use                                      |
|------------|--------------------------------------------------|
| `feat`     | New feature or behaviour                         |
| `fix`      | Bug fix                                          |
| `chore`    | Build, deps, tooling — no production code change |
| `docs`     | Documentation only                               |
| `refactor` | Code restructure without behaviour change        |

Scopes reflect the area changed, e.g. `build`, `play-mode`, `touch`, `ui`, `libraries`, `post-esp32`.

### Commit rules

- **Atomic commits**: each commit must contain exactly one logical change. Do not bundle unrelated fixes, formatting, and features into a single commit.
- **Meaningful messages**: the summary line must describe *what* changed and *why*, not just *what files* changed.
- **No `Co-Authored-By` trailers**: do not append `Co-Authored-By` or any AI-attribution lines to commit messages.

Examples from this repo's history:

```text
feat(play-mode): add play mode
fix(build): comment out variants directory line in platformio_env32.ini
chore(libraries): add libraries
docs: update README.md
```

## SD Card Setup

1. Format SD card as FAT32
2. Create a `/data/` folder
3. Place 240×240 pixel GIF files in `/data/` (use [ezgif.com](https://ezgif.com) to resize)
4. SD card CS pin is `D2`

## Key Constants

```cpp
// GIF timing limits (in TFT_eSPI_GifPlayer.ino)
maxLoopIterations = 1
maxLoopsDuration  = 3000   // ms minimum display time
maxGifDurationMs  = 60000  // ms in player mode before auto-advance

// Screen
SCREEN_WIDTH  = 240
SCREEN_HEIGHT = 240

// Touch controller
CHSC6X_I2C_ID = 0x2e
TOUCH_INT      = D7
```
