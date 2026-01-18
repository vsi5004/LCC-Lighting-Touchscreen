# Agent Operating Rules

## Component Ownership

| Component | Files | Scope |
|-----------|-------|-------|
| Board Drivers | `components/board_drivers/*` | CH422G, LCD, Touch, SD, Backlight |
| LCC/OpenMRN | `main/app/lcc_node.*` | Node init, event production, TWAI |
| LVGL UI | `main/ui/*` | Screens, widgets, touch handling |
| Scene Manager | `main/app/scene_manager.*` | JSON parse/save, atomic writes |
| Fade Controller | `main/app/fade_controller.*` | Rate limiting, interpolation |
| Lighting Task | `main/app/lighting_task.*` | State machine, command dispatch |

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
