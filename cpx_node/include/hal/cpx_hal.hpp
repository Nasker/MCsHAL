#pragma once

#include <Adafruit_CircuitPlayground.h>
#include <Arduino.h>
#include <math.h>
#include "wiring_private.h"
#include "domain/voice_recorder_service.hpp"
#include "hal/cpx_speech_vocab.hpp"
#include "interfaces/i_hardware.hpp"

namespace mcshal {

class CpxSensorProvider final : public ISensorProvider {
public:
    SensorReading sample() override {
        SensorReading r;
        r.temperature_c = CircuitPlayground.temperature();
        r.light_level = CircuitPlayground.lightSensor();
        r.sound_db = CircuitPlayground.mic.soundPressureLevel(8);
        r.mute_switch = !CircuitPlayground.slideSwitch();
        return r;
    }

    // Zero-static-RAM 32-band frequency/envelope decomposition (saves 8KB SRAM vs Adafruit_ZeroFFT)
    void sampleFft(uint16_t (&spectrum)[32]) override {
        int16_t raw[64];
        CircuitPlayground.mic.capture(raw, 64);
        int32_t avg = 0;
        for (int i = 0; i < 64; ++i) avg += raw[i];
        avg /= 64;
        for (int i = 0; i < 64; ++i) raw[i] = static_cast<int16_t>(raw[i] - avg);

        for (int b = 0; b < 32; ++b) {
            int stride = (b % 8) + 1;
            int32_t acc = 0;
            for (int i = b; i < 64 - stride; i += 2) {
                int32_t diff = raw[i] - raw[i + stride];
                acc += (diff < 0 ? -diff : diff);
            }
            uint32_t mag = static_cast<uint32_t>(acc / 8);
            spectrum[b] = (mag > 255) ? 255 : static_cast<uint16_t>(mag);
        }
    }
};

class CpxNeoPixelVisualizer final : public ILedVisualizer {
public:
    void renderComfortScore(uint8_t score, bool is_noisy) override {
        uint8_t lit_leds = static_cast<uint8_t>((score + 5) / 10);
        if (lit_leds < 1) lit_leds = 1;
        if (lit_leds > 10) lit_leds = 10;

        breath_phase_ += 10;
        uint8_t tri = (breath_phase_ & 0x80) ? (255 - ((breath_phase_ - 128) * 2)) : (breath_phase_ * 2);
        uint16_t pulse = 150 + (tri * 105) / 255;

        uint8_t r = 0, g = 0, b = 0;
        if (is_noisy) {
            r = 255;
            g = 10;
            b = 10;
        } else if (score >= 50) {
            uint8_t t = static_cast<uint8_t>((score - 50) * 2);
            r = static_cast<uint8_t>((100 - t) * 2);
            g = 200;
            b = static_cast<uint8_t>(t / 2);
        } else {
            uint8_t t = static_cast<uint8_t>(score * 2);
            r = 220;
            g = static_cast<uint8_t>(t * 18 / 10);
            b = 0;
        }

        r = static_cast<uint8_t>((r * pulse) >> 8);
        g = static_cast<uint8_t>((g * pulse) >> 8);
        b = static_cast<uint8_t>((b * pulse) >> 8);

        CircuitPlayground.strip.clear();
        for (uint8_t i = 0; i < lit_leds; ++i) {
            CircuitPlayground.strip.setPixelColor(i, r, g, b);
        }
        uint8_t head = (breath_phase_ / 26) % 10;
        if (head >= lit_leds) {
            CircuitPlayground.strip.setPixelColor(head, 0, 12, 28);
        }
        CircuitPlayground.strip.show();
    }

    void renderSolidRing(uint8_t r, uint8_t g, uint8_t b, uint8_t active_pixels) override {
        CircuitPlayground.strip.clear();
        for (uint8_t i = 0; i < active_pixels && i < 10; ++i) {
            CircuitPlayground.strip.setPixelColor(i, r, g, b);
        }
        CircuitPlayground.strip.show();
    }

    void renderRainbowWheel(uint8_t phase_offset) override {
        for (uint8_t i = 0; i < 10; ++i) {
            uint8_t wheel_pos = static_cast<uint8_t>(phase_offset + i * 25);
            CircuitPlayground.strip.setPixelColor(i, CircuitPlayground.colorWheel(wheel_pos));
        }
        CircuitPlayground.strip.show();
    }

