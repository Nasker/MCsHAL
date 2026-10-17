#pragma once

#include <Arduino.h>
#include "interfaces/i_cpx_bridge.hpp"

namespace mcshal {

class UartCpxBridge final : public ICpxBridge {
public:
    UartCpxBridge(HardwareSerial &serial, int rx_pin, int tx_pin, unsigned long baud)
        : serial_(serial), rx_pin_(rx_pin), tx_pin_(tx_pin), baud_(baud) {}

    void begin() {
        serial_.begin(baud_, SERIAL_8N1, rx_pin_, tx_pin_);
    }

    void poll(uint32_t now_ms) override {
        while (serial_.available() > 0) {
            char c = static_cast<char>(serial_.read());
            if (c == '\r') continue;
            if (c == '\n') {
                rx_buf_[rx_pos_] = '\0';
                rx_pos_ = 0;
                TelemetryFrame parsed;
                if (ProtocolCodec::decodeTelemetry(rx_buf_, parsed)) {
                    latest_ = parsed;
                    last_rx_ms_ = now_ms;
                    has_received_ = true;
                }
            } else if (rx_pos_ + 1 < sizeof(rx_buf_)) {
                rx_buf_[rx_pos_++] = c;
            } else {
                rx_pos_ = 0;
            }
        }
    }

    TelemetryFrame latestTelemetry() const override { return latest_; }

    uint32_t lastFrameAgeMs(uint32_t now_ms) const override {
        return has_received_ ? (now_ms - last_rx_ms_) : 999999UL;
    }

    void sendCommand(const ConfigCommand &cmd) override {
        char buf[64];
        size_t len = ProtocolCodec::encodeCommand(cmd, buf, sizeof(buf));
        if (len > 0) {
            serial_.write(reinterpret_cast<const uint8_t *>(buf), len);
        }
    }

private:
    HardwareSerial &serial_;
    int rx_pin_;
    int tx_pin_;
    unsigned long baud_;
    TelemetryFrame latest_{};
    uint32_t last_rx_ms_{0};
    bool has_received_{false};
    char rx_buf_[96]{};
    size_t rx_pos_{0};
};

} // namespace mcshal
