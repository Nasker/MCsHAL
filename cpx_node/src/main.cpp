#include <Adafruit_CircuitPlayground.h>
#include <Arduino.h>

#include "domain/gadget_context.hpp"
#include "domain/ima_adpcm_codec.hpp"
#include "domain/voice_recorder_service.hpp"
#include "domain/weighted_comfort_evaluator.hpp"
#include "hal/cpx_hal.hpp"
#include "hal/cpx_ir_emitter.hpp"
#include "hal/spi_flash_audio_storage.hpp"
#include "hal/sram_audio_storage.hpp"
#include "modes/comfyness_mode.hpp"
#include "modes/costume_mode.hpp"
#include "modes/sandbox_modes.hpp"
#include "states/active_state.hpp"
#include "states/greeting_state.hpp"
#include "states/idle_state.hpp"

namespace mcshal {

// Orchestrates the gadget lifecycle: drains inbound UART frames, feeds button
// edges into the context, ticks the active state, performs state transitions,
// and publishes telemetry at the cadence the state asks for.
class GadgetOrchestrator {
public:
    GadgetOrchestrator(GadgetContext &ctx, IGadgetState *const *states, size_t state_count)
        : ctx_(ctx), states_(states), state_count_(state_count) {}

    void begin() {
        for (uint8_t step = 0; step < 32; ++step) {
            ctx_.leds.renderRainbowWheel(static_cast<uint8_t>(step * 8));
            delay(20);
        }
        ctx_.audio.playBootGreeting();
        transitionTo(GadgetStateId::Idle);
    }

    void tick(uint32_t now_ms) {
        dispatchFrames(now_ms);
        pollButtons(now_ms);

        GadgetStateId req;
        if (ctx_.takeStateRequest(req)) {
            transitionTo(req);
        }

        current_->tick(ctx_, now_ms);
        ctx_.left_edge = false;
        ctx_.right_edge = false;

        if (ctx_.takeStateRequest(req)) {
            transitionTo(req);
        }

        if (now_ms - last_telemetry_ms_ >= ctx_.telemetry_period_ms) {
            last_telemetry_ms_ = now_ms;
            ctx_.frame.state = current_->id();
            ctx_.gateway.publish(ctx_.frame);
        }
    }

private:
    void transitionTo(GadgetStateId target) {
        for (size_t i = 0; i < state_count_; ++i) {
            if (states_[i]->id() == target && states_[i] != current_) {
                if (current_) current_->onExit(ctx_);
                current_ = states_[i];
                current_->onEnter(ctx_);
                return;
            }
        }
    }

    void dispatchFrames(uint32_t now_ms) {
        char line[200];
        while (ctx_.gateway.pollLine(line, sizeof(line))) {
            switch (line[0]) {
                case 'C': {
                    ConfigCommand cmd;
                    if (ProtocolCodec::decodeCommand(line, cmd)) {
                        ctx_.thresholds.max_sound_db = cmd.max_sound_db;
                        ctx_.thresholds.ideal_temp_c = cmd.ideal_temp_c;
                        ctx_.thresholds.muted = cmd.muted;
                        ctx_.requested_mode = cmd.mode;
                        ctx_.mode_pending = true;
                        ctx_.notifyActivity(now_ms);
                    }
                    break;
                }
                case 'D': {
                    ClockSync clk;
                    if (ProtocolCodec::decodeClock(line, clk)) ctx_.applyClock(clk);
                    break;
                }
                case 'W': {
                    WakePolicy w;
                    if (ProtocolCodec::decodePolicy(line, w)) ctx_.policy = w;
                    break;
                }
                case 'K': {
                    CostumePalette k;
                    if (ProtocolCodec::decodeCostume(line, k)) ctx_.costume = k;
                    break;
                }
                case 'M': {
                    MelodyChunk m;
                    if (ProtocolCodec::decodeMelodyChunk(line, m)) ctx_.melody.appendChunk(m);
                    break;
                }
                case 'R': {
                    IrKeyId key;
                    if (ProtocolCodec::decodeRemote(line, key)) {
                        ctx_.pending_ir = static_cast<int8_t>(key);
                        ctx_.notifyActivity(now_ms);
                    }
                    break;
                }
                case 'G': { // 'G' alone = force the greeting show (web /api/greet)
                    ctx_.requestState(GadgetStateId::Greeting);
                    ctx_.notifyActivity(now_ms);
                    break;
                }
                default:
                    break;
            }
        }
    }