    void renderFftSpectrum(const uint16_t (&spectrum)[32], uint8_t phase_offset) override {
        for (uint8_t i = 0; i < 10; ++i) {
            uint32_t bin_energy = spectrum[i * 3 + 1] + spectrum[i * 3 + 2] + spectrum[i * 3 + 3];
            uint32_t color = CircuitPlayground.colorWheel(static_cast<uint8_t>(phase_offset + i * 24));
            uint8_t scale = (bin_energy > 255) ? 255 : static_cast<uint8_t>(bin_energy);
            if (scale < 20) scale = 20;
            uint8_t r = static_cast<uint8_t>(((color >> 16) & 0xFF) * scale / 255);
            uint8_t g = static_cast<uint8_t>(((color >> 8) & 0xFF) * scale / 255);
            uint8_t b = static_cast<uint8_t>((color & 0xFF) * scale / 255);
            CircuitPlayground.strip.setPixelColor(i, r, g, b);
        }
        CircuitPlayground.strip.show();
    }

    void clear() override { CircuitPlayground.clearPixels(); }

private:
    uint8_t breath_phase_{0};
};

class CpxSpeakerAlert final : public IAudioAlert {
public:
    explicit CpxSpeakerAlert(VoiceRecorderService &recorder) : recorder_(recorder) {}

    void begin() {
        disableSpeakerAmp();
    }

    void playBootGreeting() override {
        if (!CircuitPlayground.slideSwitch()) {
            disableSpeakerAmp();
            return;
        }
        enableSpeakerAmp();
        playDacSweep10Bit(260.0f, 1046.5f, 180);
        say10Bit(vocab::spHELLO);
        say10Bit(vocab::spGOOD);
        disableSpeakerAmp();
    }

    void playNoiseWarning(uint32_t now_ms) override {
        if (now_ms - last_beep_ms_ < 4000) {
            return;
        }
        last_beep_ms_ = now_ms;
        enableSpeakerAmp();
        playDacSweep10Bit(1200.0f, 440.0f, 140);
        say10Bit(vocab::spALERT);
        disableSpeakerAmp();
    }

    // Right Button (D5):
    // - Hold >= 350 ms (or first press when no clip exists): Record 4.0 seconds (4-bit ADPCM)
    //   into SRAM + persist to onboard 2 MB SPI Flash, then immediately replay it.
    // - Quick tap (< 350 ms): Replay the last recorded clip directly from SRAM or 2 MB SPI Flash
    //   (even after unplugging and re-plugging power!).
    void playSynthShowcase() override {
        if (!CircuitPlayground.slideSwitch()) return;

        uint32_t press_start = millis();
        bool long_press = false;
        while (CircuitPlayground.rightButton()) {
            if (millis() - press_start >= 350 && !long_press) {
                long_press = true;
                // Visual cue: purple ring = ready to record on release
                for (uint8_t i = 0; i < 10; ++i) {
                    CircuitPlayground.strip.setPixelColor(i, 160, 0, 160);
                }
                CircuitPlayground.strip.show();
            }
            delay(10);
        }
        delay(30);

        AudioClipMetadata meta;
        bool has_sram = recorder_.primaryStorage().loadMetadata(meta);
        bool has_flash = (!has_sram && recorder_.mirrorStorage() != nullptr &&
                          recorder_.mirrorStorage()->loadMetadata(meta));

        if (long_press || (!has_sram && !has_flash)) {
            enableSpeakerAmp();
            playDacSweep10Bit(880.0f, 1760.0f, 90);
            disableSpeakerAmp();
            delay(50);

            // Record 4.0 seconds using 4-bit IMA ADPCM (14,664 bytes in SRAM + mirrored to 2MB SPI Flash)
            recorder_.recordClip(4000);
            has_sram = true;
        }

        // Replay cue: Cyan ring for SRAM replay, Blue-Gold ring for persistent SPI Flash replay
        for (uint8_t i = 0; i < 10; ++i) {
            if (has_sram) {
                CircuitPlayground.strip.setPixelColor(i, 0, 180, 120);
            } else {
                CircuitPlayground.strip.setPixelColor(i, 40, 120, 255);
            }
        }
        CircuitPlayground.strip.show();

        enableSpeakerAmp();
        playDacSweep10Bit(1320.0f, 880.0f, 70);
        delay(50);

        IAudioStorage &source = has_sram ? recorder_.primaryStorage() : *recorder_.mirrorStorage();
        recorder_.playbackFrom(source, [](uint16_t sample_10bit) {
            writeDac10Bit(sample_10bit);
        });

        disableSpeakerAmp();
    }

private:
    // Wakes the onboard Class-D audio amplifier (D11 = HIGH) and connects A0 to the 10-bit DAC
    static void enableSpeakerAmp() {
        static bool dac_initialized = false;
        if (!dac_initialized) {
            dac_initialized = true;
            pinPeripheral(A0, PIO_ANALOG);
            while (DAC->STATUS.bit.SYNCBUSY == 1) {}
            DAC->CTRLA.bit.ENABLE = 1;
            while (DAC->STATUS.bit.SYNCBUSY == 1) {}
        }
        writeDac10Bit(512);
        pinMode(CPLAY_SPEAKER_SHUTDOWN, OUTPUT);
        digitalWrite(CPLAY_SPEAKER_SHUTDOWN, HIGH);
        delay(3);
    }

