# Interfaces

## 1. I2C Bus

### Configuration
- SCL: GPIO9
- SDA: GPIO8
- Frequency: 400 kHz
- Port: I2C_NUM_0

### Devices
| Address | Device | Purpose |
|---------|--------|---------|
| 0x24 | CH422G | Mode register |
| 0x38 | CH422G | Output register |
| 0x5D | GT911 | Touch controller |

---

## 2. CH422G I/O Expander

The CH422G controls SD card CS, LCD backlight, and touch reset.

### Mode Configuration
Write `0x01` to address `0x24` to set output mode.

### Output Register (0x38) Bit Mapping
| Bit | Function |
|-----|----------|
| 0 | SD Card CS (active low) |
| 1 | Touch Reset |
| 2 | LCD Backlight |
| 3-7 | Reserved |

### Common Commands
| Action | Value | Notes |
|--------|-------|-------|
| Backlight ON | 0x1E | CS high, touch normal, BL on |
| Backlight OFF | 0x1A | CS high, touch normal, BL off |
| SD CS Low | 0x0A | Enable SD card |
| Touch Reset Start | 0x2C | Assert touch reset |
| Touch Reset End | 0x2E | Release touch reset |

---

## 3. SPI / SD Card

### SPI Pins
- MOSI: GPIO11
- MISO: GPIO13
- CLK: GPIO12
- CS: Via CH422G (not direct GPIO)

### Required SD Enable Sequence
1. Initialize I2C bus
2. Write `0x01` to CH422G address `0x24` (output mode)
3. Write `0x0A` to CH422G address `0x38` (SD CS low)
4. Initialize SPI bus (SDSPI_HOST_DEFAULT)
5. Mount FATFS at `/sdcard`

### SD Card Files
| File | Purpose |
|------|---------|
| `/sdcard/nodeid.txt` | LCC Node ID (plain text, dotted hex) |
| `/sdcard/scenes.json` | Scene definitions (auto-created if missing) |
| `/sdcard/splash.jpg` | Boot splash image |
| `/sdcard/openmrn_config` | OpenMRN persistent config (auto-created) |

---

## 4. RGB LCD

### Timing Configuration (800x480 @ 16MHz)
- Pixel Clock: 16 MHz
- HSYNC Pulse: 4, Back Porch: 8, Front Porch: 8
- VSYNC Pulse: 4, Back Porch: 8, Front Porch: 8

### GPIO Mapping
| Signal | GPIO |
|--------|------|
| VSYNC | 3 |
| HSYNC | 46 |
| DE | 5 |
| PCLK | 7 |
| DATA0-4 | 14, 38, 18, 17, 10 |
| DATA5-7 | 39, 0, 45 |
| DATA8-11 | 48, 47, 21, 1 |
| DATA12-15 | 2, 42, 41, 40 |

### Frame Buffer
- Location: PSRAM
- Format: RGB565 (16-bit)
- Size: 800 × 480 × 2 bytes × 2 buffers = 1.5MB
- Mode: Full-frame double buffering (eliminates horizontal banding during animations)

---

## 5. Touch Controller (GT911)

### Configuration
- I2C Address: 0x5D (after reset sequence)
- Interrupt: Not used (polling)
- Resolution: Matches LCD (800x480)

### Reset Sequence
1. Write `0x01` to CH422G `0x24`
2. Write `0x2C` to CH422G `0x38`
3. Delay 100ms
4. Set GPIO4 LOW
5. Delay 100ms
6. Write `0x2E` to CH422G `0x38`
7. Delay 200ms

---

## 6. CAN / TWAI (LCC Bus)

### GPIO Mapping
- TX: GPIO15
- RX: GPIO16

### Configuration
- Bit Rate: 125 kbps (LCC standard)
- Mode: Normal (with ACK)
- Driver: OpenMRN `Esp32HardwareTwai`
- VFS Path: `/dev/twai/twai0`

---

## 7. LCC Event Mapping

### Node ID
Configured in `/sdcard/nodeid.txt` (12 hex digits, e.g., `050101012260`)

### Base Event ID
Configured via LCC CDI, stored in `/sdcard/openmrn_config` at offset 132.
Default: `05.01.01.01.22.60.00.00`

### Parameter Offsets
| Parameter | Offset (byte 6) | Event ID Example |
|-----------|-----------------|------------------|
| Red | 0x00 | 05.01.01.01.22.60.00.xx |
| Green | 0x01 | 05.01.01.01.22.60.01.xx |
| Blue | 0x02 | 05.01.01.01.22.60.02.xx |
| White | 0x03 | 05.01.01.01.22.60.03.xx |
| Brightness | 0x04 | 05.01.01.01.22.60.04.xx |

Where `xx` is the parameter value (0x00–0xFF).

### Transmission Order
1. Brightness
2. Red
3. Green
4. Blue
5. White

Minimum interval between transmission rounds: 10ms
Transmission mode: Burst (all 5 params sent together)
