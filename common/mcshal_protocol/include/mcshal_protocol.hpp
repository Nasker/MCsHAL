#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mcshal {

enum class GadgetModeId : uint8_t {
    Comfyness = 0,
    SoundParty = 1,
    PomodoroFocus = 2
};

struct SensorReading {
    float temperature_c{22.0f};
    uint16_t light_level{300};
    float sound_db{45.0f};
    bool mute_switch{false};
};

struct ComfortThresholds {
    float ideal_temp_c{22.5f};
    float temp_tolerance_c{5.0f};
    float max_sound_db{82.0f};
    uint16_t min_comfy_light{80};
    uint16_t max_comfy_light{800};
    bool muted{false};
};

struct ComfortReport {
    uint8_t score{100}; // 0 (miserable) .. 100 (super comfy)
    bool is_too_noisy{false};
};

struct TelemetryFrame {
    float temperature_c{22.0f};
    uint16_t light_level{300};
    float sound_db{45.0f};
    uint8_t comfort_score{100};
    bool is_too_noisy{false};
    bool is_muted{false};
    GadgetModeId mode{GadgetModeId::Comfyness};
};

struct ConfigCommand {
    float max_sound_db{70.0f};
    float ideal_temp_c{22.5f};
    bool muted{false};
    GadgetModeId mode{GadgetModeId::Comfyness};
};

class ProtocolCodec {
public:
    static size_t encodeTelemetry(const TelemetryFrame &f, char *out, size_t max_len) {
        int n = snprintf(
            out, max_len, "T,%.1f,%u,%.1f,%u,%u,%u,%u\n",
            static_cast<double>(f.temperature_c),
            static_cast<unsigned>(f.light_level),
            static_cast<double>(f.sound_db),
            static_cast<unsigned>(f.comfort_score),
            f.is_too_noisy ? 1u : 0u,
            f.is_muted ? 1u : 0u,
            static_cast<unsigned>(f.mode));
        return (n > 0 && static_cast<size_t>(n) < max_len) ? static_cast<size_t>(n) : 0;
    }

    static bool decodeTelemetry(const char *line, TelemetryFrame &out) {
        if (!line || line[0] != 'T' || line[1] != ',') {
            return false;
        }
        char buf[96];
        strncpy(buf, line + 2, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char *save = nullptr;
        char *tok = strtok_r(buf, ",", &save);
        if (!tok) return false;
        out.temperature_c = static_cast<float>(atof(tok));

        tok = strtok_r(nullptr, ",", &save);
        if (!tok) return false;
        out.light_level = static_cast<uint16_t>(atoi(tok));

        tok = strtok_r(nullptr, ",", &save);
        if (!tok) return false;
        out.sound_db = static_cast<float>(atof(tok));

        tok = strtok_r(nullptr, ",", &save);
        if (!tok) return false;
        out.comfort_score = static_cast<uint8_t>(atoi(tok));

        tok = strtok_r(nullptr, ",", &save);
        if (!tok) return false;
        out.is_too_noisy = (atoi(tok) != 0);

        tok = strtok_r(nullptr, ",", &save);
        if (!tok) return false;
        out.is_muted = (atoi(tok) != 0);

        tok = strtok_r(nullptr, ",", &save);
        if (!tok) return false;
        out.mode = static_cast<GadgetModeId>(atoi(tok));
        return true;
    }

    static size_t encodeCommand(const ConfigCommand &c, char *out, size_t max_len) {
        int n = snprintf(
            out, max_len, "C,%.1f,%.1f,%u,%u\n",
            static_cast<double>(c.max_sound_db),
            static_cast<double>(c.ideal_temp_c),
            c.muted ? 1u : 0u,
            static_cast<unsigned>(c.mode));
        return (n > 0 && static_cast<size_t>(n) < max_len) ? static_cast<size_t>(n) : 0;
    }

    static bool decodeCommand(const char *line, ConfigCommand &out) {
        if (!line || line[0] != 'C' || line[1] != ',') {
            return false;
        }
        char buf[64];
        strncpy(buf, line + 2, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char *save = nullptr;
        char *tok = strtok_r(buf, ",", &save);
        if (!tok) return false;
        out.max_sound_db = static_cast<float>(atof(tok));

        tok = strtok_r(nullptr, ",", &save);
        if (!tok) return false;
        out.ideal_temp_c = static_cast<float>(atof(tok));

        tok = strtok_r(nullptr, ",", &save);
        if (!tok) return false;
        out.muted = (atoi(tok) != 0);

        tok = strtok_r(nullptr, ",", &save);
        if (!tok) return false;
        out.mode = static_cast<GadgetModeId>(atoi(tok));
        return true;
    }
};

} // namespace mcshal
