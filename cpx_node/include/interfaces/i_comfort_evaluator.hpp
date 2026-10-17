#pragma once

#include "mcshal_protocol.hpp"

namespace mcshal {

class IComfortEvaluator {
public:
    virtual ~IComfortEvaluator() = default;
    virtual ComfortReport evaluate(const SensorReading &reading,
                                   const ComfortThresholds &thresholds) const = 0;
};

} // namespace mcshal