    // Shuts down the PAM8301 Class-D audio amplifier (D11 = LOW -> <1 uA, zero static hiss)
    static void disableSpeakerAmp() {
        pinMode(CPLAY_SPEAKER_SHUTDOWN, OUTPUT);
        digitalWrite(CPLAY_SPEAKER_SHUTDOWN, LOW);
    }

    static inline void writeDac10Bit(uint16_t sample_10bit) {
        while (DAC->STATUS.bit.SYNCBUSY == 1) {}
        DAC->DATA.reg = sample_10bit & 0x3FF;
    }

    void playDacSweep10Bit(float start_hz, float end_hz, uint16_t duration_ms) {
        const uint32_t sample_rate = 16000;
        const uint32_t total_samples = (sample_rate * duration_ms) / 1000;
        const uint32_t us_per_sample = 1000000UL / sample_rate;
        uint32_t phase = 0;

        for (uint32_t i = 0; i < total_samples; ++i) {
            float progress = static_cast<float>(i) / static_cast<float>(total_samples);
            float freq = start_hz + (end_hz - start_hz) * progress;
            phase += static_cast<uint32_t>((freq * 65536.0f) / sample_rate);
            uint16_t sqr = (phase & 0x8000) ? 1023 : 0;
            writeDac10Bit(sqr);
            delayMicroseconds(us_per_sample);
        }
        writeDac10Bit(512);
    }

    static uint8_t revByte(uint8_t a) {
        a = (a >> 4) | (a << 4);
        a = ((a & 0xCC) >> 2) | ((a & 0x33) << 2);
        a = ((a & 0xAA) >> 1) | ((a & 0x55) << 1);
        return a;
    }

