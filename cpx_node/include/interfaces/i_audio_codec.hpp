#pragma once

#include <stdint.h>

namespace mcshal {

class IAudioCodec {
public:
    virtual ~IAudioCodec() = default;
    virtual void reset() = 0;
    virtual uint8_t bitsPerSample() const = 0;
    virtual uint8_t encodeSample(int16_t pcm_sample) = 0;
    virtual int16_t decodeSample(uint8_t encoded_code) = 0;
};

} // namespace mcshal
