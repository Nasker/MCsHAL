#pragma once

#include "interfaces/i_comfort_evaluator.hpp"
#include "interfaces/i_gadget_mode.hpp"
#include "interfaces/i_hardware.hpp"

namespace mcshal {

class ComfynessMode final : public IGadgetMode {
public:
    ComfynessMode(const IComfortEvaluator &evaluator,
                  ILedVisualizer &visualizer,
                  IAudioAlert &audio)
        : evaluator_(evaluator), visualizer_(visualizer), audio_(audio) {}

    GadgetModeId id() const override { return GadgetModeId::Comfyness; }

    void onEnter() override { visualizer_.clear(); }

    TelemetryFrame update(uint32_t now_ms,
                          const SensorReading &reading,
                          const ComfortThresholds &thresholds) override {
        ComfortReport report = evaluator_.evaluate(reading, thresholds);
        visualizer_.renderComfortScore(report.score, report.is_too_noisy);

        bool muted = thresholds.muted || reading.mute_switch;
        if (report.is_too_noisy && !muted) {
            audio_.playNoiseWarning(now_ms);
        }

        TelemetryFrame frame;
        frame.temperature_c = reading.temperature_c;
        frame.light_level = reading.light_level;
        frame.sound_db = reading.sound_db;
        frame.comfort_score = report.score;
        frame.is_too_noisy = report.is_too_noisy;
        frame.is_muted = muted;
        frame.mode = id();
        return frame;
    }

private:
    const IComfortEvaluator &evaluator_;
    ILedVisualizer &visualizer_;
    IAudioAlert &audio_;
};

} // namespace mcshal
