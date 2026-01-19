# LCC Lighting Touchscreen Controller

An ESP32-S3–based LCC/OpenLCB lighting scene controller with a touch LCD user interface for model railroad layout lighting control. Designed specifically to interface with my [LCC Lighting Controller](https://github.com/vsi5004/LCCLightingController)


![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.1.6-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-orange)

## Overview

This device connects to an LCC (Layout Command Control) / OpenLCB CAN bus and sends RGBW lighting commands to follower lighting controller boards. It provides an intuitive touch interface for:

- **Manual Control** — Real-time RGBW + brightness adjustment via sliders
- **Scene Management** — Save, load, and smoothly transition between lighting presets
- **Configurable Fades** — Linear transitions up to 5 minutes with visual progress

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
├── sdkconfig.defaults          # ESP-IDF configuration
├── partitions.csv              # Flash partition table
├── components/
│   ├── OpenMRN/                # LCC/OpenLCB stack (git submodule)
│   └── board_drivers/          # Hardware abstraction layer
│       ├── ch422g.c/.h         # I2C expander driver
│       ├── waveshare_lcd.c     # Display initialization
│       ├── waveshare_touch.c   # Touch input handling
│       └── waveshare_sd.c      # SD card driver
├── main/
│   ├── main.c                  # Entry point
│   ├── lv_conf.h               # LVGL configuration
│   ├── app/                    # Application logic
│   │   ├── app_main.c/.h       # App state machine
│   │   ├── lighting_task.c/.h  # Lighting state & fades
│   │   ├── lcc_node.cpp/.h     # OpenMRN integration
│   │   ├── scene_manager.c/.h  # Scene business logic
│   │   └── scene_storage.c/.h  # SD card persistence
│   └── ui/                     # LVGL screens
│       ├── ui_common.c/.h      # Shared UI utilities
│       ├── ui_splash.c/.h      # Boot splash screen
│       ├── ui_manual.c/.h      # Manual RGBW control
│       └── ui_scenes.c/.h      # Scene card carousel
└── docs/                       # Design documentation
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

Automatically created by OpenMRN. Stores LCC configuration including:
- Base Event ID (configurable via LCC tools like JMRI)
- Auto-apply settings (enable/disable, transition duration)
- User-editable node name and description

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

- Minimum interval: **20 ms** between events
- Transmission order: Brightness → R → G → B → W
- All 5 parameters sent on Apply

## LCC Configuration

The following settings can be configured via any LCC configuration tool (JMRI, etc.):

| Setting | Default | Description |
|---------|---------|-------------|
| Base Event ID | 05.01.01.01.22.60.00.00 | Base for lighting event IDs |
| Auto-Apply on Boot | Enabled (1) | Apply first scene after startup |
| Auto-Apply Duration | 10 seconds | Fade duration for auto-apply (0-300s) |
| User Name | (editable) | Node name shown in LCC tools |
| User Description | (editable) | Node description shown in LCC tools |

## User Interface

### Scene Selector Tab (Default)

- Horizontal swipeable card carousel (240×260px cards)
- **Color preview circle** on each card showing approximate light output
- Center-snapping scroll behavior with blue selection highlight
- Transition duration slider (0–300 seconds)
- **Apply** performs smooth linear fade to selected scene
- Progress bar shows fade completion (auto-hides when done)
- Delete button on each card (with confirmation modal)

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

## Architecture

### Task Model

| Task | Priority | Responsibility |
|------|----------|----------------|
| lvgl_task | 2 | LVGL rendering via `lv_timer_handler()` |
| openmrn_task | 5 | OpenMRN executor loop |
| lighting_task | 4 | Fade controller tick (15ms interval) |
| main_task | 1 | Hardware init, app orchestration |

### State Flow

```
BOOT → SPLASH → LCC_INIT → MAIN_UI
                    ↓
              (timeout: degraded mode)
```

## License

MIT License — See [LICENSE](LICENSE) for details.

## Acknowledgments

- [OpenMRN](https://github.com/bakerstu/openmrn) — LCC/OpenLCB implementation
- [LVGL](https://lvgl.io/) — Graphics library
- [Espressif](https://www.espressif.com/) — ESP-IDF framework
