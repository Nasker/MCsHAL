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
};

} // namespace mcshal
