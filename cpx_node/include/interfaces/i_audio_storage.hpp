#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mcshal {

struct AudioClipMetadata {
    uint32_t magic{0};
    uint32_t sample_rate_hz{7333};
    uint32_t sample_count{0};
    uint32_t byte_length{0};
    int16_t dc_offset{0};
    int16_t peak_amplitude{64};
    uint8_t bits_per_sample{4};
};

class IAudioStorage {
public:
    static constexpr uint32_t kValidMagic = 0x4D435341UL; // "MCSA"

    virtual ~IAudioStorage() = default;
    virtual bool begin() = 0;
    virtual bool isPersistent() const = 0;
    virtual size_t capacityBytes() const = 0;
    virtual bool prepareWrite(size_t required_bytes) = 0;
    virtual bool writeChunk(const uint8_t *data, size_t len) = 0;
    virtual bool commitWrite(const AudioClipMetadata &meta) = 0;
    virtual bool loadMetadata(AudioClipMetadata &out_meta) = 0;
    virtual bool readChunk(size_t byte_offset, uint8_t *out_data, size_t len) = 0;
};

} // namespace mcshal
