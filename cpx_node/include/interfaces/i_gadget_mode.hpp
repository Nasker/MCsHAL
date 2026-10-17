#pragma once

#include <stdint.h>
#include "mcshal_protocol.hpp"

namespace mcshal {

class IGadgetMode {
public:
    virtual ~IGadgetMode() = default;
    virtual GadgetModeId id() const = 0;
    virtual void onEnter() = 0;
    virtual TelemetryFrame update(uint32_t now_ms,
                                  const SensorReading &reading,
                                  const ComfortThresholds &thresholds) = 0;
};

} // namespace mcshal
