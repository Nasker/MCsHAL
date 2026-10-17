#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <string.h>
#include "interfaces/i_audio_storage.hpp"

namespace mcshal {

// Non-volatile audio storage backend for the Circuit Playground Express onboard
// 2 MB GD25Q16C SPI Flash (SPI / SERCOM3, CS = pin 33).
// Sector 0 (0x000000..0x000FFF) stores AudioClipMetadata.
// Sectors 1..511 (0x001000..0x1FFFFF = 2,093,056 bytes) store audio payload.
// Automatically enters Deep Power-Down (< 1 uA) when idle to save battery.
class SpiFlashAudioStorage final : public IAudioStorage {
public:
    static constexpr uint32_t kSectorSize = 4096UL;
    static constexpr uint32_t kPageSize = 256UL;
    static constexpr uint32_t kDataStartAddr = 0x001000UL;
    static constexpr uint32_t kTotalFlashSize = 2UL * 1024UL * 1024UL; // 2 MB

    explicit SpiFlashAudioStorage(SPIClass &spi = SPI, uint8_t cs_pin = SS)
        : spi_(spi), cs_pin_(cs_pin) {}

    bool begin() override {
        pinMode(cs_pin_, OUTPUT);
        digitalWrite(cs_pin_, HIGH);
        spi_.begin();
        wakeUp();
        uint8_t jedec_id = readManufacturerId();
        deepPowerDown();
        // GD25Q16C (0xC8) or Winbond W25Q16 (0xEF) or Cypress/Adesto (0x01/0x1F)
        return (jedec_id != 0x00 && jedec_id != 0xFF);
    }

    bool isPersistent() const override { return true; }

    size_t capacityBytes() const override {
        return static_cast<size_t>(kTotalFlashSize - kDataStartAddr);
    }

    bool prepareWrite(size_t required_bytes) override {
        if (required_bytes > capacityBytes()) {
            return false;
        }
        wakeUp();
        // Erase metadata sector (Sector 0) and required payload sectors upfront
        // so page writes during recording never stall on sector erases
        eraseSector(0);
        uint32_t sectors = static_cast<uint32_t>((required_bytes + kSectorSize - 1) / kSectorSize);
        for (uint32_t s = 0; s < sectors; ++s) {
            eraseSector(kDataStartAddr + s * kSectorSize);
        }
        write_addr_ = kDataStartAddr;
        bytes_written_ = 0;
        return true;
    }

    bool writeChunk(const uint8_t *data, size_t len) override {
        if (bytes_written_ + len > capacityBytes()) {
            return false;
        }
        while (len > 0) {
            uint32_t page_offset = write_addr_ & (kPageSize - 1);
            size_t chunk = kPageSize - page_offset;
            if (chunk > len) chunk = len;

            writeEnable();
            select();
            spi_.transfer(0x02); // Page Program
            spi_.transfer(static_cast<uint8_t>((write_addr_ >> 16) & 0xFF));
            spi_.transfer(static_cast<uint8_t>((write_addr_ >> 8) & 0xFF));
            spi_.transfer(static_cast<uint8_t>(write_addr_ & 0xFF));
            for (size_t i = 0; i < chunk; ++i) {
                spi_.transfer(data[i]);
            }
            deselect();
            waitWhileBusy();

            write_addr_ += static_cast<uint32_t>(chunk);
            bytes_written_ += chunk;
            data += chunk;
            len -= chunk;
        }
        return true;
    }

    bool commitWrite(const AudioClipMetadata &meta) override {
        AudioClipMetadata stored = meta;
        stored.magic = kValidMagic;
        stored.byte_length = static_cast<uint32_t>(bytes_written_);

        writeEnable();
        select();
        spi_.transfer(0x02); // Page Program at 0x000000
        spi_.transfer(0x00);
        spi_.transfer(0x00);
        spi_.transfer(0x00);
        const auto *raw = reinterpret_cast<const uint8_t *>(&stored);
        for (size_t i = 0; i < sizeof(AudioClipMetadata); ++i) {
            spi_.transfer(raw[i]);
        }
        deselect();
        waitWhileBusy();
        deepPowerDown();
        return true;
    }

    bool loadMetadata(AudioClipMetadata &out_meta) override {
        wakeUp();
        select();
        spi_.transfer(0x03); // Read Data at 0x000000
        spi_.transfer(0x00);
        spi_.transfer(0x00);
        spi_.transfer(0x00);
        auto *raw = reinterpret_cast<uint8_t *>(&out_meta);
        for (size_t i = 0; i < sizeof(AudioClipMetadata); ++i) {
            raw[i] = spi_.transfer(0x00);
        }
        deselect();
        deepPowerDown();
        return (out_meta.magic == kValidMagic && out_meta.byte_length > 0 &&
                out_meta.byte_length <= capacityBytes());
    }

    bool readChunk(size_t byte_offset, uint8_t *out_data, size_t len) override {
        uint32_t addr = kDataStartAddr + static_cast<uint32_t>(byte_offset);
        wakeUp();
        select();
        spi_.transfer(0x03); // Read Data
        spi_.transfer(static_cast<uint8_t>((addr >> 16) & 0xFF));
        spi_.transfer(static_cast<uint8_t>((addr >> 8) & 0xFF));
        spi_.transfer(static_cast<uint8_t>(addr & 0xFF));
        for (size_t i = 0; i < len; ++i) {
            out_data[i] = spi_.transfer(0x00);
        }
        deselect();
        deepPowerDown();
        return true;
    }

private:
    void select() {
        spi_.beginTransaction(SPISettings(12000000UL, MSBFIRST, SPI_MODE0));
        digitalWrite(cs_pin_, LOW);
    }

    void deselect() {
        digitalWrite(cs_pin_, HIGH);
        spi_.endTransaction();
    }

    void wakeUp() {
        select();
        spi_.transfer(0xAB); // Release from Deep Power-Down
        deselect();
        delayMicroseconds(25);
    }

    void deepPowerDown() {
        select();
        spi_.transfer(0xB9); // Deep Power-Down (< 1 uA standby)
        deselect();
    }

    uint8_t readManufacturerId() {
        select();
        spi_.transfer(0x9F); // Read JEDEC ID
        uint8_t mfg = spi_.transfer(0x00);
        spi_.transfer(0x00);
        spi_.transfer(0x00);
        deselect();
        return mfg;
    }

    void writeEnable() {
        select();
        spi_.transfer(0x06);
        deselect();
    }

    void waitWhileBusy() {
        select();
        spi_.transfer(0x05); // Read Status Register-1
        while (spi_.transfer(0x00) & 0x01) {}
        deselect();
    }

    void eraseSector(uint32_t addr) {
        writeEnable();
        select();
        spi_.transfer(0x20); // 4KB Sector Erase
        spi_.transfer(static_cast<uint8_t>((addr >> 16) & 0xFF));
        spi_.transfer(static_cast<uint8_t>((addr >> 8) & 0xFF));
        spi_.transfer(static_cast<uint8_t>(addr & 0xFF));
        deselect();
        waitWhileBusy();
    }

    SPIClass &spi_;
    uint8_t cs_pin_;
    uint32_t write_addr_{kDataStartAddr};
    size_t bytes_written_{0};
};

} // namespace mcshal
