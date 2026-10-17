#include <Adafruit_CircuitPlayground.h>
#include <Arduino.h>

#include "domain/ima_adpcm_codec.hpp"
#include "domain/voice_recorder_service.hpp"
#include "domain/weighted_comfort_evaluator.hpp"
#include "hal/cpx_hal.hpp"
#include "hal/spi_flash_audio_storage.hpp"
#include "hal/sram_audio_storage.hpp"
#include "modes/comfyness_mode.hpp"
#include "modes/sandbox_modes.hpp"

namespace mcshal {

class GadgetOrchestrator {
public:
    GadgetOrchestrator(ISensorProvider &sensors,
                       ILedVisualizer &visualizer,
                       IAudioAlert &audio,
                       ITelemetryGateway &gateway,
                       IGadgetMode *const *modes,
                       size_t mode_count)
        : sensors_(sensors),
          visualizer_(visualizer),
          audio_(audio),
          gateway_(gateway),
          modes_(modes),
          mode_count_(mode_count),
          active_mode_(modes[0]) {}

    void begin() {
        for (uint8_t step = 0; step < 32; ++step) {
            visualizer_.renderRainbowWheel(static_cast<uint8_t>(step * 8));
            delay(20);
        }
        audio_.playBootGreeting();
        if (active_mode_) {
            active_mode_->onEnter();
        }
    }

    void tick(uint32_t now_ms) {
        ConfigCommand cmd;
        if (gateway_.pollCommand(cmd)) {
            thresholds_.max_sound_db = cmd.max_sound_db;
            thresholds_.ideal_temp_c = cmd.ideal_temp_c;
            thresholds_.muted = cmd.muted;
            switchMode(cmd.mode);
        }

        // Physical Left Button (D4) cycles gadget modes locally
        bool left_btn = CircuitPlayground.leftButton();
        if (left_btn && !prev_left_btn_) {
            uint8_t next_id = (static_cast<uint8_t>(active_mode_->id()) + 1) % mode_count_;
            switchMode(static_cast<GadgetModeId>(next_id));
        }
        prev_left_btn_ = left_btn;

        // Physical Right Button (D5):
        // - Hold >= 0.35s: Record 4s (4-bit ADPCM) to SRAM + 2MB SPI Flash, then replay
        // - Short tap: Replay persisted clip from SRAM or 2MB SPI Flash
        bool right_btn = CircuitPlayground.rightButton();
        if (right_btn && !prev_right_btn_) {
            audio_.playSynthShowcase();
        }
        prev_right_btn_ = right_btn;

        if (now_ms - last_sample_ms_ >= 120) {
            last_sample_ms_ = now_ms;
            SensorReading reading = sensors_.sample();
            last_frame_ = active_mode_->update(now_ms, reading, thresholds_);
        }

        if (now_ms - last_telemetry_ms_ >= 500) {
            last_telemetry_ms_ = now_ms;
            gateway_.publish(last_frame_);
        }
    }

private:
    void switchMode(GadgetModeId target) {
        for (size_t i = 0; i < mode_count_; ++i) {
            if (modes_[i]->id() == target && modes_[i] != active_mode_) {
                active_mode_ = modes_[i];
                active_mode_->onEnter();
                break;
            }
        }
    }

    ISensorProvider &sensors_;
    ILedVisualizer &visualizer_;
    IAudioAlert &audio_;
    ITelemetryGateway &gateway_;
    IGadgetMode *const *modes_;
    size_t mode_count_;
    IGadgetMode *active_mode_;
    ComfortThresholds thresholds_{};
    TelemetryFrame last_frame_{};
    uint32_t last_sample_ms_{0};
    uint32_t last_telemetry_ms_{0};
    bool prev_left_btn_{false};
    bool prev_right_btn_{false};
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
static mcshal::WeightedComfortEvaluator g_evaluator;

static mcshal::ComfynessMode g_comfy_mode(g_evaluator, g_visualizer, g_speaker);
static mcshal::SoundPartyMode g_party_mode(g_sensors, g_visualizer);
static mcshal::PomodoroFocusMode g_pomodoro_mode(g_visualizer);

static mcshal::IGadgetMode *const g_modes[] = {
    &g_comfy_mode,
    &g_party_mode,
    &g_pomodoro_mode,
};

static mcshal::GadgetOrchestrator g_orchestrator(
    g_sensors, g_visualizer, g_speaker, g_gateway, g_modes, sizeof(g_modes) / sizeof(g_modes[0]));

void setup() {
    Serial.begin(115200);
    CircuitPlayground.begin(40);
    // Immediately shut down the Class-D speaker amp that CircuitPlayground.begin() enabled
    g_speaker.begin();
    g_voice_recorder.begin();
    g_gateway.begin(115200);
    g_orchestrator.begin();
    Serial.print(F("BOOT OK\n")); // one-shot liveness marker on USB CDC
}

void loop() {
    g_orchestrator.tick(millis());
}
