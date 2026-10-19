#pragma once

#include <Arduino.h>
#include "driver/gpio.h"
#include "esp32-hal-uart.h"
#include "interfaces/i_cpx_bridge.hpp"

namespace mcshal {

class UartCpxBridge final : public ICpxBridge {
public:
    UartCpxBridge(HardwareSerial &serial, int rx_pin, int tx_pin, unsigned long baud)
        : serial_(serial), rx_pin_(rx_pin), tx_pin_(tx_pin), baud_(baud) {}

    void begin() {
        // Detach UART0 (Serial0) from GPIO20/GPIO21 first! By default the ESP32-C3 ROM and
        // Serial0 constructor attach U0TXD to GPIO21 (driving it HIGH), which conflicts with
        // the SuperMini pinout diagram that labels GPIO21 as RX and GPIO20 as TX.
        Serial0.end();
        uartSetPins(0, -1, -1, -1, -1);
        gpio_reset_pin(static_cast<gpio_num_t>(rx_pin_));
        gpio_reset_pin(static_cast<gpio_num_t>(tx_pin_));

        serial_.begin(baud_, SERIAL_8N1, rx_pin_, tx_pin_);
    }

    void poll(uint32_t now_ms) override {
        while (serial_.available() > 0) {
            char c = static_cast<char>(serial_.read());
            if (c == '\r') continue;
            if (c == '\n') {
                rx_buf_[rx_pos_] = '\0';
                strncpy(last_raw_line_, rx_buf_, sizeof(last_raw_line_) - 1);
                rx_pos_ = 0;
                TelemetryFrame parsed;
                if (ProtocolCodec::decodeTelemetry(rx_buf_, parsed)) {
                    latest_ = parsed;
                    last_rx_ms_ = now_ms;
                    has_received_ = true;
                    frame_count_++;
                }
            } else if (rx_pos_ + 1 < sizeof(rx_buf_)) {
                rx_buf_[rx_pos_++] = c;
                rx_bytes_++;
            } else {
                rx_pos_ = 0;
            }
        }
    }

    int activeRxPin() const { return rx_pin_; }
    int activeTxPin() const { return tx_pin_; }
    uint32_t frameCount() const { return frame_count_; }
    uint32_t rxBytes() const { return rx_bytes_; }
    const char *lastRawLine() const { return last_raw_line_; }

    TelemetryFrame latestTelemetry() const override { return latest_; }

    uint32_t lastFrameAgeMs(uint32_t now_ms) const override {
        return has_received_ ? (now_ms - last_rx_ms_) : 999999UL;
    }

    void sendCommand(const ConfigCommand &cmd) override {
        char buf[64];
        sendEncoded(ProtocolCodec::encodeCommand(cmd, buf, sizeof(buf)), buf);
    }

    void sendClock(const ClockSync &clk) override {
        char buf[24];
        sendEncoded(ProtocolCodec::encodeClock(clk, buf, sizeof(buf)), buf);
    }

    void sendPolicy(const WakePolicy &w) override {
        char buf[40];
        sendEncoded(ProtocolCodec::encodePolicy(w, buf, sizeof(buf)), buf);
    }

    void sendCostume(const CostumePalette &k) override {
        char buf[192];
        sendEncoded(ProtocolCodec::encodeCostume(k, buf, sizeof(buf)), buf);
    }

    void sendMelodyChunk(const MelodyChunk &m) override {
        char buf[128];
        sendEncoded(ProtocolCodec::encodeMelodyChunk(m, buf, sizeof(buf)), buf);
    }

    void sendRemote(IrKeyId key) override {
        char buf[12];
        sendEncoded(ProtocolCodec::encodeRemote(key, buf, sizeof(buf)), buf);
    }

    void sendGreet() override {
        serial_.write(reinterpret_cast<const uint8_t *>("G\n"), 2);
    }

private:
    void sendEncoded(size_t len, const char *buf) {
        if (len > 0) {
            serial_.write(reinterpret_cast<const uint8_t *>(buf), len);
        }
    }

    HardwareSerial &serial_;
    int rx_pin_;
    int tx_pin_;
    unsigned long baud_;
    TelemetryFrame latest_{};
    uint32_t last_rx_ms_{0};
    uint32_t frame_count_{0};
    uint32_t rx_bytes_{0};
    bool has_received_{false};
    char rx_buf_[96]{};
    char last_raw_line_[96]{};
    size_t rx_pos_{0};
};

} // namespace mcshal
