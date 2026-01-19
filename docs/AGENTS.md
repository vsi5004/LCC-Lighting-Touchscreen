# Agent Operating Rules

## Implementation Status

| Component | Status | Notes |
|-----------|--------|-------|
| Board Drivers | ✓ Complete | CH422G, LCD, Touch, SD all working |
| LCC/OpenMRN | ✓ Complete | Node init, TWAI, event production, CDI/ACDI |
| LVGL UI | ✓ Complete | Scene Selector (left) + Manual Control tabs |
| Scene Manager | ✓ Complete | JSON parse, auto-create default scenes |
| Fade Controller | ✓ Complete | State machine, interpolation, rate limiting |
| SD Error Screen | ✓ Complete | Displays when SD card missing |
| Progress Bar | ✓ Complete | LVGL timer updates, auto-hides on completion |
| Auto-Apply | ✓ Complete | Applies first scene on boot with configurable duration |
| Color Preview | ✓ Complete | RGBW light mixing preview circles on cards and manual tab |

### Recent Changes (Session 2026-01-18)
- Fixed SNIP user info display (CDI space 251 with origin 1)
- Fixed Base Event ID CDI offset (changed from 128 to 132)
- Implemented fade_controller with linear interpolation
- Wired UI Apply buttons to fade_controller
- Added progress bar updates via LVGL timer
- Added SD card error screen (persists until restart)
- Updated branding: IvanBuilds / LCC Touchscreen Controller
- Swapped tab order: Scene Selector now first (leftmost)
- Improved Scene Selector layout (smaller cards, proper spacing)
- Fixed progress bar to hide when fade reaches 100%
- Added auto-apply first scene on boot (LCC configurable)
- Added color preview circles to Manual Control tab and scene cards
- Fixed fade controller completion bug (next_param_index stuck at end)
- Fixed progress bar not hiding on first auto-apply (added fade_started flag)

---

## Component Ownership

| Component | Files | Scope |
|-----------|-------|-------|
| Board Drivers | `components/board_drivers/*` | CH422G, LCD, Touch, SD |
| LCC/OpenMRN | `main/app/lcc_node.*` | Node init, event production, TWAI |
| LVGL UI | `main/ui/*` | Screens, widgets, touch handling |
| Scene Manager | `main/app/scene_manager.*` | JSON parse/save, atomic writes |
| Fade Controller | `main/app/fade_controller.*` | Rate limiting, interpolation |

---

## Definition of Done

- Builds under ESP-IDF v5.1.6 with no errors (warnings acceptable in OpenMRN)
- Requirement IDs (FR-xxx) referenced in code comments
- Unit tests added or updated
- No blocking I/O in UI task
- LVGL calls protected by mutex
- Memory allocations use appropriate heap (PSRAM for large buffers)

---

## Dependencies

### External Components (idf_component.yml)
- `lvgl/lvgl: "^8"` — UI framework
- `espressif/esp_lcd_touch: "*"` — Touch abstraction
- `espressif/esp_lcd_touch_gt911: "*"` — GT911 driver
- `espressif/esp_jpeg: "*"` — JPEG decoding for splash

### Git Submodules
- `components/OpenMRN` — LCC/OpenLCB stack

---

## Change Control

| Change Type | Required Updates |
|-------------|------------------|
| Event mapping | SPEC.md §3, INTERFACES.md §7 |
| Task model | ARCHITECTURE.md §2 |
| GPIO assignments | INTERFACES.md, sdkconfig.defaults |
| Config file format | SPEC.md §4, scene_manager |
| New component | AGENTS.md, CMakeLists.txt |

---

## Build & CI

### Local Build
```bash
# Ensure ESP-IDF 5.1.6 is installed and exported
. $HOME/esp/v5.1.6/export.sh   # Linux/macOS
# or: C:\Users\<user>\esp\v5.1.6\export.ps1  # Windows PowerShell

idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

### Windows-Specific Setup

OpenMRN's `include/esp-idf/` directory contains Unix symlinks that appear as 
plain text files on Windows. Before building, verify these files contain 
`#include` directives rather than bare paths. If not, convert them:

```
// Bad (broken symlink):
../openmrn_features.h

// Good (proper wrapper):
#include "../openmrn_features.h"
```

Files requiring this fix:
- `openmrn_features.h`, `freertos_includes.h`, `can_frame.h`, `can_ioctl.h`
- `ifaddrs.h`, `i2c.h`, `i2c-dev.h`, `nmranet_config.h`, `stropts.h`
- `CDIXMLGenerator.hxx`, `sys/tree.hxx`

### GitHub Actions
- Workflow: `.github/workflows/build.yml`
- Container: `espressif/idf:v5.1.6`
- Artifacts: `build/*.bin`
- Submodules: Recursive checkout required
