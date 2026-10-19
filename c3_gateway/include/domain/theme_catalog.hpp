#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>
#include "net/time_service.hpp"
#include "mcshal_protocol.hpp"

namespace mcshal {

// A seasonal theme: NeoPixel "costume" + melody, parsed from /themes/<name>.json.
//
// Explicit JSON format (kept dumb on purpose so a person or an LLM can edit it):
// {
//   "name": "halloween",
//   "season": { "from": "10-15", "to": "11-05" },   // MM-DD inclusive, wrap-around ok
//   "costume": {
//     "animation": "pulse",                        // static | pulse | sparkle | spin
//     "anim_speed": 5,                             // 1..10
//     "pixels": [ {"r":0,"g":140,"b":0}, ...x10 ]  // index 0..9 around the ring
//   },
//   "melody": { "notes": [ {"f":440,"d":300}, ... ] }  // f Hz (0 = rest), d ms
// }
struct Theme {
    String name;
    String season_from;   // "MM-DD" or empty = no season window
    String season_to;
    CostumePalette costume;
    std::vector<uint16_t> melody_freq;
    std::vector<uint16_t> melody_dur;
};

class ThemeCatalog {
public:
    bool begin() {
        if (!LittleFS.begin(true)) { // formatOnFail
            Serial.println("ThemeCatalog: LittleFS mount FAILED");
            return false;
        }
        if (!LittleFS.exists("/themes")) LittleFS.mkdir("/themes");
        Serial.printf("ThemeCatalog: LittleFS mounted (%u KB total)\n",
                      static_cast<unsigned>(LittleFS.totalBytes() / 1024));
        return true;
    }

    std::vector<String> list() const {
        std::vector<String> names;
        File dir = LittleFS.open("/themes");
        if (!dir) return names;
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            String n = f.name();
            if (n.endsWith(".json")) {
                n = n.substring(0, n.length() - 5);
                names.push_back(n);
            }
        }
        return names;
    }

    bool load(const String &name, Theme &out) const {
        String path = "/themes/" + name + ".json";
        File f = LittleFS.open(path, "r");
        if (!f) return false;
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err) return false;

        out = Theme{};
        out.name = doc["name"] | name;
        out.season_from = doc["season"]["from"] | "";
        out.season_to = doc["season"]["to"] | "";

        JsonObject costume = doc["costume"];
        out.costume.anim = animFromString(costume["animation"] | "static");
        out.costume.anim_speed = static_cast<uint8_t>(costume["anim_speed"] | 5);
        JsonArray px = costume["pixels"];
        for (uint8_t i = 0; i < 10 && i < px.size(); ++i) {
            out.costume.px[i][0] = px[i]["r"] | 0;
            out.costume.px[i][1] = px[i]["g"] | 0;
            out.costume.px[i][2] = px[i]["b"] | 0;
        }

        JsonArray notes = doc["melody"]["notes"];
        out.melody_freq.reserve(notes.size());
        out.melody_dur.reserve(notes.size());
        for (JsonObject n : notes) {
            out.melody_freq.push_back(n["f"] | 0);
            out.melody_dur.push_back(n["d"] | 200);
        }
        return true;
    }

    bool save(const String &name, const String &json, String &err) const {
        JsonDocument doc;
        if (deserializeJson(doc, json)) {
            err = "invalid JSON";
            return false;
        }
        if (!doc["costume"]["pixels"].is<JsonArray>() || !doc["melody"]["notes"].is<JsonArray>()) {
            err = "missing costume.pixels[] or melody.notes[]";
            return false;
        }
        File f = LittleFS.open("/themes/" + name + ".json", "w");
        if (!f) {
            err = "cannot write file";
            return false;
        }
        f.print(json);
        f.close();
        return true;
    }

    // Pick the theme whose season window contains today; falls back to "default".
    String selectFor(uint8_t month, uint8_t day) const {
        for (const String &name : list()) {
            if (name == "default") continue;
            Theme t;
            if (load(name, t) && t.season_from.length() == 5 && t.season_to.length() == 5 &&
                TimeService::dateInWindow(month, day, t.season_from.c_str(), t.season_to.c_str())) {
                return name;
            }
        }
        return "default";
    }

private:
    static CostumeAnim animFromString(const char *s) {
        if (strcmp(s, "pulse") == 0) return CostumeAnim::Pulse;
        if (strcmp(s, "sparkle") == 0) return CostumeAnim::Sparkle;
        if (strcmp(s, "spin") == 0) return CostumeAnim::Spin;
        return CostumeAnim::Static_;
    }
};

} // namespace mcshal
