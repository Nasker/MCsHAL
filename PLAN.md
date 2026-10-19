# MCsHAL — Multi-Controller Living Room Sandbox Plan

## 1. System Overview & Hardware Roles

`MCsHAL` is a dual-microcontroller monorepo combining deterministic local sensing/actuation with wireless connectivity:

| Node | MCU / Board | Responsibilities |
| :--- | :--- | :--- |
| **`cpx_node`** | Adafruit Circuit Playground Express (`ATSAMD21G18A @ 48MHz`) | Reads ambient **Temperature**, **Light**, and **Sound (SPL dB)**; runs active `IGadgetMode` (MVP: *Comfyness Monitor*); drives the **10x NeoPixel ring** and **onboard speaker**; streams telemetry frames and receives configuration commands over `Serial1` (`RX=D0/A6`, `TX=D1/A7`). |
| **`c3_gateway`** | ESP32-C3 (`esp32-c3-devkitm-1`) | Connects to `cpx_node` via UART; hosts the **Wi-Fi HTTP + API server**; serves a real-time living-room dashboard displaying comfort metrics and allowing remote tuning of comfort thresholds, mute/unmute, and active gadget mode selection. |
| **`common/mcshal_protocol`** | Shared Header-Only C++ Library | Defines the zero-allocation line-based UART protocol (`TelemetryFrame`, `ConfigCommand`, serializer/parser) shared by both nodes. |

### Physical Wiring (UART Bridge) — VERIFIED ON HARDWARE
Both boards run at **3.3V logic**, so no level shifter is required.
Note: the ESP32-C3 SuperMini's silkscreen labels **GPIO21 = RXD0** and **GPIO20 = TXD0**:
- `CPX TX (Pad A7 / D1)` $\rightarrow$ `ESP32-C3 RX (GPIO 21)`
- `CPX RX (Pad A6 / D0)` $\leftarrow$ `ESP32-C3 TX (GPIO 20)`
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
- [x] **Phase 3 — ESP32-C3 Web Gateway (`c3_gateway`) — VERIFIED END-TO-END**:
  - Non-blocking UART parser receiving `T` frames and sending `C` frames (verified bidirectionally: `POST /api/config?mode=2&muted=1` applied on the CPX and echoed in telemetry).
  - Wi-Fi Manager: Station with bounded 20 s attempt + clean SoftAP `MCsHAL-Gateway` fallback (`http://192.168.4.1/`); NVS credentials saved from the web UI; reached home LAN at `http://192.168.1.111/`.
  - Live Web Dashboard showing temperature, light, noise dB, comfort gauge, and threshold/mode controls.
- [x] **Phase 3.5 — CPX Audio & Storage Subsystem (`cpx_node`)**:
  - `IAudioCodec` (`ImaAdpcmCodec` 4-bit, `Raw8BitPcmCodec`) + `IAudioStorage` (`SramAudioStorage` 16 KB, `SpiFlashAudioStorage` onboard 2 MB GD25Q16C).
  - `VoiceRecorderService`: hold D5 to record 4 s ADPCM clip (persisted to SPI flash, survives power-off); tap D5 to replay.
  - Class-D speaker amp held in shutdown (`D11` LOW) except during active audio output.
  - 10-bit DAC synth sweeps/arpeggio + LPC speech (`say10Bit`, 4x pre-gain); live mic FFT ring in `SoundPartyMode`.
- [ ] **Phase 4 — Future Directions**:
  - Voice command recognition: stream mic audio/FFT slices over UART to an ESP32-S3 (or PC via the C3) running TFLite Micro / Edge Impulse keyword spotting.
  - Add historical comfort graphing or MQTT/Home Assistant publishing on `c3_gateway`.

---

## 5. Phase 5 — Living-Room TV Companion (State Pattern)

### 5.1 Lifecycle States (`IGadgetState`, CPX)
`GadgetOrchestrator` owns a state context; each state implements `onEnter()`, `tick(now)`, `onExit()`, `name()`:

| State | Behaviour | Exit condition |
| :--- | :--- | :--- |
| **Idle** | Amp off, NeoPixels off, mic off. Light sensor sampled every ~10 s only; CPU dozes between samples; heartbeat telemetry every ~10 s. | Light >= `light_on_threshold` while `day_window` active -> Greeting. |
| **Greeting** | Plays the season melody on the DAC (amp wakes for the tune) and runs the costume LED animation. | Melody finished -> Active. |
| **Active** | Costume LED pattern stays rendered (default `CostumeMode`; `Comfyness/Party/Pomodoro` still selectable as renderers). Full 2 Hz telemetry. IR remote commands emitted on demand. | No activity for `idle_timeout_min` **and** light below threshold -> Idle. |

### 5.2 Division of Labour
- **C3 = policy + storage + network**: NTP clock (POSIX TZ), day-window evaluation, season->theme resolution from JSON, web UI, pushes compact wire frames to the CPX.
- **CPX = deterministic render/actuation**: light sensing, state machine, LED/DAC/IR outputs. No JSON parsing on the 32 KB SAMD21 — themes arrive as compact `K`/`M` frames.

### 5.3 Extended Wire Protocol (`mcshal_protocol.hpp`)
- `T,...,<state>` — telemetry gains `state` field (0=Idle,1=Greeting,2=Active).
- `D,<minute_of_day>,<day_window_active>` — clock/policy sync (~60 s cadence + on link-up).
- `W,<light_on_lux>,<day_start_min>,<day_end_min>,<idle_timeout_min>` — wake policy config.
- `K,<topR>,<topG>,<topB>,<bodyR>,<bodyG>,<bodyB>,<anim>` — costume palette+animation.
- `M,<total>,<i>,<f>,<d>[,<f>,<d>...]` — melody chunks (`f`=Hz, `d`=ms, `i`=note offset).
- `R,<key>` — IR remote command (0=POWER,1=UP,2=DOWN,3=LEFT,4=RIGHT,5=OK,6=BACK) -> CPX emits LG NEC on onboard IR LED (pin 25).

### 5.4 Theme JSON (LittleFS `/themes/*.json` on C3, editable via web UI)
```json
{
  "name": "halloween", "label": "Halloween Pumpkin",
  "active": {"from": "10-15", "to": "11-05"},
  "costume": {
    "pixels": [{"range": "0", "color": "#18FF00"}, {"range": "1-9", "color": "#FF7A00"}],
    "animation": "pulse", "anim_speed": 5
  },
  "melody": [[659,300],[622,300],[659,300],[622,300],[659,300],[494,300],[587,300],[523,300],[440,700]]
}
```
C3 picks the first theme whose `from..to` (MM-DD, wraps across year end) contains today's date; fallback `default.json` / built-in.

### 5.5 Web UI additions
- TV remote pad (Power, arrows, OK, Back) -> `POST /api/tv?key=N`.
- `POST /api/greet` force-triggers the greeting; `/api/themes` list, `GET/POST /api/theme?name=x` JSON editor.
- Config form gains light threshold, day start/end, idle timeout (-> `W` frame).

### 5.6 Power Budget
- Idle: NeoPixels 0 mA, amp <1 uA, mic/PDM off, CPU idles between 10 s samples; UART down to a 10 s heartbeat.
- C3: `WIFI_PS_MIN_MODEM` modem sleep; web server stays live.
- Known limit: onboard IR LED is short-range (board-to-board by design); an external IR LED + transistor on a free pad is the upgrade path for couch-to-TV distance.
