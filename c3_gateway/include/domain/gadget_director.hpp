#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "domain/theme_catalog.hpp"
#include "interfaces/i_cpx_bridge.hpp"
#include "net/time_service.hpp"

namespace mcshal {

// Policy-side brain on the C3: decides WHEN to push what to the CPX.
// - On CPX link (re)connect: W policy + D clock + K costume + M melody chunks.
// - Re-pushes D every minute so the day-window flag stays current.
// - Season/theme selection uses NTP time; without internet it keeps whatever
//   theme was last applied (or "default").
class GadgetDirector {
public:
    GadgetDirector(ICpxBridge &bridge, ThemeCatalog &catalog, TimeService &time)
        : bridge_(bridge), catalog_(catalog), time_(time) {
        // Wake policy persisted in NVS so web edits survive reboots
        Preferences prefs;
        prefs.begin("gadget", true);
        policy_.light_on_threshold = prefs.getUShort("light", policy_.light_on_threshold);
        policy_.day_start_min = prefs.getUShort("day0", policy_.day_start_min);
        policy_.day_end_min = prefs.getUShort("day1", policy_.day_end_min);
        policy_.idle_timeout_min = prefs.getUShort("idle", policy_.idle_timeout_min);
        active_theme_ = prefs.getString("theme", "");
        prefs.end();
    }

    const WakePolicy &policy() const { return policy_; }
    // What the CPX is currently wearing: explicit pick if set, else last pushed.
    const String &activeTheme() const {
        return active_theme_.length() > 0 ? active_theme_ : last_pushed_theme_;
    }

    void setPolicy(const WakePolicy &w) {
        policy_ = w;
        Preferences prefs;
        prefs.begin("gadget", false);
        prefs.putUShort("light", w.light_on_threshold);
        prefs.putUShort("day0", w.day_start_min);
        prefs.putUShort("day1", w.day_end_min);
        prefs.putUShort("idle", w.idle_timeout_min);
        prefs.end();
        bridge_.sendPolicy(policy_);
    }

    void tick(uint32_t now_ms) {
        // Idle heartbeats arrive every ~10 s — tolerate the gaps so the link
        // doesn't flap between idle samples and spam theme re-pushes.
        bool linked = bridge_.lastFrameAgeMs(now_ms) < 15000;
        if (linked && !was_linked_) {
            Serial.println("GadgetDirector: CPX link up - pushing policy/clock/theme");
            pushAll(now_ms);
        }
        was_linked_ = linked;

        // Refresh the clock window flag once a minute
        if (linked && now_ms - last_clock_ms_ >= 60000) {
            last_clock_ms_ = now_ms;
            pushClock();
        }
    }

    void pushAll(uint32_t now_ms) {
        (void)now_ms;
        bridge_.sendPolicy(policy_);
        pushClock();
        pushTheme(activeThemeOrSeason());
    }

    void pushClock() {
        ClockSync clk;
        uint16_t mod;
        if (time_.minuteOfDay(mod)) {
            clk.minute_of_day = mod;
            clk.day_window_active = inDayWindow(mod);
        } else {
            clk.day_window_active = true; // standalone: always inside window
        }
        bridge_.sendClock(clk);
    }

    // Resolve: explicit theme name if set, else pick by today's season window.
    String activeThemeOrSeason() {
        if (active_theme_.length() > 0) return active_theme_;
        uint8_t m, d;
        if (time_.dateMd(m, d)) {
            return catalog_.selectFor(m, d);
        }
        return "default";
    }

    bool applyTheme(const String &name) {
        active_theme_ = name;
        Preferences prefs;
        prefs.begin("gadget", false);
        prefs.putString("theme", name);
        prefs.end();
        return pushTheme(name);
    }

    // Streams the theme to the CPX: K costume frame, then M melody chunks.
    bool pushTheme(const String &name) {
        Theme t;
        if (!catalog_.load(name, t)) {
            Serial.printf("GadgetDirector: theme '%s' not found\n", name.c_str());
            return false;
        }
        bridge_.sendCostume(t.costume);
        last_pushed_theme_ = t.name;

        uint16_t total = static_cast<uint16_t>(t.melody_freq.size());
        if (total > MelodyChunk::kMaxNotes) total = MelodyChunk::kMaxNotes;
        for (uint16_t base = 0; base < total; base += MelodyChunk::kMaxNotesPerChunk) {
            MelodyChunk chunk;
            chunk.total = total;
            chunk.index = static_cast<uint8_t>(base);
            chunk.count = 0;
            for (uint8_t i = 0; i < MelodyChunk::kMaxNotesPerChunk && base + i < total; ++i) {
                chunk.freq_hz[i] = t.melody_freq[base + i];
                chunk.dur_ms[i] = t.melody_dur[base + i];
                chunk.count++;
            }
            bridge_.sendMelodyChunk(chunk);
        }
        Serial.printf("GadgetDirector: pushed theme '%s' (%u notes)\n",
                      name.c_str(), static_cast<unsigned>(total));
        return true;
    }

private:
    bool inDayWindow(uint16_t minute_of_day) const {
        uint16_t a = policy_.day_start_min, b = policy_.day_end_min;
        if (a <= b) return minute_of_day >= a && minute_of_day <= b;
        return minute_of_day >= a || minute_of_day <= b; // overnight window
    }

    ICpxBridge &bridge_;
    ThemeCatalog &catalog_;
    TimeService &time_;
    WakePolicy policy_{};
    String active_theme_;
    String last_pushed_theme_;
    bool was_linked_{false};
    uint32_t last_clock_ms_{0};
};

} // namespace mcshal
