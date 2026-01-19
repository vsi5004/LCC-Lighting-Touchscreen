# Test Plan

## Unit Tests

### Fade Algorithm (`test_fade.c`)
- Fade math with various deltas
- Rounding accumulation (no drift)
- Step count calculation
- Interval clamping to 10ms minimum

### Rate Limiter (`test_rate_limiter.c`)
- Enforces ≥10ms spacing between transmission rounds
- Burst transmission (all 5 params per round)
- Timer accuracy within ±1ms

### Scene Manager (`test_scene_manager.c`)
- JSON parsing valid scenes
- JSON parsing with missing fields
- JSON parsing with invalid values
- Version mismatch detection
- Atomic save (simulate power loss)

### Config Manager (`test_config.c`)
- Parse valid nodeid.txt
- Parse with invalid node ID format
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
| IT-011 | Apply scene, 10s duration | Gradual fade, event rounds ≥10ms apart |
| IT-012 | Apply during fade | Previous fade cancelled |
| IT-013 | Apply same scene | No events (optimization) |

### Persistence
| Test | Steps | Expected |
|------|-------|----------|
| IT-020 | Save scene, reboot | Scene appears in carousel |
| IT-021 | Corrupt scenes.json | Fallback to empty scene list |
| IT-022 | Missing nodeid.txt | Default node ID used |

### Auto-Apply on Boot
| Test | Steps | Expected |
|------|-------|----------|
| IT-030 | Enable auto-apply, set 10s duration, reboot | First scene fades in over 10s, progress bar visible |
| IT-031 | Disable auto-apply, reboot | No fade, lights remain off |
| IT-032 | Enable auto-apply, no scenes saved | No fade occurs, no crash |
| IT-033 | Auto-apply with 0s duration | First scene applied immediately |

---

## Hardware-in-the-Loop Tests

### CAN Bus Verification
- Use CAN analyzer (PCAN, Kvaser, etc.)
- Verify ≥10ms spacing between transmission rounds (5 events per round)
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
