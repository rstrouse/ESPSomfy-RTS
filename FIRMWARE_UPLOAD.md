# Upload Guide from Cursor

## Installing PlatformIO

1. **Install PlatformIO extension in Cursor**:
   - Open extensions (Cmd+Shift+X / Ctrl+Shift+X)
   - Search for "PlatformIO IDE"
   - Install the official extension

2. **Wait for installation**:
   - PlatformIO will automatically install necessary tools
   - This may take a few minutes the first time

## Serial Port Configuration

1. **Connect your ESP32-S3** via USB
2. **Identify the port**:
   - macOS: `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`
   - Windows: `COM3`, `COM4`, etc.
   - Linux: `/dev/ttyUSB0` or `/dev/ttyACM0`

3. **Uncomment and modify the line in `platformio.ini`**:
   ```ini
   upload_port = /dev/cu.usbserial-XXXX  ; Replace with your port
   ```

## Firmware Upload

### Method 1: Via PlatformIO Toolbar

1. Click on the **PlatformIO** icon in the left sidebar
2. In **PROJECT TASKS** → **esp32-s3-devkitc-1**
3. Click **Upload** (or **Build and Upload**)

### Method 2: Via Command Palette

1. Open command palette (Cmd+Shift+P / Ctrl+Shift+P)
2. Type "PlatformIO: Upload"
3. Select the `esp32-s3-devkitc-1` environment

### Method 3: Via Integrated Terminal

```bash
pio run --target upload
```

## LittleFS Filesystem Upload

**IMPORTANT**: After uploading the firmware, you must also upload the files from the `data/` folder (index.html, index.js, etc.)

### Via Command Palette

1. Command palette (Cmd+Shift+P / Ctrl+Shift+P)
2. Type "PlatformIO: Upload Filesystem Image"
3. Select the `esp32-s3-devkitc-1` environment

### Via Terminal

```bash
pio run --target uploadfs
```

## Verification

1. **Open serial monitor**:
   - Command palette → "PlatformIO: Serial Monitor"
   - Or PlatformIO icon → **Monitor**

2. **Check messages**:
   - You should see "Startup/Boot...."
   - And "File system mounted successfully"

3. **Connect to web interface**:
   - Look for IP address in serial logs
   - Or connect to WiFi "ESPSomfy RTS" if in AP mode

## Troubleshooting

### "Port not found" Error
- Verify ESP32 is properly connected
- Check port in `platformio.ini`
- On macOS/Linux, you may need permissions: `sudo chmod 666 /dev/cu.usbserial-*`

### Compilation Error
- Verify all libraries are installed
- PlatformIO installs them automatically, but you can force: `pio lib install`

### Filesystem Won't Upload
- Make sure firmware was uploaded first
- Verify the `data/` folder exists
- Some boards require manual reset before filesystem upload
