#pragma once

#include "interfaces/i_gadget_mode.hpp"
#include "interfaces/i_hardware.hpp"

namespace mcshal {

// Renderer used by ActiveState: keeps the seasonal costume lit with its
// configured animation (pulse / sparkle / spin / static). The palette is a
// live reference into GadgetContext so a new 'K' frame re-skins it instantly.
class CostumeMode final : public IGadgetMode {
public:
    CostumeMode(ILedVisualizer &visualizer, const CostumePalette &palette)
        : visualizer_(visualizer), palette_(palette) {}

    GadgetModeId id() const override { return GadgetModeId::Costume; }

    void onEnter() override {}

    TelemetryFrame update(uint32_t /*now_ms*/,
                          const SensorReading &reading,
                          const ComfortThresholds &thresholds) override {
        phase_ += palette_.anim_speed;
        visualizer_.renderCostume(palette_, phase_);

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
    const CostumePalette &palette_;
    uint8_t phase_{0};
};

} // namespace mcshal
