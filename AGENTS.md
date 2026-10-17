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

## Hardware Quirks (learned on real boards — do not regress)
- **C3 UART bridge pins**: SuperMini silkscreen labels **GPIO21 = RXD0, GPIO20 = TXD0**. Bridge is fixed at `Serial1.begin(115200, SERIAL_8N1, 21, 20)`; UART0 must be detached first (`Serial0.end()` + `uartSetPins(0,-1,-1,-1,-1)` in `UartCpxBridge::begin`).
- **C3 TX power**: `WiFi.setTxPower(WIFI_POWER_11dBm)` is required — the SuperMini's cramped antenna turns 20 dBm into distortion (SoftAP beacon invisible, STA joins fail).
- **C3 Wi-Fi watchdog**: any STA attempt (boot or `/api/wifi`) gets ~20 s; on failure it drops to pure `WIFI_AP` so STA scans stop suppressing SoftAP beacons. NVS creds (namespace `wifi_cfg`) override `wifi_secrets.h`.
- **CPX float printf**: `cpx_node` needs `-Wl,-u,_printf_float` or `snprintf("%.1f")` emits empty fields and telemetry decodes garbage.
- **CPX USB serial**: do NOT write to `Serial` inside the publish/telemetry path — TinyUSB writes can block the loop; `Serial1` (pins 0/1 = A6/A7) is the app UART.
- **CPX speaker amp**: `D11` (`CPLAY_SPEAKER_SHUTDOWN`) stays LOW when idle; only `enableSpeakerAmp()` raises it during audio output.
- **CPX USB ID**: sketch enumerates as `239a:8018` ("Circuit Playground Expresso"); UF2 bootloader appears as mass storage after double-clicking Reset.
