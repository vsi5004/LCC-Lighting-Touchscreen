# Architecture

## 1. Project Structure

```
LCCLightingTouchscreen/
├── CMakeLists.txt
├── sdkconfig.defaults
├── components/
│   ├── OpenMRN/              # Git submodule
│   └── board_drivers/        # Hardware abstraction
│       ├── ch422g.c/.h       # I2C expander driver
│       ├── waveshare_lcd.c/.h
│       ├── waveshare_touch.c/.h
│       ├── waveshare_sd.c/.h
│       └── waveshare_backlight.c/.h
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml     # LVGL 8.x, esp_lcd_touch, esp_jpeg
│   ├── Kconfig.projbuild
│   ├── main.c
│   ├── lv_conf.h             # LVGL configuration (performance tuning)
│   ├── app/                  # Application logic
│   │   ├── app_main.c/.h
│   │   ├── lighting_task.c/.h
│   │   ├── lcc_node.cpp/.h
│   │   ├── scene_manager.c/.h
│   │   ├── scene_storage.c/.h  # SD card scene persistence
│   │   └── fade_controller.c/.h
│   └── ui/                   # LVGL screens
│       ├── ui_common.c/.h
│       ├── ui_splash.c/.h
│       ├── ui_manual.c/.h
│       └── ui_scenes.c/.h    # Card carousel scene selector
└── docs/
```

---

## 2. Task Model

### Tasks

| Task | Priority | Stack | Core | Responsibility |
|------|----------|-------|------|----------------|
| UI Task | 2 | 6KB | Any | LVGL rendering and touch input |
| LCC Task | 5 | 4KB | Any | OpenMRN executor |
| Lighting Task | 4 | 4KB | Any | State, fades, rate limiting |
| SD Worker | 3 | 4KB | Any | Scene and config I/O |

---

## 3. Inter-Task Communication

- **UI → Lighting**: FreeRTOS queue (commands: UPDATE, APPLY_SCENE, CANCEL)
- **Lighting → LCC**: Direct OpenMRN event producer API
- **SD Worker → UI**: FreeRTOS queue (notifications: SCENE_LOADED, SAVE_COMPLETE)
- **LVGL mutex**: Required for all LVGL API access from non-UI tasks

---

## 4. State Machines

### Application State

```
BOOT → SPLASH → LCC_INIT → MAIN_UI
         ↓          ↓
     (timeout)  (timeout)
         ↓          ↓
      MAIN_UI ← MAIN_UI (degraded)
```

### Lighting State

```
IDLE ←──────────────────┐
  ↓                     │
  ├─ UPDATE ──→ SENDING │
  │                ↓    │
  │            COMPLETE─┘
  │
  └─ APPLY ───→ FADING
                   ↓
               COMPLETE─┘
```

- New Apply cancels active fade
- UPDATE sends all 5 parameters with rate limiting
- FADING interpolates over configured duration

---

## 5. OpenMRN Integration

### Build Configuration

OpenMRN requires ESP-IDF 5.1.6 (GCC 12.2.0) due to newlib/libstdc++ incompatibility 
in GCC 13.x/14.x. The component CMakeLists.txt excludes `EspIdfWiFi.cxx` (uses 
ESP-IDF 5.3+ APIs) since this project uses CAN, not WiFi.

Key compile options:
- C++ Standard: C++14
- `-fno-strict-aliasing` — Required for OpenMRN compatibility
- `-D_GLIBCXX_USE_C99` — C99 compatibility defines
- `-Wno-volatile` — Suppress deprecated volatile warnings

### LVGL Performance Tuning

The following settings in `lv_conf.h` optimize scroll performance:

| Setting | Value | Purpose |
|---------|-------|---------|
| `LV_DISP_DEF_REFR_PERIOD` | 16 | ~60 FPS refresh rate |
| `LV_INDEV_DEF_READ_PERIOD` | 10 | Fast touch polling (10ms) |
| `LV_INDEV_DEF_SCROLL_THROW` | 5 | Reduced scroll momentum |
| `LV_INDEV_DEF_SCROLL_LIMIT` | 30 | Lower scroll sensitivity |

Scene cards omit shadows to improve scroll frame rate.

### Driver
- Uses `Esp32HardwareTwai` from OpenMRN
- VFS path: `/dev/twai/twai0`
- Pins: TX=GPIO15, RX=GPIO16

### Initialization Sequence
1. Read `config.json` from SD for Node ID
2. Create `Esp32HardwareTwai` instance
3. Call `twai.hw_init()`
4. Initialize OpenMRN SimpleCanStack
5. Add CAN port via `add_can_port_async("/dev/twai/twai0")`

### Event Production
- Event ID format: `{base_event_id[0:6]}.{param_offset}.{value}`
- Rate limited to ≥20ms between events

---

## 6. Fade Algorithm (Normative)

1. Compute per-channel delta: `delta[ch] = target[ch] - current[ch]`
2. Steps = `max(abs(delta[0..4]))`
3. Ideal interval = `duration_ms / steps`
4. Actual interval = `max(ideal_interval, 20 ms)`
5. Actual steps = `duration_ms / actual_interval`
6. Per-step increment = `delta[ch] / actual_steps` (floating point)
7. Transmit integer values only (accumulate fractional remainder)
8. Order: Brightness first, then R, G, B, W