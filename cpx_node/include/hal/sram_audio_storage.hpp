#pragma once

#include <string.h>
#include "interfaces/i_audio_storage.hpp"

namespace mcshal {

// Volatile SRAM audio storage backend (zero erase latency, instant write/read)
template <size_t CapacityBytes>
class SramAudioStorage final : public IAudioStorage {
public:
    bool begin() override { return true; }
    bool isPersistent() const override { return false; }
    size_t capacityBytes() const override { return CapacityBytes; }

    bool prepareWrite(size_t required_bytes) override {
        if (required_bytes > CapacityBytes) {
            return false;
        }
        write_pos_ = 0;
        meta_ = {};
        return true;
    }

    bool writeChunk(const uint8_t *data, size_t len) override {
        if (write_pos_ + len > CapacityBytes) {
            return false;
        }
        memcpy(&buffer_[write_pos_], data, len);
        write_pos_ += len;
        return true;
    }

    bool commitWrite(const AudioClipMetadata &meta) override {
        meta_ = meta;
        meta_.magic = kValidMagic;
        meta_.byte_length = static_cast<uint32_t>(write_pos_);
        return true;
    }

    bool loadMetadata(AudioClipMetadata &out_meta) override {
        if (meta_.magic != kValidMagic || meta_.byte_length == 0) {
            return false;
        }
        out_meta = meta_;
        return true;
    }

    bool readChunk(size_t byte_offset, uint8_t *out_data, size_t len) override {
        if (byte_offset + len > meta_.byte_length) {
            return false;
        }
        memcpy(out_data, &buffer_[byte_offset], len);
        return true;
    }

    uint8_t *rawBuffer() { return buffer_; }

private:
    uint8_t buffer_[CapacityBytes]{};
    size_t write_pos_{0};
    AudioClipMetadata meta_{};
};

} // namespace mcshal
