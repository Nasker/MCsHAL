#pragma once

#include <Adafruit_CircuitPlayground.h>
#include <Arduino.h>
#include "interfaces/i_hardware.hpp"

namespace mcshal {

// Emits TV remote codes through the onboard IR LED (pin 25).
//
// NOTE: the vendored IRLib path (irSend.send -> TCC0 PWM) wedged the board in
// SYNCBUSY spin-waits when interleaved with mic/NeoPixel activity, so this uses
// a self-contained NEC bit-bang on GPIO 25 instead. NEC timing tolerance is
// wide (+-10%) and delayMicroseconds at 48 MHz is easily within spec.
// Codes are the widely-published LG NEC set (device address 0x04, prefix 0x20DF);
// some LG models answer to other addresses — verify on the actual TV.
class CpxIrEmitter final : public IIrEmitter {
public:
    void emit(IrKeyId key) override {
        uint32_t code = codeFor(key);
        if (code == 0) return;
        // 'X' debug frames: the C3 logs them as last_raw_line (unknown frame
        // type), which lets us pinpoint a wedge inside the IR send.
        Serial1.print(F("X,IR,pre\n"));
        sendNec(code);
        Serial1.print(F("X,IR,post\n"));
        // Visual ack: brief D13 flash so a key press is observable without a camera
        CircuitPlayground.redLED(HIGH);
        ir_off_at_ms_ = millis() + 60;
    }

    void tick(uint32_t now_ms) {
        if (ir_off_at_ms_ && static_cast<int32_t>(now_ms - ir_off_at_ms_) >= 0) {
            ir_off_at_ms_ = 0;
            CircuitPlayground.redLED(LOW);
        }
    }

private:
    static constexpr uint8_t IR_PIN = 25; // onboard IR LED, active-HIGH

    // 38 kHz carrier burst: ~13 us half-period
    static void mark(uint16_t us) {
        uint32_t t0 = micros();
        while (static_cast<uint16_t>(micros() - t0) < us) {
            digitalWrite(IR_PIN, HIGH);
            delayMicroseconds(11);
            digitalWrite(IR_PIN, LOW);
            delayMicroseconds(11);
        }
    }

    static void space(uint16_t us) {
        digitalWrite(IR_PIN, LOW);
        uint32_t t0 = micros();
        while (static_cast<uint16_t>(micros() - t0) < us) {}
    }

    // NEC frame: 9 ms mark + 4.5 ms space, 32 bits LSB-first
    // (560 us mark; bit0=560 us space, bit1=1690 us space), trailing mark.
    static void sendNec(uint32_t code) {
        pinMode(IR_PIN, OUTPUT);
        mark(9000);
        space(4500);
        for (uint8_t i = 0; i < 32; ++i) {
            mark(560);
            space((code >> i) & 1 ? 1690 : 560);
        }
        mark(560);
        space(40000); // gap before a potential repeat frame
        digitalWrite(IR_PIN, LOW);
    }

    static uint32_t codeFor(IrKeyId key) {
        switch (key) {
            case IrKeyId::Power: return 0x20DF10EFUL;
            case IrKeyId::Up:    return 0x20DF02FDUL;
            case IrKeyId::Down:  return 0x20DF827DUL;
            case IrKeyId::Left:  return 0x20DFE01FUL;
            case IrKeyId::Right: return 0x20DF609FUL;
            case IrKeyId::Ok:    return 0x20DF22DDUL;
            case IrKeyId::Back:  return 0x20DF14EBUL;
        }
        return 0;
    }

    uint32_t ir_off_at_ms_{0};
};

} // namespace mcshal
