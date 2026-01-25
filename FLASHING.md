# Flashing Guide

This guide covers multiple methods for flashing the LCC Lighting Touchscreen Controller firmware to your Waveshare ESP32-S3 Touch LCD 4.3B.

## Quick Start (Recommended)

Download the merged binary from the [latest release](https://github.com/vsi5004/LCC-Lighting-Touchscreen/releases/latest) and flash to address `0x0`:

```bash
esptool.py --chip esp32s3 --port COMX write_flash 0x0 LCCLightingTouchscreen-vX.X.X-XXXXXXX-merged.bin
```

Replace `COMX` with your serial port (e.g., `COM3` on Windows, `/dev/ttyUSB0` on Linux, `/dev/cu.usbserial-*` on macOS).

## Prerequisites

### Hardware

- **Waveshare ESP32-S3 Touch LCD 4.3B** with USB-C cable
- Computer with USB port

### Two-Piece Mount Users

⚠️ **Important:** If using the two-piece mount, you must remove the board from the mount to access the USB port for initial firmware flashing. See [printed_mounts/README.md](printed_mounts/README.md) for details.

### Installing esptool.py

If you don't have esptool installed:

```bash
pip install esptool
```

Or use the standalone executable from [GitHub releases](https://github.com/espressif/esptool/releases).

## Method 1: Merged Binary (Easiest)

This single-file approach is the simplest and fastest.

### Step 1: Download

From the [releases page](https://github.com/vsi5004/LCC-Lighting-Touchscreen/releases), download:
- `LCCLightingTouchscreen-vX.X.X-XXXXXXX-merged.bin`

### Step 2: Connect Device

1. Connect the ESP32-S3 board to your computer via USB-C
2. The board should appear as a serial device
3. Note the port name (e.g., `COM3`, `/dev/ttyUSB0`)

### Step 3: Flash

```bash
esptool.py --chip esp32s3 --port COMX write_flash 0x0 LCCLightingTouchscreen-vX.X.X-XXXXXXX-merged.bin
```

**Optional flags:**
- `--baud 921600` - Faster upload (default is 115200)
- `--before default_reset` - Reset before flashing
- `--after hard_reset` - Reset after flashing

Example with all options:
```bash
esptool.py --chip esp32s3 --port COM3 --baud 921600 \
  --before default_reset --after hard_reset \
  write_flash 0x0 LCCLightingTouchscreen-v1.0.0-abc1234-merged.bin
```

### Step 4: Verify

After flashing completes:
1. The device will reset automatically
2. You should see the boot splash screen
3. If not, press the reset button on the board

## Method 2: Individual Binaries (Advanced)

Use this method if you need to flash specific partitions (e.g., updating only the application).

### Step 1: Download

From the releases page, download all files:
- `bootloader-XXXXXXX.bin`
- `partition-table-XXXXXXX.bin`
- `ota_data_initial-XXXXXXX.bin`
- `LCCLightingTouchscreen-vX.X.X-XXXXXXX.bin`

### Step 2: Flash All Partitions

```bash
esptool.py --chip esp32s3 --port COMX write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 bootloader-XXXXXXX.bin \
  0x8000 partition-table-XXXXXXX.bin \
  0xf000 ota_data_initial-XXXXXXX.bin \
  0x20000 LCCLightingTouchscreen-vX.X.X-XXXXXXX.bin
```

### Step 3: Application Only Update

To update just the application (preserves bootloader and partition table):

```bash
esptool.py --chip esp32s3 --port COMX write_flash 0x20000 LCCLightingTouchscreen-vX.X.X-XXXXXXX.bin
```

## Method 3: ESP Flash Download Tool (Windows GUI)

Espressif provides a graphical tool for Windows users.

### Step 1: Download Tool

Get the tool from [Espressif's website](https://www.espressif.com/en/support/download/other-tools):
- **ESP Flash Download Tool**

### Step 2: Configure Tool

1. Launch `flash_download_tool_x.x.x.exe`
2. Select **ChipType**: `ESP32-S3`
3. Select **WorkMode**: `Develop`

### Step 3: Add Files

In the download panel, add each file with its address:

| File | Address (hex) | Checkbox |
|------|---------------|----------|
| `bootloader-XXXXXXX.bin` | `0x0` | ☑ |
| `partition-table-XXXXXXX.bin` | `0x8000` | ☑ |
| `ota_data_initial-XXXXXXX.bin` | `0xf000` | ☑ |
| `LCCLightingTouchscreen-vX.X.X-XXXXXXX.bin` | `0x20000` | ☑ |

**Or** use the merged binary:

| File | Address (hex) | Checkbox |
|------|---------------|----------|
| `LCCLightingTouchscreen-vX.X.X-XXXXXXX-merged.bin` | `0x0` | ☑ |

### Step 4: Configure Flash Settings

Set the following options:
- **SPI SPEED**: `80MHz`
- **SPI MODE**: `DIO`
- **FLASH SIZE**: `16MB`
- **COM Port**: Select your device's port
- **BAUD**: `921600` (or `115200` if errors occur)

### Step 5: Flash

1. Click **START**
2. Progress bar will show upload status
3. "FINISH" message appears when complete
4. Device resets automatically

## Method 4: Web-Based Flasher (Experimental)

Some web-based tools support ESP32-S3 flashing via Chrome/Edge's Web Serial API:

- [ESP Web Tools](https://esp.huhn.me/) by Spacehuhn
- [Adafruit WebSerial ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/)

**Note:** Not all tools support ESP32-S3 yet. Check compatibility before use.

### Using ESP Web Tools (Example)

1. Visit the web flasher in Chrome/Edge
2. Click **Connect** and select your device
3. Add binaries at their respective addresses:
   - `0x0`: `bootloader-XXXXXXX.bin`
   - `0x8000`: `partition-table-XXXXXXX.bin`
   - `0xf000`: `ota_data_initial-XXXXXXX.bin`
   - `0x20000`: `LCCLightingTouchscreen-vX.X.X-XXXXXXX.bin`
4. Click **Program**
5. Wait for completion

## Troubleshooting

### Device Not Detected

**Windows:**
- Install [CP210x USB to UART drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
- Check Device Manager for "USB Serial Device" or "Silicon Labs CP210x"

**Linux/macOS:**
- Add your user to dialout/uucp group: `sudo usermod -a -G dialout $USER`
- Log out and back in
- Check permissions: `ls -l /dev/ttyUSB*`

### Flash Errors

**"Failed to connect":**
1. Hold the BOOT button while connecting USB
2. Try a different USB cable (some cables are power-only)
3. Try a different USB port
4. Reduce baud rate to `115200`

**"Invalid head of packet":**
- Wrong chip type selected (must be esp32s3)
- Corrupt download file — re-download and verify SHA256

**"MD5 of file does not match":**
- File corruption during download
- Re-download the binary
- Verify checksum against `SHA256SUMS.txt`

### Boot Issues After Flash

**Screen stays black:**
1. Press the hardware reset button
2. Check power supply (USB-C should provide sufficient power)
3. Try re-flashing with `--after hard_reset`

**Device keeps resetting:**
- Flash may have failed partially
- Re-flash bootloader: `esptool.py --chip esp32s3 --port COMX write_flash 0x0 bootloader-XXXXXXX.bin`
- If problem persists, erase flash and reflash: `esptool.py --chip esp32s3 --port COMX erase_flash`

## Post-Flash Configuration

After successful flashing, configure your device via SD card. See [README.md - SD Card Setup](README.md#sd-card-setup) for details.

### Required Files

1. **`nodeid.txt`** — Your LCC node ID in dotted hex format:
   ```
   05.01.01.01.9F.60.00
   ```
   See [Node ID Configuration](#node-id-configuration) below.

2. **`scenes.json`** — Initial lighting scenes (optional, can create in UI):
   ```json
   {
     "version": 1,
     "scenes": [
       { "name": "sunrise", "brightness": 180, "r": 255, "g": 120, "b": 40, "w": 0 }
     ]
   }
   ```

3. **`splash.jpg`** — Custom boot image (optional, 800×480 px, baseline JPEG)

### Node ID Configuration

The device needs a **unique** LCC node ID. You have two options:

#### Option 1: SD Card `nodeid.txt` (Recommended)

Create a plain text file on the SD card root with your 48-bit node ID in dotted hex format:

```
05.01.01.01.9F.60.00
```

**Format rules:**
- 7 groups of 2 hex digits
- Separated by periods
- No spaces or extra characters
- Case insensitive

**Generating a unique Node ID:**
- Use [OpenLCB Node ID Tool](https://registry.openlcb.org/)
- Or manually: Follow OpenLCB node ID allocation guidelines
- Ensure uniqueness on your LCC network

#### Option 2: Compile-Time Default (Advanced)

If you're building from source, you can set a default node ID in the code that will be used if `nodeid.txt` is missing:

1. Open `main/app/lcc_node.cpp`
2. Find the `LCC_DEFAULT_NODE_ID` definition:
   ```cpp
   static constexpr openlcb::NodeID LCC_DEFAULT_NODE_ID = 0x050101019F6000ULL;
   ```
3. Replace with your node ID (format: `0x` + 12 hex digits + `ULL`)
4. Rebuild and flash

**Priority:** `nodeid.txt` on SD card always overrides the compiled default.

### First Boot

1. Insert configured SD card
2. Power on the device
3. Boot splash appears (or default ESP32 logo)
4. LCC initialization (2-3 seconds)
5. Main UI appears

If LCC initialization times out (no CAN bus detected), the device enters degraded mode where the UI is available but lighting commands won't be sent.

## Over-the-Air (OTA) Updates

After initial flash, you can update firmware over LCC using JMRI without physical USB access. See [README.md - Firmware Updates (OTA)](README.md#firmware-updates-ota) for instructions.

**For OTA updates, use the application-only binary:**
- `LCCLightingTouchscreen-vX.X.X-XXXXXXX.bin` (NOT the merged binary)

## Verifying Your Flash

After flashing and SD card setup:

1. **Check boot screen** — Should show custom splash or ESP32 logo
2. **Check LCC initialization** — Status text appears during boot
3. **Open Scene Selector** — Should show any scenes from `scenes.json`
4. **Test touch** — Tap, swipe, and navigate UI
5. **Test LCC communication** — Try applying a scene (check your LCC bus activity)

## Building From Source

If you prefer to build your own binaries:

```bash
# Clone with submodules
git clone --recursive https://github.com/vsi5004/LCC-Lighting-Touchscreen.git
cd LCC-Lighting-Touchscreen

# Set target
idf.py set-target esp32s3

# Build
idf.py build

# Flash
idf.py -p COMX flash monitor
```

See [README.md - Building](README.md#building) for detailed build instructions including ESP-IDF version requirements.

## Additional Resources

- **ESP-IDF Documentation**: [Flash Download Tool Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/tools/idf-py.html)
- **esptool Documentation**: [https://github.com/espressif/esptool](https://github.com/espressif/esptool)
- **OpenLCB Node ID Registry**: [https://registry.openlcb.org/](https://registry.openlcb.org/)
- **Project Issues**: [GitHub Issues](https://github.com/vsi5004/LCC-Lighting-Touchscreen/issues)

## Getting Help

If you encounter issues:

1. Check the [Troubleshooting](#troubleshooting) section
2. Verify SHA256 checksums of downloaded files
3. Try a different USB cable or port
4. Open an issue on GitHub with:
   - Hardware revision
   - Flash method used
   - Complete error output
   - Steps to reproduce
