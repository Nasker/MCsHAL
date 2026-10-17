#pragma once

#include <math.h>
#include "interfaces/i_comfort_evaluator.hpp"

namespace mcshal {

class WeightedComfortEvaluator final : public IComfortEvaluator {
public:
    ComfortReport evaluate(const SensorReading &r,
                           const ComfortThresholds &t) const override {
        // 1. Temperature penalty (0..40 pts)
        float temp_diff = fabsf(r.temperature_c - t.ideal_temp_c);
        float temp_penalty = (temp_diff / t.temp_tolerance_c) * 40.0f;
        if (temp_penalty > 40.0f) temp_penalty = 40.0f;

        // 2. Light penalty (0..25 pts)
        float light_penalty = 0.0f;
        if (r.light_level < t.min_comfy_light) {
            light_penalty = static_cast<float>(t.min_comfy_light - r.light_level) /
                            static_cast<float>(t.min_comfy_light) * 20.0f;
        } else if (r.light_level > t.max_comfy_light) {
            light_penalty = static_cast<float>(r.light_level - t.max_comfy_light) / 224.0f * 25.0f;
        }
        if (light_penalty > 25.0f) light_penalty = 25.0f;

        // 3. Sound penalty (0..35 pts)
        float sound_penalty = 0.0f;
        const float quiet_baseline_db = 48.0f;
        if (r.sound_db > quiet_baseline_db) {
            float span = (t.max_sound_db > quiet_baseline_db) ? (t.max_sound_db - quiet_baseline_db) : 20.0f;
            sound_penalty = ((r.sound_db - quiet_baseline_db) / span) * 35.0f;
        }
        if (sound_penalty > 45.0f) sound_penalty = 45.0f;

        float raw_score = 100.0f - (temp_penalty + light_penalty + sound_penalty);
        if (raw_score < 0.0f) raw_score = 0.0f;
        if (raw_score > 100.0f) raw_score = 100.0f;

        ComfortReport report;
        report.score = static_cast<uint8_t>(raw_score + 0.5f);
        report.is_too_noisy = (r.sound_db >= t.max_sound_db);
        return report;
    }
};

} // namespace mcshal
