# Architecture

## 1. Project Structure

```
LCCLightingTouchscreen/
├── CMakeLists.txt
├── sdkconfig.defaults
├── lv_conf.h                 # LVGL configuration (root level)
├── components/
│   ├── OpenMRN/              # Git submodule
│   └── board_drivers/        # Hardware abstraction
│       ├── ch422g.c/.h       # I2C expander driver
│       ├── waveshare_lcd.c/.h
│       ├── waveshare_touch.c/.h
│       └── waveshare_sd.c/.h
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml     # LVGL 8.x, esp_lcd_touch, esp_jpeg
│   ├── Kconfig.projbuild
│   ├── main.c                # Entry point, hardware init, SD error screen
│   ├── lv_conf.h             # LVGL configuration (main level)
│   ├── app/                  # Application logic
│   │   ├── lcc_node.cpp/.h   # OpenMRN integration, event production
│   │   ├── scene_manager.c/.h
│   │   └── fade_controller.c/.h  # ✓ Implemented: state machine, interpolation
│   └── ui/                   # LVGL screens
│       ├── ui_common.c/.h    # LVGL init, mutex, flush callbacks
│       ├── ui_main.c/.h      # Main tabview container
│       ├── ui_manual.c/.h    # Manual RGBW sliders, Apply button
│       └── ui_scenes.c/.h    # Card carousel, progress bar, Apply button
└── docs/
```

---

## 2. Task Model

### Tasks

| Task | Priority | Stack | Core | Responsibility |
|------|----------|-------|------|----------------|
| lvgl_task | 2 | 6KB | Any | LVGL rendering via `lv_timer_handler()` |
| openmrn_task | 5 | 8KB | Any | OpenMRN executor loop |
| lighting_task | 4 | 4KB | Any | Fade controller tick (10ms interval) |
| main_task | 1 | 4KB | Any | Hardware init, app orchestration |

### Task Implementation Notes
- **lvgl_task**: Created by `ui_init()`, runs continuously calling `lv_timer_handler()`
- **openmrn_task**: Created by `lcc_node_init()`, runs OpenMRN's internal executor
- **lighting_task**: Created in `app_main()`, calls `fade_controller_tick()` every 10ms

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

### Lighting State (Fade Controller)

```
IDLE ←──────────────────┐
  │                     │
  ├─ start() ──→ FADING │
  │                ↓    │
  │            COMPLETE─┘
  │
  └─ apply_immediate() ─→ (sends all params, stays IDLE)
```

**Implemented in `fade_controller.c`:**
- `fade_controller_start()`: Begins timed fade to target values
- `fade_controller_apply_immediate()`: Sends all 5 params instantly (rate-limited)
- `fade_controller_tick()`: Called every 10ms, advances fade state machine
- `fade_controller_get_progress()`: Returns 0.0-1.0 for progress bar updates
- `fade_controller_abort()`: Cancels active fade, resets to IDLE

**Rate Limiting:** Minimum 10ms between transmission rounds
**Transmission Mode:** Burst (all 5 params sent together each round)
**Transmission Order:** Brightness → R → G → B → W

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
1. Read `nodeid.txt` from SD for 12-digit hex Node ID
2. Create `Esp32HardwareTwai` instance
3. Call `twai.hw_init()`
4. Initialize OpenMRN SimpleCanStack
5. Add CAN port via `add_can_port_async("/dev/twai/twai0")`

### Auto-Apply on Boot
When enabled via LCC configuration (CDI):
1. Load first scene from `scenes.json`
2. Assume initial lighting state is all zeros
3. Start fade to first scene using configured duration (default 10 sec)
4. Progress bar on Scene Selector tab shows fade progress

### Event Production
- Event ID format: `{base_event_id[0:6]}.{param_offset}.{value}`
- Rate limited to ≥10ms between transmission rounds (5 params per round)

---

## 6. Fade Algorithm (Normative)

### Algorithm Steps
1. Compute per-channel delta: `delta[ch] = target[ch] - current[ch]`
2. Steps = `max(abs(delta[0..4]))`
3. Ideal interval = `duration_ms / steps`
4. Actual interval = `max(ideal_interval, 10 ms)`
5. Actual steps = `duration_ms / actual_interval`
6. Per-step increment = `delta[ch] / actual_steps` (floating point)
7. Transmit integer values only (accumulate fractional remainder)
8. Burst all 5 params together: Brightness, R, G, B, W

### Implementation Details (`fade_controller.c`)
- **Float Accumulators**: Each channel uses `float` accumulator to prevent drift
- **Integer Transmission**: Only transmits when `(int)new_value != (int)old_value`
- **Burst Transmission**: All 5 params sent together per round for smooth fades
- **Rate Limiting**: Enforces 10ms minimum between transmission rounds
- **Progress Tracking**: `elapsed_ms / duration_ms` clamped to 0.0-1.0
- **State Machine**: IDLE → FADING → COMPLETE → IDLE

### UI Integration
- **Scene Selector Tab** (leftmost): Card carousel with color preview circles, "Apply" starts fade, progress bar
- **Manual Control Tab**: RGBW sliders, color preview circle, "Apply" calls `fade_controller_apply_immediate()`
- **Progress Bar**: LVGL timer (100ms) polls `fade_controller_get_progress()`, hides when fade completes
- **Auto-Apply on Boot**: Calls `ui_scenes_start_progress_tracking()` to show fade progress

---

## 7. Color Preview Algorithm

The `ui_calculate_preview_color()` function approximates RGBW LED visual output for display.

### Mixing Rules
```c
// White blends towards white (80% max at W=255)
full_r = r + ((255 - r) * w) / 320;
full_g = g + ((255 - g) * w) / 320;
full_b = b + ((255 - b) * w) / 320;

// Brightness as intensity (gamma 0.5 via square root)
intensity = sqrt(brightness * 255);  // 0-255

// Final output
result = (full_rgb * intensity) / 255;
```

### Design Rationale
- **Additive Light Mixing**: RGB channels combine as light, not pigments
- **White Preservation**: High white values brighten but don't completely wash out color
- **Perceptual Brightness**: Square root curve makes low brightness values more visible