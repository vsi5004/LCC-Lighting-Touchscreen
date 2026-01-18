# Test Plan

## Unit Tests

### Fade Algorithm (`test_fade.c`)
- Fade math with various deltas
- Rounding accumulation (no drift)
- Step count calculation
- Interval clamping to 20ms minimum

### Rate Limiter (`test_rate_limiter.c`)
- Enforces ≥20ms spacing
- Handles burst requests
- Timer accuracy within ±1ms

### Scene Manager (`test_scene_manager.c`)
- JSON parsing valid scenes
- JSON parsing with missing fields
- JSON parsing with invalid values
- Version mismatch detection
- Atomic save (simulate power loss)

### Config Manager (`test_config.c`)
- Parse valid config.json
- Parse with missing node_id
- Fallback to defaults

---

## Integration Tests

### Manual Control Flow
| Test | Steps | Expected |
|------|-------|----------|
| IT-001 | Set sliders, press Update | 5 CAN events in order (B,R,G,B,W) |
| IT-002 | Update with no changes | 5 CAN events with current values |
| IT-003 | Save Scene flow | Modal appears, scene saved to SD |

### Scene Application
| Test | Steps | Expected |
|------|-------|----------|
| IT-010 | Apply scene, 0s duration | Immediate 5 events |
| IT-011 | Apply scene, 10s duration | Gradual fade, events ≥20ms apart |
| IT-012 | Apply during fade | Previous fade cancelled |
| IT-013 | Apply same scene | No events (optimization) |

### Persistence
| Test | Steps | Expected |
|------|-------|----------|
| IT-020 | Save scene, reboot | Scene appears in carousel |
| IT-021 | Corrupt scenes.json | Fallback to empty scene list |
| IT-022 | Missing config.json | Default node ID used |

---

## Hardware-in-the-Loop Tests

### CAN Bus Verification
- Use CAN analyzer (PCAN, Kvaser, etc.)
- Verify ≥20ms spacing between all events
- Verify correct event IDs per INTERFACES.md §7
- Verify 125 kbps bit rate

### JMRI Integration
- Node appears in JMRI node browser
- Event IDs configurable via CDI (if implemented)
- Events received and logged correctly

### Display/Touch
- Splash image displays within 1500ms
- Touch coordinates map correctly
- Slider values update smoothly
- No screen tearing during animations

### SD Card
- Boot without SD card shows error
- Corrupt files handled gracefully
- Large scene files (50+ scenes) load correctly

---

## Performance Benchmarks

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| Boot to splash | <500ms | Stopwatch/logic analyzer |
| Splash to UI | <5000ms | Stopwatch |
| Touch latency | <50ms | Touch + oscilloscope on CAN |
| Fade timing accuracy | ±2% | CAN analyzer timestamps |
| UI frame rate | ≥30 FPS | LVGL stats or visual |