    void pollButtons(uint32_t now_ms) {
        bool left = CircuitPlayground.leftButton();
        bool right = CircuitPlayground.rightButton();
        if (left && !prev_left_) {
            ctx_.left_edge = true;
            ctx_.notifyActivity(now_ms);
        }
        if (right && !prev_right_) {
            ctx_.right_edge = true;
            ctx_.notifyActivity(now_ms);
        }
        prev_left_ = left;
        prev_right_ = right;
    }

    GadgetContext &ctx_;
    IGadgetState *const *states_;
    size_t state_count_;
    IGadgetState *current_{nullptr};
    uint32_t last_telemetry_ms_{0};
    bool prev_left_{false};
    bool prev_right_{false};
};

} // namespace mcshal

// SOLID Audio Subsystem Instances:
// - 4-bit IMA ADPCM Codec (2 samples per byte)
// - 16 KB Volatile SRAM Primary Storage (~4.5s capacity at 7,333 Hz ADPCM)
// - 2 MB Onboard GD25Q16C SPI Flash Persistent Storage (~9.5 min capacity, survives power-off)
static mcshal::ImaAdpcmCodec g_adpcm_codec;
static mcshal::SramAudioStorage<16384> g_sram_storage;
static mcshal::SpiFlashAudioStorage g_flash_storage;
static mcshal::VoiceRecorderService g_voice_recorder(
    g_adpcm_codec, g_sram_storage, &g_flash_storage);

static mcshal::CpxSensorProvider g_sensors;
static mcshal::CpxNeoPixelVisualizer g_visualizer;
static mcshal::CpxSpeakerAlert g_speaker(g_voice_recorder);
static mcshal::CpxSerialGateway g_gateway(Serial1);
static mcshal::CpxIrEmitter g_ir_emitter;
static mcshal::WeightedComfortEvaluator g_evaluator;

static mcshal::GadgetContext g_ctx(
    g_sensors, g_visualizer, g_speaker, g_gateway, g_ir_emitter);

// Active-state renderers: index 0 is the seasonal costume default.
static mcshal::CostumeMode g_costume_mode(g_visualizer, g_ctx.costume);
static mcshal::ComfynessMode g_comfy_mode(g_evaluator, g_visualizer, g_speaker);
static mcshal::SoundPartyMode g_party_mode(g_sensors, g_visualizer);
static mcshal::PomodoroFocusMode g_pomodoro_mode(g_visualizer);

static mcshal::IGadgetMode *const g_modes[] = {
    &g_costume_mode,
    &g_comfy_mode,
    &g_party_mode,
    &g_pomodoro_mode,
};

static mcshal::IdleState g_idle;
static mcshal::GreetingState g_greeting;
static mcshal::ActiveState g_active(g_modes, sizeof(g_modes) / sizeof(g_modes[0]),
                                    g_ctx.costume);

static mcshal::IGadgetState *const g_states[] = {
    &g_idle,
    &g_greeting,
    &g_active,
};

static mcshal::GadgetOrchestrator g_orchestrator(
    g_ctx, g_states, sizeof(g_states) / sizeof(g_states[0]));

void setup() {
    // Liveness probe: raw-toggling pin 1 (UART TX pad) before ANY peripheral
    // setup — the C3's pin-transition counter sees it even if UART init hangs.
    pinMode(1, OUTPUT);
    for (int i = 0; i < 20; ++i) {
        digitalWrite(1, i & 1);
        delay(40);
    }
    // NOTE: no Serial (USB CDC) writes — TinyUSB writes can block boot when no
    // host is attached. Bring the C3 link up FIRST and emit stage markers on it;
    // the C3's raw-byte counter + last-line diag show where boot stalls.
    g_gateway.begin(115200);
    Serial1.print(F("X,S0\n"));
    Serial.begin(115200);
    CircuitPlayground.begin(40);
    Serial1.print(F("X,S1\n"));
    // Immediately shut down the Class-D speaker amp that CircuitPlayground.begin() enabled
    g_speaker.begin();
    g_voice_recorder.begin();
    Serial1.print(F("X,S2\n"));
    g_orchestrator.begin();
    Serial1.print(F("X,S3\n")); // reached loop()
}

void loop() {
    uint32_t now = millis();
    g_orchestrator.tick(now);
    g_ir_emitter.tick(now); // D13 ack flash timer
}
