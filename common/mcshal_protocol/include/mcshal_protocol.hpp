#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mcshal {

enum class GadgetModeId : uint8_t {
    Comfyness = 0,
    SoundParty = 1,
    PomodoroFocus = 2,
    Costume = 3
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

enum class GadgetStateId : uint8_t {
    Idle = 0,
    Greeting = 1,
    Active = 2
};

struct TelemetryFrame {
    float temperature_c{22.0f};
    uint16_t light_level{300};
    float sound_db{45.0f};
    uint8_t comfort_score{100};
    bool is_too_noisy{false};
    bool is_muted{false};
    GadgetModeId mode{GadgetModeId::Comfyness};
    GadgetStateId state{GadgetStateId::Active};
};

struct ConfigCommand {
    float max_sound_db{70.0f};
    float ideal_temp_c{22.5f};
    bool muted{false};
    GadgetModeId mode{GadgetModeId::Comfyness};
};

// ---- Phase 5: living-room gadget state machine frames ----

enum class CostumeAnim : uint8_t {
    Static_ = 0,
    Pulse = 1,
    Sparkle = 2,
    Spin = 3
};

// 'D' — C3 -> CPX wall-clock + day-window policy sync
struct ClockSync {
    uint16_t minute_of_day{0};      // 0..1439 local time
    bool day_window_active{true};   // inside configured wake window?
};

// 'W' — C3 -> CPX wake/idle policy thresholds
struct WakePolicy {
    uint16_t light_on_threshold{250}; // light sensor raw value that counts as "room lit"
    uint16_t day_start_min{7 * 60};   // 07:00
    uint16_t day_end_min{23 * 60};    // 23:00
    uint16_t idle_timeout_min{5};     // inactivity timeout for Active -> Idle
};

// 'K' — C3 -> CPX costume palette: per-pixel colors + animation
struct CostumePalette {
    uint8_t px[10][3]{};    // [pixel][r,g,b]
    CostumeAnim anim{CostumeAnim::Static_};
    uint8_t anim_speed{5};  // 1..10
};

// 'M' — C3 -> CPX melody chunk: notes appended at an absolute index
struct MelodyChunk {
    static constexpr uint8_t kMaxNotesPerChunk = 8;
    static constexpr uint8_t kMaxNotes = 64;
    uint16_t total{0};      // total notes in the melody
    uint8_t index{0};       // first note index carried by this chunk
    uint8_t count{0};       // notes in this chunk
    uint16_t freq_hz[kMaxNotesPerChunk]{};
    uint16_t dur_ms[kMaxNotesPerChunk]{};
};

// 'R' — C3 -> CPX remote/IR key
enum class IrKeyId : uint8_t {
    Power = 0,
    Up = 1,
    Down = 2,
    Left = 3,
    Right = 4,
    Ok = 5,
    Back = 6
};

class ProtocolCodec {
public:
    static size_t encodeTelemetry(const TelemetryFrame &f, char *out, size_t max_len) {
        int n = snprintf(
            out, max_len, "T,%.1f,%u,%.1f,%u,%u,%u,%u,%u\n",
            static_cast<double>(f.temperature_c),
            static_cast<unsigned>(f.light_level),
            static_cast<double>(f.sound_db),
            static_cast<unsigned>(f.comfort_score),
            f.is_too_noisy ? 1u : 0u,
            f.is_muted ? 1u : 0u,
            static_cast<unsigned>(f.mode),
            static_cast<unsigned>(f.state));
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

        // 'state' field was appended in Phase 5 — tolerate older CPX builds without it
        tok = strtok_r(nullptr, ",", &save);
        if (tok) out.state = static_cast<GadgetStateId>(atoi(tok));
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

    // ---- Phase 5 frames ----

    // D,<minute_of_day>,<day_window_active>
    static size_t encodeClock(const ClockSync &c, char *out, size_t max_len) {
        int n = snprintf(out, max_len, "D,%u,%u\n",
                         static_cast<unsigned>(c.minute_of_day),
                         c.day_window_active ? 1u : 0u);
        return (n > 0 && static_cast<size_t>(n) < max_len) ? static_cast<size_t>(n) : 0;
    }

    static bool decodeClock(const char *line, ClockSync &out) {
        char buf[24];
        char *save = nullptr;
        char *tok = tokenizePayload(line, 'D', buf, sizeof(buf), save);
        if (!tok) return false;
        out.minute_of_day = static_cast<uint16_t>(atoi(tok));
        tok = strtok_r(nullptr, ",", &save);
        if (!tok) return false;
        out.day_window_active = (atoi(tok) != 0);
        return true;
    }

    // W,<light_on_threshold>,<day_start_min>,<day_end_min>,<idle_timeout_min>
    static size_t encodePolicy(const WakePolicy &w, char *out, size_t max_len) {
        int n = snprintf(out, max_len, "W,%u,%u,%u,%u\n",
                         static_cast<unsigned>(w.light_on_threshold),
                         static_cast<unsigned>(w.day_start_min),
                         static_cast<unsigned>(w.day_end_min),
                         static_cast<unsigned>(w.idle_timeout_min));
        return (n > 0 && static_cast<size_t>(n) < max_len) ? static_cast<size_t>(n) : 0;
    }

    static bool decodePolicy(const char *line, WakePolicy &out) {
        char buf[40];
        char *save = nullptr;
        char *tok = tokenizePayload(line, 'W', buf, sizeof(buf), save);
        if (!tok) return false;
        out.light_on_threshold = static_cast<uint16_t>(atoi(tok));
        for (int i = 0; i < 3; ++i) {
            tok = strtok_r(nullptr, ",", &save);
            if (!tok) return false;
            uint16_t *fields[] = {&out.day_start_min, &out.day_end_min, &out.idle_timeout_min};
            *fields[i] = static_cast<uint16_t>(atoi(tok));
        }
        return true;
    }

    // K,<anim>,<speed>,<r0>,<g0>,<b0>,...,<r9>,<g9>,<b9>  (30 channel ints)
    static size_t encodeCostume(const CostumePalette &k, char *out, size_t max_len) {
        int n = snprintf(out, max_len, "K,%u,%u",
                         static_cast<unsigned>(k.anim),
                         static_cast<unsigned>(k.anim_speed));
        if (n <= 0 || static_cast<size_t>(n) >= max_len) return 0;
        size_t pos = static_cast<size_t>(n);
        for (uint8_t i = 0; i < 10; ++i) {
            n = snprintf(out + pos, max_len - pos, ",%u,%u,%u",
                         static_cast<unsigned>(k.px[i][0]),
                         static_cast<unsigned>(k.px[i][1]),
                         static_cast<unsigned>(k.px[i][2]));
            if (n <= 0 || static_cast<size_t>(n) >= max_len - pos) return 0;
            pos += static_cast<size_t>(n);
        }
        if (pos + 1 >= max_len) return 0;
        out[pos++] = '\n';
        out[pos] = '\0';
        return pos;
    }

    static bool decodeCostume(const char *line, CostumePalette &out) {
        char buf[192];
        char *save = nullptr;
        char *tok = tokenizePayload(line, 'K', buf, sizeof(buf), save);
        if (!tok) return false;
        out.anim = static_cast<CostumeAnim>(atoi(tok));
        tok = strtok_r(nullptr, ",", &save);
        if (!tok) return false;
        out.anim_speed = static_cast<uint8_t>(atoi(tok));
        for (uint8_t i = 0; i < 10; ++i) {
            for (uint8_t ch = 0; ch < 3; ++ch) {
                tok = strtok_r(nullptr, ",", &save);
                if (!tok) return false;
                out.px[i][ch] = static_cast<uint8_t>(atoi(tok));
            }
        }
        return true;
    }

    // M,<total>,<index>,<f0>,<d0>,<f1>,<d1>,...
    static size_t encodeMelodyChunk(const MelodyChunk &m, char *out, size_t max_len) {
        int n = snprintf(out, max_len, "M,%u,%u",
                         static_cast<unsigned>(m.total),
                         static_cast<unsigned>(m.index));
        if (n <= 0 || static_cast<size_t>(n) >= max_len) return 0;
        size_t pos = static_cast<size_t>(n);
        for (uint8_t i = 0; i < m.count && i < MelodyChunk::kMaxNotesPerChunk; ++i) {
            n = snprintf(out + pos, max_len - pos, ",%u,%u",
                         static_cast<unsigned>(m.freq_hz[i]),
                         static_cast<unsigned>(m.dur_ms[i]));
            if (n <= 0 || static_cast<size_t>(n) >= max_len - pos) return 0;
            pos += static_cast<size_t>(n);
        }
        if (pos + 1 >= max_len) return 0;
        out[pos++] = '\n';
        out[pos] = '\0';
        return pos;
    }

    static bool decodeMelodyChunk(const char *line, MelodyChunk &out) {
        char buf[128];
        char *save = nullptr;
        char *tok = tokenizePayload(line, 'M', buf, sizeof(buf), save);
        if (!tok) return false;
        out.total = static_cast<uint16_t>(atoi(tok));
        tok = strtok_r(nullptr, ",", &save);
        if (!tok) return false;
        out.index = static_cast<uint8_t>(atoi(tok));
        out.count = 0;
        while (out.count < MelodyChunk::kMaxNotesPerChunk) {
            tok = strtok_r(nullptr, ",", &save);
            if (!tok) break;
            out.freq_hz[out.count] = static_cast<uint16_t>(atoi(tok));
            tok = strtok_r(nullptr, ",", &save);
            if (!tok) return false;
            out.dur_ms[out.count] = static_cast<uint16_t>(atoi(tok));
            out.count++;
        }
        return out.count > 0;
    }

    // R,<key_id>
    static size_t encodeRemote(IrKeyId key, char *out, size_t max_len) {
        int n = snprintf(out, max_len, "R,%u\n", static_cast<unsigned>(key));
        return (n > 0 && static_cast<size_t>(n) < max_len) ? static_cast<size_t>(n) : 0;
    }

    static bool decodeRemote(const char *line, IrKeyId &out) {
        char buf[12];
        char *save = nullptr;
        char *tok = tokenizePayload(line, 'R', buf, sizeof(buf), save);
        if (!tok) return false;
        out = static_cast<IrKeyId>(atoi(tok));
        return true;
    }

private:
    // Copies "payload after 'X,'" into buf and returns the first strtok token.
    static char *tokenizePayload(const char *line, char frame_id,
                                 char *buf, size_t buf_len, char *&save) {
        if (!line || line[0] != frame_id || line[1] != ',') return nullptr;
        strncpy(buf, line + 2, buf_len - 1);
        buf[buf_len - 1] = '\0';
        return strtok_r(buf, ",", &save);
    }
};

} // namespace mcshal
