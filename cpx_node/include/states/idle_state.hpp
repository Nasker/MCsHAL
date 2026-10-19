#pragma once

#include <Adafruit_CircuitPlayground.h>
#include "interfaces/i_gadget_state.hpp"

namespace mcshal {

// Idle: amp off, NeoPixels off, mic off. Only the light sensor is polled
// every ~10 s; everything else naps. Wakes to Greeting on a low->high light
// edge inside the day window, or straight to Active on explicit user input
// (IR/remote/config frames or a physical button).
class IdleState final : public IGadgetState {
public:
    GadgetStateId id() const override { return GadgetStateId::Idle; }

    void onEnter(GadgetContext &ctx) override {
        ctx.leds.clear();
        ctx.telemetry_period_ms = 10000;
        lit_prev_ = false;
        last_sample_ms_ = 0;
    }

    void tick(GadgetContext &ctx, uint32_t now_ms) override {
        // Explicit user intent skips the greeting show entirely
        // (peek at pending_ir — ActiveState will consume and emit it)
        if (ctx.pending_ir >= 0 || ctx.mode_pending || ctx.left_edge || ctx.right_edge) {
            ctx.mode_pending = false;
            ctx.notifyActivity(now_ms);
            ctx.requestState(GadgetStateId::Active);
            return;
        }

        // NOTE: a plain loop here is intentional — __WFI() wedged the board on
        // this TinyUSB core even though SysTick is running. The real idle power
        // savings come from LEDs/amp/mic off + 10 s sampling cadence anyway.
        if (now_ms - last_sample_ms_ < 10000) {
            return;
        }
        last_sample_ms_ = now_ms;

        uint16_t light = ctx.sensors.sampleLight();
        bool lit = light >= ctx.policy.light_on_threshold;

        ctx.frame.temperature_c = ctx.sensors.sample().temperature_c;
        ctx.frame.light_level = light;
        ctx.frame.sound_db = 0;
        ctx.frame.comfort_score = 0;
        ctx.frame.is_too_noisy = false;
        ctx.frame.is_muted = ctx.thresholds.muted || !CircuitPlayground.slideSwitch();
        ctx.frame.state = id();

        if (lit && !lit_prev_ && ctx.dayWindowActive()) {
            ctx.requestState(GadgetStateId::Greeting);
        }
        lit_prev_ = lit;
    }

    void onExit(GadgetContext &) override {}

private:
    uint32_t last_sample_ms_{0};
    bool lit_prev_{false};
};

} // namespace mcshal