    void say10Bit(const uint8_t *addr) {
        static const uint16_t tmsK1[] PROGMEM = {
            0x82C0,0x8380,0x83C0,0x8440,0x84C0,0x8540,0x8600,0x8780,
            0x8880,0x8980,0x8AC0,0x8C00,0x8D40,0x8F00,0x90C0,0x92C0,
            0x9900,0xA140,0xAB80,0xB840,0xC740,0xD8C0,0xEBC0,0x0000,
            0x1440,0x2740,0x38C0,0x47C0,0x5480,0x5EC0,0x6700,0x6D40};
        static const uint16_t tmsK2[] PROGMEM = {
            0xAE00,0xB480,0xBB80,0xC340,0xCB80,0xD440,0xDDC0,0xE780,
            0xF180,0xFBC0,0x0600,0x1040,0x1A40,0x2400,0x2D40,0x3600,
            0x3E40,0x45C0,0x4CC0,0x5300,0x5880,0x5DC0,0x6240,0x6640,
            0x69C0,0x6CC0,0x6F80,0x71C0,0x73C0,0x7580,0x7700,0x7E80};
        static const uint8_t tmsK3[] PROGMEM = {
            0x92,0x9F,0xAD,0xBA,0xC8,0xD5,0xE3,0xF0,0xFE,0x0B,0x19,0x26,0x34,0x41,0x4F,0x5C};
        static const uint8_t tmsK4[] PROGMEM = {
            0xAE,0xBC,0xCA,0xD8,0xE6,0xF4,0x01,0x0F,0x1D,0x2B,0x39,0x47,0x55,0x63,0x71,0x7E};
        static const uint8_t tmsK5[] PROGMEM = {
            0xAE,0xBA,0xC5,0xD1,0xDD,0xE8,0xF4,0xFF,0x0B,0x17,0x22,0x2E,0x39,0x45,0x51,0x5C};
        static const uint8_t tmsK6[] PROGMEM = {
            0xC0,0xCB,0xD6,0xE1,0xEC,0xF7,0x03,0x0E,0x19,0x24,0x2F,0x3A,0x45,0x50,0x5B,0x66};
        static const uint8_t tmsK7[] PROGMEM = {
            0xB3,0xBF,0xCB,0xD7,0xE3,0xEF,0xFB,0x07,0x13,0x1F,0x2B,0x37,0x43,0x4F,0x5A,0x66};
        static const uint8_t tmsK8[] PROGMEM = {0xC0,0xD8,0xF0,0x07,0x1F,0x37,0x4F,0x66};
        static const uint8_t tmsK9[] PROGMEM = {0xC0,0xD4,0xE8,0xFC,0x10,0x25,0x39,0x4D};
        static const uint8_t tmsK10[] PROGMEM = {0xCD,0xDF,0xF1,0x04,0x16,0x20,0x3B,0x4D};
        static const uint8_t chirp[] PROGMEM = {
            0x00,0x2A,0xD4,0x32,0xB2,0x12,0x25,0x14,0x02,0xE1,0xC5,0x02,0x5F,0x5A,0x05,0x0F,
            0x26,0xFC,0xA5,0xA5,0xD6,0xDD,0xDC,0xFC,0x25,0x2B,0x22,0x21,0x0F,0xFF,0xF8,0xEE,
            0xED,0xEF,0xF7,0xF6,0xFA,0x00,0x03,0x02,0x01};
        static const uint8_t tmsEnergy[] PROGMEM = {
            0x00,0x02,0x03,0x04,0x05,0x07,0x0A,0x0F,0x14,0x20,0x29,0x39,0x51,0x72,0xA1,0xFF};
        static const uint8_t tmsPeriod[] PROGMEM = {
            0x00,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,
            0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2D,0x2F,0x31,
            0x33,0x35,0x36,0x39,0x3B,0x3D,0x3F,0x42,0x45,0x47,0x49,0x4D,0x4F,0x51,0x55,0x57,
            0x5C,0x5F,0x63,0x66,0x6A,0x6E,0x73,0x77,0x7B,0x80,0x85,0x8A,0x8F,0x95,0x9A,0xA0};

        const uint8_t *ptr = addr;
        uint16_t bit_buf = 0;
        uint8_t bit_count = 0;

        auto getBits = [&](uint8_t bits) -> uint8_t {
            if (bits > bit_count) {
                bit_buf |= static_cast<uint16_t>(revByte(pgm_read_byte(ptr))) << (8 - bit_count);
                bit_count += 8;
                ptr++;
            }
            uint8_t val = bit_buf >> (16 - bits);
            bit_buf <<= bits;
            bit_count -= bits;
            return val;
        };

        int16_t x0 = 0, x1 = 0, x2 = 0, x3 = 0, x4 = 0, x5 = 0, x6 = 0, x7 = 0, x8 = 0, x9 = 0;
        int16_t synthK1 = 0, synthK2 = 0, u0 = 0;
        uint16_t synthEnergy = 0, synthRand = 1;
        int8_t synthK3 = 0, synthK4 = 0, synthK5 = 0, synthK6 = 0, synthK7 = 0, synthK8 = 0, synthK9 = 0, synthK10 = 0;
        uint8_t periodCounter = 0, synthPeriod = 0;
        uint16_t nextDac = 512;
        uint8_t iCount = 200;
        uint32_t prevTime = micros();

        for (uint32_t max_samples = 0; max_samples < 32000; ++max_samples) {
            while ((micros() - prevTime) < 125) {}
            prevTime = micros();
            writeDac10Bit(nextDac);

            if (++iCount >= 200) {
                uint8_t energy = getBits(4);
                if (energy == 0) {
                    synthEnergy = 0;
                } else if (energy == 0xF) {
                    writeDac10Bit(512);
                    break;
                } else {
                    synthEnergy = pgm_read_byte(&tmsEnergy[energy]);
                    uint8_t repeat = getBits(1);
                    synthPeriod = pgm_read_byte(&tmsPeriod[getBits(6)]);
                    if (!repeat) {
                        synthK1 = pgm_read_word(&tmsK1[getBits(5)]);
                        synthK2 = pgm_read_word(&tmsK2[getBits(5)]);
                        synthK3 = pgm_read_byte(&tmsK3[getBits(4)]);
                        synthK4 = pgm_read_byte(&tmsK4[getBits(4)]);
                        if (synthPeriod) {
                            synthK5 = pgm_read_byte(&tmsK5[getBits(4)]);
                            synthK6 = pgm_read_byte(&tmsK6[getBits(4)]);
                            synthK7 = pgm_read_byte(&tmsK7[getBits(4)]);
                            synthK8 = pgm_read_byte(&tmsK8[getBits(3)]);
                            synthK9 = pgm_read_byte(&tmsK9[getBits(3)]);
                            synthK10 = pgm_read_byte(&tmsK10[getBits(3)]);
                        }
                    }
                }
                iCount = 0;
            }

            if (synthPeriod) {
                if (++periodCounter >= synthPeriod) periodCounter = 0;
                u0 = (periodCounter >= sizeof(chirp))
                         ? 0
                         : static_cast<int16_t>((static_cast<int8_t>(pgm_read_byte(&chirp[periodCounter])) *
                                                 static_cast<int32_t>(synthEnergy)) >> 8);
            } else {
                synthRand = (synthRand >> 1) ^ ((synthRand & 1) ? 0xB800 : 0);
                u0 = (synthRand & 1) ? static_cast<int16_t>(synthEnergy) : -static_cast<int16_t>(synthEnergy);
            }

            u0 -= ((synthK10 * x9) + (synthK9 * x8)) >> 7;
            x9 = x8 + ((synthK9 * u0) >> 7);
            u0 -= (synthK8 * x7) >> 7;
            x8 = x7 + ((synthK8 * u0) >> 7);
            u0 -= (synthK7 * x6) >> 7;
            x7 = x6 + ((synthK7 * u0) >> 7);
            u0 -= (synthK6 * x5) >> 7;
            x6 = x5 + ((synthK6 * u0) >> 7);
            u0 -= (synthK5 * x4) >> 7;
            x5 = x4 + ((synthK5 * u0) >> 7);
            u0 -= (synthK4 * x3) >> 7;
            x4 = x3 + ((synthK4 * u0) >> 7);
            u0 -= (synthK3 * x2) >> 7;
            x3 = x2 + ((synthK3 * u0) >> 7);
            u0 -= (synthK2 * static_cast<int32_t>(x1)) >> 15;
            x2 = x1 + ((synthK2 * static_cast<int32_t>(u0)) >> 15);
            u0 -= (synthK1 * static_cast<int32_t>(x0)) >> 15;
            x1 = x0 + ((synthK1 * static_cast<int32_t>(u0)) >> 15);

            if (u0 > 511) u0 = 511;
            else if (u0 < -512) u0 = -512;

            x0 = u0;
            int32_t boosted = static_cast<int32_t>(u0) * 4;
            if (boosted > 511) boosted = 511;
            else if (boosted < -512) boosted = -512;
            nextDac = static_cast<uint16_t>(boosted + 512);
        }
    }

