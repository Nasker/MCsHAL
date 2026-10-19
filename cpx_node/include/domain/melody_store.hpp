#pragma once

#include <stdint.h>
#include <string.h>
#include "mcshal_protocol.hpp"

namespace mcshal {

// Accumulates 'M' melody chunks into a playable note buffer.
class MelodyStore {
public:
    void clear() {
        total_ = 0;
        received_ = 0;
        memset(mask_, 0, sizeof(mask_));
    }

    void appendChunk(const MelodyChunk &chunk) {
        if (chunk.index == 0) {
            clear();
            total_ = chunk.total;
        }
        for (uint8_t i = 0; i < chunk.count; ++i) {
            uint16_t idx = static_cast<uint16_t>(chunk.index) + i;
            if (idx >= MelodyChunk::kMaxNotes) return;
            freq_[idx] = chunk.freq_hz[i];
            dur_[idx] = chunk.dur_ms[i];
            if (!isBitSet(idx)) {
                setBitAt(idx);
                received_++;
            }
        }
    }

    bool complete() const { return total_ > 0 && received_ >= total_; }
    uint16_t total() const { return total_; }
    const uint16_t *freqs() const { return freq_; }
    const uint16_t *durs() const { return dur_; }

private:
    bool isBitSet(uint16_t idx) const { return (mask_[idx >> 3] >> (idx & 7)) & 1; }
    void setBitAt(uint16_t idx) { mask_[idx >> 3] |= static_cast<uint8_t>(1u << (idx & 7)); }

    uint16_t freq_[MelodyChunk::kMaxNotes]{};
    uint16_t dur_[MelodyChunk::kMaxNotes]{};
    uint8_t mask_[MelodyChunk::kMaxNotes / 8]{};
    uint16_t total_{0};
    uint16_t received_{0};
};

} // namespace mcshal
