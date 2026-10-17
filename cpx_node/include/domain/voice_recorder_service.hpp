#pragma once

#include <Adafruit_CircuitPlayground.h>
#include <Arduino.h>
#include "interfaces/i_audio_codec.hpp"
#include "interfaces/i_audio_storage.hpp"

namespace mcshal {

// Domain service coordinating microphone capture, codec compression (4-bit ADPCM or 8-bit PCM),
// and storage persistence (SRAM buffer and/or 2 MB SPI Flash).
class VoiceRecorderService {
public:
    VoiceRecorderService(IAudioCodec &codec,
                         IAudioStorage &primary_storage,
                         IAudioStorage *persistent_mirror = nullptr)
        : codec_(codec),
          primary_(primary_storage),
          mirror_(persistent_mirror) {}

    bool begin() {
        bool ok = primary_.begin();
        if (mirror_) {
            mirror_->begin();
        }
        return ok;
    }

    // Records `duration_ms` at 7,333 Hz using `codec_`, writes to `primary_`
    // (and optionally persists to `mirror_` SPI Flash after recording so zero
    // samples are dropped during real-time capture).
    bool recordClip(uint16_t duration_ms) {
        static constexpr uint32_t kSampleRateHz = 7333;
        const uint32_t total_samples = ((kSampleRateHz * duration_ms) / 1000) & ~1UL;
        const size_t required_bytes = (codec_.bitsPerSample() == 4)
                                          ? (total_samples / 2)
                                          : total_samples;

        if (!primary_.prepareWrite(required_bytes)) {
            return false;
        }

        codec_.reset();

        int16_t raw_block[60];
        uint8_t encoded_block[20];
        int32_t dc_sum = 0;
        int16_t peak = 16;
        uint32_t samples_captured = 0;
        uint8_t last_leds_lit = 10;

        for (uint8_t i = 0; i < 10; ++i) {
            CircuitPlayground.strip.setPixelColor(i, 200, 0, 0);
        }
        CircuitPlayground.strip.show();

        while (samples_captured < total_samples) {
            CircuitPlayground.mic.capture(raw_block, 60);
            size_t enc_idx = 0;
            uint8_t packed_byte = 0;
            bool high_nibble = false;

            for (size_t i = 0; i < 60 && samples_captured < total_samples; i += 3) {
                int16_t avg3 = static_cast<int16_t>(
                    (static_cast<int32_t>(raw_block[i]) +
                     static_cast<int32_t>(raw_block[i + 1]) +
                     static_cast<int32_t>(raw_block[i + 2])) / 3);
                dc_sum += avg3;
                int16_t abs_v = avg3 < 0 ? -avg3 : avg3;
                if (abs_v > peak) peak = abs_v;

                if (codec_.bitsPerSample() == 4) {
                    uint8_t nib = codec_.encodeSample(avg3);
                    if (!high_nibble) {
                        packed_byte = nib & 0x0F;
                        high_nibble = true;
                    } else {
                        packed_byte |= static_cast<uint8_t>((nib & 0x0F) << 4);
                        encoded_block[enc_idx++] = packed_byte;
                        high_nibble = false;
                    }
                } else {
                    encoded_block[enc_idx++] = codec_.encodeSample(avg3);
                }
                samples_captured++;
            }

            primary_.writeChunk(encoded_block, enc_idx);

            uint8_t remaining_leds = static_cast<uint8_t>(
                ((total_samples - samples_captured) * 10 + total_samples - 1) / total_samples);
            if (remaining_leds != last_leds_lit) {
                last_leds_lit = remaining_leds;
                CircuitPlayground.strip.clear();
                for (uint8_t p = 0; p < remaining_leds; ++p) {
                    CircuitPlayground.strip.setPixelColor(p, 200, 0, 0);
                }
                CircuitPlayground.strip.show();
            }
        }

        AudioClipMetadata meta;
        meta.sample_rate_hz = kSampleRateHz;
        meta.sample_count = samples_captured;
        meta.byte_length = static_cast<uint32_t>(required_bytes);
        meta.dc_offset = static_cast<int16_t>(dc_sum / static_cast<int32_t>(samples_captured));
        meta.peak_amplitude = peak;
        meta.bits_per_sample = codec_.bitsPerSample();
        primary_.commitWrite(meta);

        // Persist to onboard 2MB SPI Flash mirror if attached
        if (mirror_ && mirror_->prepareWrite(required_bytes)) {
            uint8_t copy_buf[64];
            size_t offset = 0;
            while (offset < required_bytes) {
                size_t chunk = (required_bytes - offset > sizeof(copy_buf))
                                   ? sizeof(copy_buf)
                                   : (required_bytes - offset);
                primary_.readChunk(offset, copy_buf, chunk);
                mirror_->writeChunk(copy_buf, chunk);
                offset += chunk;
            }
            mirror_->commitWrite(meta);
        }

        return true;
    }

    // Decodes and streams the stored clip from `storage` (`primary_` or `mirror_`)
    // via a callback `emitDac10Bit(uint16_t sample_0_1023)`
    template <typename DacWriteFn>
    bool playbackFrom(IAudioStorage &storage, DacWriteFn emitDac10Bit) {
        AudioClipMetadata meta;
        if (!storage.loadMetadata(meta) || meta.byte_length == 0) {
            return false;
        }

        codec_.reset();
        const uint32_t us_per_sample = 1000000UL / meta.sample_rate_hz;
        const int32_t peak = (meta.peak_amplitude > 12) ? meta.peak_amplitude : 12;

        uint8_t chunk_buf[64];
        size_t offset = 0;
        uint32_t prev_us = micros();

        while (offset < meta.byte_length) {
            size_t chunk = (meta.byte_length - offset > sizeof(chunk_buf))
                               ? sizeof(chunk_buf)
                               : (meta.byte_length - offset);
            if (!storage.readChunk(offset, chunk_buf, chunk)) {
                break;
            }
            offset += chunk;

            for (size_t i = 0; i < chunk; ++i) {
                if (meta.bits_per_sample == 4) {
                    uint8_t b = chunk_buf[i];
                    for (int n = 0; n < 2; ++n) {
                        uint8_t nib = (n == 0) ? (b & 0x0F) : ((b >> 4) & 0x0F);
                        int16_t pcm = codec_.decodeSample(nib);
                        while ((micros() - prev_us) < us_per_sample) {}
                        prev_us = micros();
                        emitDac10Bit(normalizeTo10Bit(pcm, meta.dc_offset, peak));
                    }
                } else {
                    int16_t pcm = codec_.decodeSample(chunk_buf[i]);
                    while ((micros() - prev_us) < us_per_sample) {}
                    prev_us = micros();
                    emitDac10Bit(normalizeTo10Bit(pcm, meta.dc_offset, peak));
                }
            }
        }
        return true;
    }

    IAudioStorage &primaryStorage() { return primary_; }
    IAudioStorage *mirrorStorage() { return mirror_; }

private:
    static uint16_t normalizeTo10Bit(int16_t pcm, int16_t dc_offset, int32_t peak) {
        int32_t centered = static_cast<int32_t>(pcm) - dc_offset;
        int32_t scaled = (centered * 720) / peak;
        if (scaled > 511) scaled = 511;
        else if (scaled < -512) scaled = -512;
        return static_cast<uint16_t>(scaled + 512);
    }

    IAudioCodec &codec_;
    IAudioStorage &primary_;
    IAudioStorage *mirror_;
};

} // namespace mcshal
