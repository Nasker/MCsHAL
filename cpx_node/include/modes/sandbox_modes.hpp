#pragma once

#include "interfaces/i_gadget_mode.hpp"
#include "interfaces/i_hardware.hpp"

namespace mcshal {

// Sandbox Mode 1: Real-time 32-bin FFT Spectrum + Rainbow NeoPixel Ring
class SoundPartyMode final : public IGadgetMode {
public:
    SoundPartyMode(ISensorProvider &sensors, ILedVisualizer &visualizer)
        : sensors_(sensors), visualizer_(visualizer) {}

    GadgetModeId id() const override { return GadgetModeId::SoundParty; }

    void onEnter() override { visualizer_.clear(); }

    TelemetryFrame update(uint32_t /*now_ms*/,
                          const SensorReading &reading,
                          const ComfortThresholds &thresholds) override {
        uint16_t spectrum[32] = {};
        sensors_.sampleFft(spectrum);
        phase_ += 7;
        visualizer_.renderFftSpectrum(spectrum, phase_);

        TelemetryFrame f;
        f.temperature_c = reading.temperature_c;
        f.light_level = reading.light_level;
        f.sound_db = reading.sound_db;
        f.comfort_score = 100;
        f.is_too_noisy = false;
        f.is_muted = thresholds.muted || reading.mute_switch;
        f.mode = id();
        return f;
    }

private:
    ISensorProvider &sensors_;
    ILedVisualizer &visualizer_;
    uint8_t phase_{0};
};

// Sandbox Mode 2: Pomodoro Focus Timer Ring
class PomodoroFocusMode final : public IGadgetMode {
public:
    explicit PomodoroFocusMode(ILedVisualizer &visualizer) : visualizer_(visualizer) {}

    GadgetModeId id() const override { return GadgetModeId::PomodoroFocus; }

    void onEnter() override { visualizer_.clear(); }

    TelemetryFrame update(uint32_t /*now_ms*/,
                          const SensorReading &reading,
                          const ComfortThresholds &thresholds) override {
        phase_ += 3;
        visualizer_.renderRainbowWheel(phase_);

        TelemetryFrame f;
        f.temperature_c = reading.temperature_c;
        f.light_level = reading.light_level;
        f.sound_db = reading.sound_db;
        f.comfort_score = 100;
        f.is_too_noisy = false;
        f.is_muted = thresholds.muted || reading.mute_switch;
        f.mode = id();
        return f;
    }

private:
    ILedVisualizer &visualizer_;
    uint8_t phase_{0};
};

} // namespace mcshal
