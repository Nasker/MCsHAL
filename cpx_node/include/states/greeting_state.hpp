#pragma once

#include "interfaces/i_gadget_state.hpp"

namespace mcshal {

// Greeting: the amp stays on for the whole tune; each tick plays one note
// (blocking ~200-600 ms) and steps the costume animation between notes, so
// the ring visibly dances to the melody. Transitions to Active when done.
class GreetingState final : public IGadgetState {
public:
    GadgetStateId id() const override { return GadgetStateId::Greeting; }

    void onEnter(GadgetContext &ctx) override {
        ctx.telemetry_period_ms = 500;
        note_idx_ = 0;
        anim_phase_ = 0;
        ctx.notifyActivity(millis());
        if (!ctx.melody.complete()) {
            loadFallbackMelody(ctx);
        }
        ctx.audio.ampOn();
        ctx.frame.state = id();
    }

    void tick(GadgetContext &ctx, uint32_t /*now_ms*/) override {
        uint16_t total = ctx.melody.total();
        if (total == 0 || note_idx_ >= total) {
            ctx.requestState(GadgetStateId::Active);
            return;
        }
        // Animate one beat, then sound the note (blocks for its duration)
        anim_phase_ += 2;
        ctx.leds.renderCostume(ctx.costume, anim_phase_);
        ctx.audio.playTone(ctx.melody.freqs()[note_idx_], ctx.melody.durs()[note_idx_]);
        note_idx_++;
        ctx.frame.state = id();
    }

    void onExit(GadgetContext &ctx) override { ctx.audio.ampOff(); }

private:
    // Built-in fallback so the gadget still greets when standalone (no C3 theme).
    static void loadFallbackMelody(GadgetContext &ctx) {
        static const uint16_t kFreq[] = {523, 659, 784, 1046};
        static const uint16_t kDur[] = {140, 140, 140, 320};
        MelodyChunk chunk;
        chunk.total = 4;
        chunk.index = 0;
        chunk.count = 4;
        for (uint8_t i = 0; i < 4; ++i) {
            chunk.freq_hz[i] = kFreq[i];
            chunk.dur_ms[i] = kDur[i];
        }
        ctx.melody.appendChunk(chunk);
        for (uint8_t i = 0; i < 10; ++i) {
            ctx.costume.px[i][0] = 20;
            ctx.costume.px[i][1] = 120;
            ctx.costume.px[i][2] = 220;
        }
        ctx.costume.anim = CostumeAnim::Pulse;
        ctx.costume.anim_speed = 5;
    }

    uint16_t note_idx_{0};
    uint8_t anim_phase_{0};
};

} // namespace mcshal
