# MCsHAL Workspace Guide

## Repository Structure
- `PLAN.md`: Architecture, SOLID boundaries, UART protocol, and roadmap.
- `common/mcshal_protocol/`: Shared header-only library (`mcshal_protocol.hpp`) included by both nodes via `lib_extra_dirs = ../common`.
- `cpx_node/`: PlatformIO project for **Adafruit Circuit Playground Express** (`adafruit_circuitplayground_m0`, ATSAMD21 @ 48MHz).
  - Note: `lib_ignore = Adafruit TinyUSB Library` is required in `cpx_node/platformio.ini` to avoid USB header conflicts with standard SAMD Arduino USBCore.
- `c3_gateway/`: PlatformIO project for **ESP32-C3** (`esp32-c3-devkitm-1`).

## Build & Upload Commands
```bash
# Build CPX Sensory Node
~/.platformio/penv/bin/pio run -d cpx_node

# Build ESP32-C3 Gateway
~/.platformio/penv/bin/pio run -d c3_gateway

# Upload to connected board
~/.platformio/penv/bin/pio run -d cpx_node -t upload
~/.platformio/penv/bin/pio run -d c3_gateway -t upload
```
