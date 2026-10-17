# MCsHAL — Multi-Controller Living Room Sandbox Plan

## 1. System Overview & Hardware Roles

`MCsHAL` is a dual-microcontroller monorepo combining deterministic local sensing/actuation with wireless connectivity:

| Node | MCU / Board | Responsibilities |
| :--- | :--- | :--- |
| **`cpx_node`** | Adafruit Circuit Playground Express (`ATSAMD21G18A @ 48MHz`) | Reads ambient **Temperature**, **Light**, and **Sound (SPL dB)**; runs active `IGadgetMode` (MVP: *Comfyness Monitor*); drives the **10x NeoPixel ring** and **onboard speaker**; streams telemetry frames and receives configuration commands over `Serial1` (`RX=D0/A6`, `TX=D1/A7`). |
| **`c3_gateway`** | ESP32-C3 (`esp32-c3-devkitm-1`) | Connects to `cpx_node` via UART; hosts the **Wi-Fi HTTP + API server**; serves a real-time living-room dashboard displaying comfort metrics and allowing remote tuning of comfort thresholds, mute/unmute, and active gadget mode selection. |
| **`common/mcshal_protocol`** | Shared Header-Only C++ Library | Defines the zero-allocation line-based UART protocol (`TelemetryFrame`, `ConfigCommand`, serializer/parser) shared by both nodes. |

### Physical Wiring (UART Bridge)
Both boards run at **3.3V logic**, so no level shifter is required:
- `CPX TX (Pad A7 / D1)` $\rightarrow$ `ESP32-C3 RX (GPIO 20 or configured RX pin)`
- `CPX RX (Pad A6 / D0)` $\leftarrow$ `ESP32-C3 TX (GPIO 21 or configured TX pin)`
- `CPX GND` $\leftrightarrow$ `ESP32-C3 GND`

---

## 2. SOLID Architecture

Both firmware projects strictly separate **Hardware Abstraction**, **Domain Logic**, and **Communication**:

1. **Single Responsibility Principle (SRP)**
   - `ISensorProvider` (`CpxSensorProvider`): Only samples raw temperature (°C), light (lux/raw), and sound pressure level (dB).
   - `IComfortEvaluator` (`WeightedComfortEvaluator`): Pure domain class that scores room comfort (`0..100`) and noise alert state from `SensorReading` + `ComfortThresholds`.
   - `ILedVisualizer` (`CpxNeoPixelVisualizer`): Renders comfort level (`0..10` LEDs + color gradient from cozy emerald/warm amber to alert crimson) without knowing how sensors work.
   - `IAudioAlert` (`CpxSpeakerAlert`): Plays non-blocking warning chirps when noise exceeds threshold (respecting the physical slide switch / mute setting).
   - `IUartLink` (`SerialProtocolLink`): Encodes/decodes protocol frames between `cpx_node` and `c3_gateway`.

2. **Open/Closed Principle (OCP) & Sandbox Extensibility**
   - Behaviors implement `IGadgetMode` (`update()`, `onCommand()`, `name()`).
   - **MVP ModeImplemented**: `ComfynessMode` (balances temperature, light, and noise onto the 10 RGB LEDs + beeps when too noisy).
   - **Placeholder Sandbox Modes** (ready to implement without modifying existing classes):
     - `SoundPartyMode`: Real-time VU meter / spectrum ring on the 10 NeoPixels.
     - `PomodoroFocusMode`: Visual countdown ring using the 10 NeoPixels + gentle speaker chime on completion.
     - `NightLightMode`: Ambient auto-dimming warm lamp controlled from the ESP32-C3 web UI.

3. **Dependency Inversion Principle (DIP)**
   - High-level controllers (`GadgetOrchestrator` on CPX, `GatewayController` on ESP32-C3) depend strictly on abstract C++ interfaces (`include/interfaces/*.hpp`), making unit testing or hardware swaps trivial.

---

## 3. UART Protocol Specification (`common/mcshal_protocol`)

Human-readable, newline-delimited frames at `115200 8N1` (easy to debug with any serial monitor):

- **CPX $\rightarrow$ C3 Telemetry (`T` frame, 2 Hz)**:
  `T,<temp_c>,<light>,<sound_db>,<comfort_score>,<noisy_flag>,<muted_flag>,<mode_id>\n`
  Example: `T,22.4,310,54.2,88,0,0,0`
- **C3 $\rightarrow$ CPX Command (`C` frame, on user action)**:
  `C,<max_sound_db>,<ideal_temp_c>,<mute>,<mode_id>\n`
  Example: `C,72.0,22.5,0,0`

---

## 4. Phased Implementation Roadmap

- [x] **Phase 1 — Monorepo & Shared Protocol**: `MCsHAL` repo, `common/mcshal_protocol`, `cpx_node`, and `c3_gateway` PlatformIO projects.
- [x] **Phase 2 — CPX Comfyness MVP (`cpx_node`)**:
  - Hardware HAL for CPX Temp, Light, PDM Mic, 10x NeoPixels, Speaker, and Slide Switch (hardware mute).
  - `WeightedComfortEvaluator` + `ComfynessMode` + `Serial1` UART telemetry emitter.
  - Extensible `IGadgetMode` placeholders (`SoundPartyMode`, `PomodoroFocusMode`).
- [x] **Phase 3 — ESP32-C3 Web Gateway (`c3_gateway`)**:
  - Non-blocking UART parser receiving `T` frames and sending `C` frames.
  - Wi-Fi Manager (Station + fallback SoftAP `MCsHAL-Gateway`).
  - Live Web Dashboard showing temperature, light, noise dB, comfort gauge, and threshold/mode controls.
- [ ] **Phase 4 — Future Sandbox Modes**:
  - Fill in `SoundPartyMode` and `PomodoroFocusMode` implementations.
  - Add historical comfort graphing or MQTT/Home Assistant publishing on `c3_gateway`.
