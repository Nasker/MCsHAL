#pragma once

#include "interfaces/i_audio_codec.hpp"

namespace mcshal {

namespace detail {
static const int8_t kAdpcmIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const int16_t kAdpcmStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
} // namespace detail

// 4-bit IMA ADPCM codec: compresses 16-bit PCM into 4-bit nibbles (2 samples per byte),
// doubling recording duration in both SRAM and SPI Flash.
class ImaAdpcmCodec final : public IAudioCodec {
public:
    void reset() override {
        predictor_ = 0;
        step_index_ = 0;
    }

    uint8_t bitsPerSample() const override { return 4; }

    uint8_t encodeSample(int16_t pcm_sample) override {
        int32_t diff = static_cast<int32_t>(pcm_sample) - predictor_;
        uint8_t nibble = 0;
        if (diff < 0) {
            nibble = 8;
            diff = -diff;
        }

        int32_t step = detail::kAdpcmStepTable[step_index_];
        int32_t vpdiff = step >> 3;

        if (diff >= step) {
            nibble |= 4;
            diff -= step;
            vpdiff += step;
        }
        step >>= 1;
        if (diff >= step) {
            nibble |= 2;
            diff -= step;
            vpdiff += step;
        }
        step >>= 1;
        if (diff >= step) {
            nibble |= 1;
            vpdiff += step;
        }

        if (nibble & 8) predictor_ -= vpdiff;
        else predictor_ += vpdiff;

        if (predictor_ > 32767) predictor_ = 32767;
        else if (predictor_ < -32768) predictor_ = -32768;

        step_index_ += detail::kAdpcmIndexTable[nibble];
        if (step_index_ < 0) step_index_ = 0;
        else if (step_index_ > 88) step_index_ = 88;

        return nibble & 0x0F;
    }

    int16_t decodeSample(uint8_t nibble) override {
        nibble &= 0x0F;
        int32_t step = detail::kAdpcmStepTable[step_index_];
        int32_t vpdiff = step >> 3;

        if (nibble & 4) vpdiff += step;
        if (nibble & 2) vpdiff += (step >> 1);
        if (nibble & 1) vpdiff += (step >> 2);

        if (nibble & 8) predictor_ -= vpdiff;
        else predictor_ += vpdiff;

        if (predictor_ > 32767) predictor_ = 32767;
        else if (predictor_ < -32768) predictor_ = -32768;

        step_index_ += detail::kAdpcmIndexTable[nibble];
        if (step_index_ < 0) step_index_ = 0;
        else if (step_index_ > 88) step_index_ = 88;

        return static_cast<int16_t>(predictor_);
    }

private:
    int32_t predictor_{0};
    int8_t step_index_{0};
};

// Uncompressed 8-bit signed PCM codec (1 byte per sample)
class Raw8BitPcmCodec final : public IAudioCodec {
public:
    void reset() override {}
    uint8_t bitsPerSample() const override { return 8; }

    uint8_t encodeSample(int16_t pcm_sample) override {
        int16_t s = pcm_sample >> 2; // 10-bit signed (-512..511) -> 8-bit signed (-128..127)
        if (s > 127) s = 127;
        else if (s < -128) s = -128;
        return static_cast<uint8_t>(static_cast<int8_t>(s));
    }

    int16_t decodeSample(uint8_t encoded_code) override {
        return static_cast<int16_t>(static_cast<int8_t>(encoded_code)) << 2;
    }
};

} // namespace mcshal
