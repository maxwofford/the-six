# The Six

An e-paper display showing real-time bus arrivals for Green Mountain Transit Route 6 at 15 Falls Rd, Shelburne, VT.

## Software

Every 2 minutes:
1. Connects to WiFi
2. Fetches real-time arrivals from Trillium
3. Updates the display
4. Disconnects WiFi

## Hardware (will be)

- LILYGO T5 4.7" (ESP32 + e-paper display)
- USB-C power

## Setup

1. `cp src/secrets.h.example src/secrets.h`
2. `$EDITOR src/secrets.h` with your WiFi credentials
3. Open in VS Code with PlatformIO
4. Build and upload