    VoiceRecorderService &recorder_;
    uint32_t last_beep_ms_{0};
};

class CpxSerialGateway final : public ITelemetryGateway {
public:
    explicit CpxSerialGateway(HardwareSerial &uart) : uart_(uart) {}

    void begin(unsigned long baud = 115200) {
        uart_.begin(baud);
        pinPeripheral(PIN_SERIAL1_RX, PIO_SERCOM_ALT);
        pinPeripheral(PIN_SERIAL1_TX, PIO_SERCOM_ALT);
        PORT->Group[PORTB].PINCFG[9].bit.PULLEN = 1;
        PORT->Group[PORTB].OUTSET.reg = (1ul << 9);
    }

    void publish(const TelemetryFrame &frame) override {
        char buf[96];
        size_t len = ProtocolCodec::encodeTelemetry(frame, buf, sizeof(buf));
        if (len > 0) {
            tx_led_ = !tx_led_;
            CircuitPlayground.redLED(tx_led_);
            uart_.write(reinterpret_cast<const uint8_t *>(buf), len);
        }
    }

    bool pollCommand(ConfigCommand &out_cmd) override {
        while (uart_.available() > 0) {
            char c = static_cast<char>(uart_.read());
            if (c == '\r') continue;
            if (c == '\n') {
                rx_buf_[rx_pos_] = '\0';
                rx_pos_ = 0;
                if (ProtocolCodec::decodeCommand(rx_buf_, out_cmd)) {
                    return true;
                }
            } else if (rx_pos_ + 1 < sizeof(rx_buf_)) {
                rx_buf_[rx_pos_++] = c;
            } else {
                rx_pos_ = 0;
            }
        }
        return false;
    }

private:
    HardwareSerial &uart_;
    char rx_buf_[80]{};
    size_t rx_pos_{0};
    bool tx_led_{false};
};

} // namespace mcshal
