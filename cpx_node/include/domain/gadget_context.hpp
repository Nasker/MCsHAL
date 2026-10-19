#pragma once

#include <stdint.h>
#include "domain/melody_store.hpp"
#include "interfaces/i_hardware.hpp"
#include "mcshal_protocol.hpp"

namespace mcshal {

// Mutable context shared between the orchestrator and the gadget states.
// States read hardware through the interface ports and signal transitions
// through requestState(); the orchestrator performs the switch centrally.
struct GadgetContext {
    ISensorProvider &sensors;
    ILedVisualizer &leds;
    IAudioAlert &audio;
    ITelemetryGateway &gateway;
    IIrEmitter &ir;

    // Policy/config pushed by the C3 gateway (sane standalone defaults)
    ComfortThresholds thresholds{};
    WakePolicy policy{};
    ClockSync clock{};
    CostumePalette costume{};
    MelodyStore melody{};

    // Runtime
    TelemetryFrame frame{};
    GadgetModeId render_mode{GadgetModeId::Costume};
    GadgetModeId requested_mode{GadgetModeId::Costume};
    bool mode_pending{false};
    int8_t pending_ir{-1};
    bool left_edge{false};
    bool right_edge{false};
    uint32_t last_activity_ms{0};
    uint32_t telemetry_period_ms{500};

    GadgetContext(ISensorProvider &s, ILedVisualizer &l, IAudioAlert &a,
                  ITelemetryGateway &g, IIrEmitter &e)
        : sensors(s), leds(l), audio(a), gateway(g), ir(e) {}

    void requestState(GadgetStateId s) {
        request_state = s;
        state_requested_ = true;
    }
    bool takeStateRequest(GadgetStateId &out) {
        if (!state_requested_) return false;
        state_requested_ = false;
        out = request_state;
        return true;
    }
    void notifyActivity(uint32_t now_ms) { last_activity_ms = now_ms; }
    bool takePendingIr(IrKeyId &out) {
        if (pending_ir < 0) return false;
        out = static_cast<IrKeyId>(pending_ir);
        pending_ir = -1;
        return true;
    }

    // Standalone fallback: no C3 clock -> always inside the wake window.
    bool dayWindowActive() const {
        return clock_received_ ? clock.day_window_active : true;
    }
    void applyClock(const ClockSync &c) {
        clock = c;
        clock_received_ = true;
    }

    GadgetStateId request_state{GadgetStateId::Idle};
    bool state_requested_{false};
    bool clock_received_{false};
};

} // namespace mcshal
