# LCC Lighting Touchscreen Controller

An ESP32-S3–based LCC/OpenLCB lighting scene controller with a touch LCD user interface for model railroad layout lighting control. Designed specifically to interface with my [LCC Lighting Controller](https://github.com/vsi5004/LCCLightingController)


![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.1.6-blue)
![License](https://img.shields.io/badge/license-BSD--2--Clause-green)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-orange)

## Overview

This device connects to an LCC (Layout Command Control) / OpenLCB CAN bus and sends RGBW lighting commands to follower lighting controller boards. It provides an intuitive touch interface for:

- **Manual Control** — Real-time RGBW + brightness adjustment via sliders
- **Scene Management** — Save, load, edit, reorder, and delete lighting presets
- **Configurable Fades** — Linear transitions up to 5 minutes with visual progress
- **Power Saving** — Automatic backlight timeout with touch-to-wake

The controller does **not** drive LEDs directly; it acts as a command station sending events to downstream lighting nodes.

## Hardware

### Target Platform

**[Waveshare ESP32-S3 Touch LCD 4.3B](https://www.waveshare.com/esp32-s3-touch-lcd-4.3b.htm)**

| Component | Specification |
|-----------|---------------|
| MCU | ESP32-S3 (dual-core, 240 MHz) |
| Display | 4.3" RGB LCD (800×480) |
| Touch | Capacitive (GT911) |
| Storage | SD card slot (SPI via CH422G) |
| CAN | Onboard transceiver (TX: GPIO15, RX: GPIO16) |
| I/O Expander | CH422G (I2C) — controls backlight & SD CS |

### Connections

| Interface | Pins |
|-----------|------|
| CAN TX | GPIO 15 |
| CAN RX | GPIO 16 |
| I2C SDA | GPIO 8 |
| I2C SCL | GPIO 9 |
| SD Card | SPI via CH422G CS |

### 3D Printed Mounts

The `printed_mounts/` directory contains 3D models for mounting the display at an angle
for convenient viewing on a layout fascia or control panel.

![Angled Mount Render](docs/img/angled_mount_render.png)

**Included Files:**
- `Waveshare ESP32-S3-Touch_LCD-4.3B Angled Mount v4.f3d` — Fusion 360 source file
- `Waveshare ESP32-S3-Touch_LCD-4.3B Angled Mount v4.stl` — Print-ready STL

The mount provides an ergonomic viewing angle and can be attached to a fascia or
mounted on a surface near the layout. The mount allows for rear access to the SD Card even after the screen is affixed with double sided tape. Mounting to the layout can be acheived with a variety of commonly available fasteners as long as the head is smaller than 8.2mm in diameter.

## Software Stack

| Component | Version/Source |
|-----------|----------------|
| ESP-IDF | **v5.1.6** (required — see below) |
| LVGL | 8.x (ESP-IDF component) |
| OpenMRN | [Forked submodule](https://github.com/vsi5004/openmrn) |
| Touch Driver | esp_lcd_touch_gt911 |
| Image Decoder | esp_jpeg |

## Project Structure

```
LCCLightingTouchscreen/
├── CMakeLists.txt              # Top-level build file
├── LICENSE                     # BSD 2-Clause license
├── lv_conf.h                   # LVGL configuration (root level)
├── sdkconfig.defaults          # ESP-IDF configuration defaults
├── sdkconfig                   # Full ESP-IDF configuration
├── partitions.csv              # Flash partition table
├── components/
│   ├── OpenMRN/                # LCC/OpenLCB stack (git submodule)
│   └── board_drivers/          # Hardware abstraction layer
│       ├── CMakeLists.txt
│       ├── ch422g.c            # I2C expander driver
│       ├── waveshare_lcd.c     # Display initialization
│       ├── waveshare_touch.c   # Touch input handling
│       ├── waveshare_sd.c      # SD card driver
│       └── include/
│           ├── ch422g.h
│           ├── waveshare_lcd.h
│           ├── waveshare_touch.h
│           └── waveshare_sd.h
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                  # Entry point, task creation
│   ├── lv_conf.h               # LVGL configuration (main level)
│   ├── Kconfig.projbuild       # Project-specific Kconfig options
│   ├── idf_component.yml       # ESP Component Registry dependencies
│   ├── app/                    # Application logic
│   │   ├── app.h               # App-wide includes
│   │   ├── fade_controller.c/.h # Lighting fade state machine
│   │   ├── lcc_node.cpp/.h     # OpenMRN integration
│   │   ├── lcc_config.hxx      # CDI configuration definition
│   │   ├── scene_storage.c/.h  # SD card scene persistence
│   │   └── screen_timeout.c/.h # Backlight power saving with fade
│   └── ui/                     # LVGL screens
│       ├── ui.h                # UI-wide includes
│       ├── ui_common.c/.h      # LVGL init, mutex, color preview
│       ├── ui_main.c           # Main tabview screen
│       ├── ui_manual.c         # Manual RGBW control tab
│       └── ui_scenes.c         # Scene card carousel tab
├── docs/                       # Design documentation
│   ├── ARCHITECTURE.md         # System design, task model
│   ├── INTERFACES.md           # Hardware interfaces, GPIO mapping
│   ├── SPEC.md                 # Feature specification
│   ├── TEST_PLAN.md            # Test cases
│   ├── AGENTS.md               # AI coding guidelines
│   └── img/                    # Documentation images
├── printed_mounts/             # 3D printable enclosure models
│   ├── *.f3d                   # Fusion 360 source
│   └── *.stl                   # Slicer-ready STL
```

## Building

### Prerequisites

1. **ESP-IDF v5.1.6** — [Installation Guide](https://docs.espressif.com/projects/esp-idf/en/v5.1.6/esp32s3/get-started/index.html)
   
   > ⚠️ **Version Constraint**: ESP-IDF 5.3+ uses GCC 13.x/14.x which has a newlib/libstdc++ incompatibility causing OpenMRN compilation failures. You **must** use ESP-IDF 5.1.6 (GCC 12.2.0).

2. **Git** with submodule support

### Clone & Build

```bash
# Clone with submodules
git clone --recursive https://github.com/vsi5004/LCC-Lighting-Touchscreen.git
cd LCC-Lighting-Touchscreen

# Set target
idf.py set-target esp32s3

# Build
idf.py build

# Flash
idf.py -p COMx flash monitor
```

### VS Code with ESP-IDF Extension

This project is configured for the [ESP-IDF VS Code Extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension):

1. Open the project folder in VS Code
2. Use the ESP-IDF commands from the command palette or status bar
3. Build: `ESP-IDF: Build your project`
4. Flash: `ESP-IDF: Flash your project`
5. Monitor: `ESP-IDF: Monitor your device`

## SD Card Setup

The device reads configuration and scenes from the SD card root:

### `nodeid.txt`

Plain text file containing the 6-byte LCC node ID in dotted hex format:

```
05.01.01.01.22.60
```

### `openmrn_config`

Binary file automatically created by OpenMRN. Stores LCC configuration data:

**User Info (ACDI)**
- User Name — Node name shown in LCC tools (e.g., JMRI)
- User Description — Node description

**Startup Behavior**
- Auto-Apply First Scene on Boot — Enable (1) or disable (0)
- Auto-Apply Transition Duration — Fade time in seconds (0-300)

**Lighting Configuration**
- Base Event ID — 8-byte event ID base for lighting commands

All settings are configurable via any LCC configuration tool (JMRI, etc.).

### `scenes.json`

```json
{
  "version": 1,
  "scenes": [
    { "name": "sunrise", "brightness": 180, "r": 255, "g": 120, "b": 40, "w": 0 },
    { "name": "night",   "brightness": 30,  "r": 10,  "g": 10,  "b": 40, "w": 0 }
  ]
}
```

### `splash.jpg`

Custom 800 x 480 px boot splash image (decoded via esp_jpeg). Cannot be saved as "progressive" jpg

## LCC Event Model

Events are transmitted to control downstream RGBW lighting nodes.

### Event ID Format

```
05.01.01.01.22.60.0X.VV
                  │  └── Value (0-255)
                  └───── Parameter
```

| Parameter | X | Description |
|-----------|---|-------------|
| Red | 0 | Red channel intensity |
| Green | 1 | Green channel intensity |
| Blue | 2 | Blue channel intensity |
| White | 3 | White channel intensity |
| Brightness | 4 | Master brightness |

### Transmission Rules

- Minimum interval: **10 ms** between transmission rounds
- Transmission mode: **Burst** (all 5 params sent together)
- Transmission order: Brightness → R → G → B → W
- All 5 parameters sent on Apply

## LCC Configuration (CDI)

The following settings can be configured via any LCC configuration tool (JMRI, etc.):

### User Info (ACDI)
| Setting | Description |
|---------|-------------|
| User Name | Node name displayed in LCC tools |
| User Description | Node description displayed in LCC tools |

### Startup Behavior
| Setting | Default | Range | Description |
|---------|---------|-------|--------------|
| Auto-Apply First Scene on Boot | 1 (enabled) | 0-1 | Apply first scene after startup |
| Auto-Apply Transition Duration | 10 seconds | 0-300s | Fade duration for auto-apply |
| Screen Backlight Timeout | 60 seconds | 0, 10-3600s | Idle timeout before screen off (0=disabled) |

### Lighting Configuration
| Setting | Default | Description |
|---------|---------|-------------|
| Base Event ID | 05.01.01.01.22.60.00.00 | Base for lighting event IDs |

## User Interface

### Scene Selector Tab (Default)

- Horizontal swipeable card carousel
- Color preview circle on each card showing approximate light output
- Center-snapping scroll behavior with blue selection highlight
- Transition duration slider (0–300 seconds)
- Apply Button performs smooth linear fade to selected scene
- Progress bar shows fade completion (auto-hides when done)
- Edit button on each card opens edit modal
- Delete button on each card (with confirmation modal)

### Scene Edit Modal

Tap the edit button (✏️) on any scene card to open the edit modal:

- **Scene name** text input with on-screen keyboard
- **RGBW + Brightness sliders** (0-255 each) with real-time preview
- **Color preview circle** updates as you adjust values
- **Move Left/Right buttons** to reorder scenes in the carousel
- **Preview button** sends current slider values to lighting for live testing
- **Save** applies changes to SD card, **Cancel** discards

### Manual Control Tab

- Five sliders: Brightness, Red, Green, Blue, White
- **Color preview circle** showing approximate light output from current settings
- **Apply** button sends current values immediately to LCC bus
- **Save Scene** opens dialog to save current settings

### Color Preview Algorithm

The color preview circles approximate the visual output of RGBW LEDs:

- **Additive color mixing**: RGB channels combine as light (R+G=Yellow, etc.)
- **White channel**: Blends color towards white (max 80% at W=255) while preserving hue
- **Brightness**: Acts as intensity using gamma 0.5 curve for perceptual accuracy

### Auto-Apply on Boot

When enabled (default), the device automatically applies the first scene in the
scene list after startup with a configurable fade duration. This allows lights
to smoothly turn on when the layout powers up.

- Configurable via LCC tools (JMRI, etc.)
- Default: Enabled with 10-second transition
- Assumes initial state is all channels at 0 (lights off)

### Power Saving (Screen Timeout)

The screen backlight automatically turns off after a configurable idle period to
save power. Touch the screen to wake it.

- **Configurable timeout**: 0 (disabled), or 10-3600 seconds
- **Default**: 60 seconds
- **Smooth transitions**: 1-second fade-to-black before backlight off, fade-in on wake
- **Touch-to-wake**: Touch during fade or sleep immediately begins wake animation
- **Note**: The backlight is on/off only (not dimmable) due to CH422G I/O expander
  hardware limitation. Fade effect is achieved via LVGL overlay animation.

Configuration via LCC tools (JMRI, etc.):
- Set to 0 to keep screen always on
- Minimum enabled timeout is 10 seconds

## Architecture

### Task Model

| Task | Priority | Core | Responsibility |
|------|----------|------|--------------|
| lvgl_task | 2 | CPU1 | LVGL rendering via `lv_timer_handler()` |
| openmrn_task | 5 | Any | OpenMRN executor loop |
| lighting_task | 4 | Any | Fade controller tick (10ms interval) |
| main_task | 1 | CPU1 | Hardware init, app orchestration |

CPU0 is dedicated to RGB LCD DMA interrupt handling for smooth display updates.

### State Flow

```
BOOT → SPLASH → LCC_INIT → MAIN_UI
                    ↓
              (timeout: degraded mode)
```

## License

BSD 2-Clause License — See [LICENSE](LICENSE) for details.

## Acknowledgments

- [OpenMRN](https://github.com/bakerstu/openmrn) — LCC/OpenLCB implementation
- [LVGL](https://lvgl.io/) — Graphics library
- [Espressif](https://www.espressif.com/) — ESP-IDF framework
