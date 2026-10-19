#pragma once

#include <Adafruit_CircuitPlayground.h>
#include "interfaces/i_gadget_mode.hpp"
#include "interfaces/i_gadget_state.hpp"
#include "modes/sandbox_modes.hpp"

namespace mcshal {

// Active: the costume stays lit (default CostumeMode renderer; the web UI or
// the left button can switch to the sandbox renderers). Handles IR remote
// frames, buttons, full sensor telemetry at 2 Hz. Returns to Idle after the
// configured inactivity timeout once the room goes dark.
class ActiveState final : public IGadgetState {
public:
    // modes: ordered renderer list; index 0 should be the CostumeMode.
    ActiveState(IGadgetMode *const *modes, size_t mode_count,
                const CostumePalette &costume_ref)
        : modes_(modes), mode_count_(mode_count), costume_ref_(costume_ref) {}

    GadgetStateId id() const override { return GadgetStateId::Active; }

    void onEnter(GadgetContext &ctx) override {
        ctx.telemetry_period_ms = 500;
        ctx.notifyActivity(millis());
        if (ctx.mode_pending) {
            ctx.mode_pending = false;
            selectMode(ctx.requested_mode);
        } else {
            selectMode(GadgetModeId::Costume);
        }
        ctx.frame.state = id();
    }

    void tick(GadgetContext &ctx, uint32_t now_ms) override {
        IrKeyId key;
        if (ctx.takePendingIr(key)) {
            ctx.ir.emit(key);
            ctx.notifyActivity(now_ms);
            anim_flash_ticks_ = 2; // brief white ring flash as IR ack
        }

        if (ctx.mode_pending) {
            ctx.mode_pending = false;
            selectMode(ctx.requested_mode);
            ctx.notifyActivity(now_ms);
        }

        if (ctx.left_edge) {
            uint8_t next = (static_cast<uint8_t>(render_mode_->id()) + 1) % mode_count_;
            selectMode(static_cast<GadgetModeId>(next));
            ctx.notifyActivity(now_ms);
        }
        if (ctx.right_edge) {
            ctx.audio.playSynthShowcase(); // 4s ADPCM record/replay sandbox
            ctx.notifyActivity(now_ms);
        }

        if (now_ms - last_sample_ms_ >= 120) {
            last_sample_ms_ = now_ms;
            SensorReading reading = ctx.sensors.sample();
            last_light_ = reading.light_level;
            ctx.frame = render_mode_->update(now_ms, reading, ctx.thresholds);
            ctx.frame.state = id();
            if (anim_flash_ticks_) {
                anim_flash_ticks_--;
                for (uint8_t i = 0; i < 10; ++i) {
                    CircuitPlayground.strip.setPixelColor(i, 255, 255, 255);
                }
                CircuitPlayground.strip.show();
            }
        }

        uint32_t idle_ms = static_cast<uint32_t>(ctx.policy.idle_timeout_min) * 60000UL;
        bool inactivity_expired = (now_ms - ctx.last_activity_ms) > idle_ms;
        bool room_dark = last_light_ < ctx.policy.light_on_threshold;
        if (inactivity_expired && room_dark) {
            ctx.requestState(GadgetStateId::Idle);
        }
    }

    void onExit(GadgetContext &) override {}

private:
    void selectMode(GadgetModeId target) {
        for (size_t i = 0; i < mode_count_; ++i) {
            if (modes_[i]->id() == target) {
                render_mode_ = modes_[i];
                render_mode_->onEnter();
                return;
            }
        }
        render_mode_ = modes_[0];
        render_mode_->onEnter();
    }

    IGadgetMode *const *modes_;
    size_t mode_count_;
    const CostumePalette &costume_ref_;
    IGadgetMode *render_mode_{nullptr};
    uint32_t last_sample_ms_{0};
    uint16_t last_light_{0};
    uint8_t anim_flash_ticks_{0};
};

} // namespace mcshal
