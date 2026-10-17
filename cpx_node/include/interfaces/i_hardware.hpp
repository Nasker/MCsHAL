#pragma once

#include <stdint.h>
#include "mcshal_protocol.hpp"

namespace mcshal {

class ISensorProvider {
public:
    virtual ~ISensorProvider() = default;
    virtual SensorReading sample() = 0;
    virtual void sampleFft(uint16_t (&spectrum)[32]) = 0;
};

class ILedVisualizer {
public:
    virtual ~ILedVisualizer() = default;
    virtual void renderComfortScore(uint8_t score, bool is_noisy) = 0;
    virtual void renderSolidRing(uint8_t r, uint8_t g, uint8_t b, uint8_t active_pixels = 10) = 0;
    virtual void renderRainbowWheel(uint8_t phase_offset) = 0;
    virtual void renderFftSpectrum(const uint16_t (&spectrum)[32], uint8_t phase_offset) = 0;
    virtual void clear() = 0;
};

class IAudioAlert {
public:
    virtual ~IAudioAlert() = default;
    virtual void playBootGreeting() = 0;
    virtual void playNoiseWarning(uint32_t now_ms) = 0;
    virtual void playSynthShowcase() = 0;
};

class ITelemetryGateway {
public:
    virtual ~ITelemetryGateway() = default;
    virtual void publish(const TelemetryFrame &frame) = 0;
    virtual bool pollCommand(ConfigCommand &out_cmd) = 0;
};

} // namespace mcshal
