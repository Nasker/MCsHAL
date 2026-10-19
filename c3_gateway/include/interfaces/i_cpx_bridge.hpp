#pragma once

#include <stdint.h>
#include "mcshal_protocol.hpp"

namespace mcshal {

class ICpxBridge {
public:
    virtual ~ICpxBridge() = default;
    virtual void poll(uint32_t now_ms) = 0;
    virtual TelemetryFrame latestTelemetry() const = 0;
    virtual uint32_t lastFrameAgeMs(uint32_t now_ms) const = 0;
    virtual void sendCommand(const ConfigCommand &cmd) = 0;
    virtual void sendClock(const ClockSync &clk) = 0;
    virtual void sendPolicy(const WakePolicy &w) = 0;
    virtual void sendCostume(const CostumePalette &k) = 0;
    virtual void sendMelodyChunk(const MelodyChunk &m) = 0;
    virtual void sendRemote(IrKeyId key) = 0;
    virtual void sendGreet() = 0; // bare 'G' frame: force the greeting show
};

} // namespace mcshal
