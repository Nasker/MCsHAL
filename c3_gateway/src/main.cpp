#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#include "hal/uart_cpx_bridge.hpp"
#include "net/gateway_web_server.hpp"
#include "wifi_secrets.h"

static mcshal::UartCpxBridge g_cpx_bridge(Serial1, 21, 20, CPX_UART_BAUD);
static mcshal::GatewayWebServer g_web_server(g_cpx_bridge, 80);
static bool g_logged_sta_ip = false;

static void setupWifi() {
    Preferences prefs;
    prefs.begin("wifi_cfg", true);
    String saved_ssid = prefs.getString("ssid", "");
    String saved_pass = prefs.getString("pass", "");
    prefs.end();

    String ssid = saved_ssid.length() > 0 ? saved_ssid : String(WIFI_SSID);
    String pass = saved_ssid.length() > 0 ? saved_pass : String(WIFI_PASS);
    Serial.printf("Wi-Fi cfg: %s (NVS ssid='%s')\n",
                  saved_ssid.length() > 0 ? "using saved NVS credentials" : "using compiled-in credentials",
                  saved_ssid.c_str());

    WiFi.mode(WIFI_AP_STA);
    // The ESP32-C3 SuperMini's cramped PCB antenna has no keep-out clearance; at the
    // default 20 dBm the extra power turns into distortion instead of radiation.
    // ~11 dBm is the community-proven sweet spot for these boards.
    WiFi.setTxPower(WIFI_POWER_11dBm);
    // Quick RF sanity check: if the scan finds our own router/neighbours, the
    // radio's RX path works (distinguishes 'wrong password' from 'dead radio').
    int found = WiFi.scanNetworks(false, false, false, 200U);
    Serial.printf("Wi-Fi scan: %d networks visible\n", found);
    for (int i = 0; i < found && i < 8; ++i) {
        Serial.printf("  - '%s' rssi=%d ch=%d\n",
                      WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
    }
    WiFi.scanDelete();

    bool ap_ok = WiFi.softAP(SOFTAP_SSID, nullptr, 6);
    Serial.printf("SoftAP '%s' %s at http://%s/ channel=%d\n",
                  SOFTAP_SSID, ap_ok ? "active" : "FAILED",
                  WiFi.softAPIP().toString().c_str(), WiFi.channel());

    bool have_pass = pass.length() > 0 && pass != "YOUR_WIFI_PASS";
    if (ssid.length() > 0 && ssid != "YOUR_WIFI_SSID" && have_pass) {
        Serial.printf("Connecting to Home Wi-Fi '%s'...\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());
    } else {
        // Do not start STA retries with a placeholder/empty password: the constant
        // STA scans would suppress SoftAP beacons and hide 'MCsHAL-Gateway'.
        Serial.println("No Wi-Fi password set - staying on SoftAP. Configure Wi-Fi at http://192.168.4.1/");
    }
}

static void updateStatusLed(uint32_t now_ms) {
    static uint32_t last_toggle_ms = 0;
    static bool led_on = false;

    if (WiFi.status() == WL_CONNECTED) {
        if (!g_logged_sta_ip) {
            g_logged_sta_ip = true;
            Serial.printf("================================================\n");
            Serial.printf("Connected to Wi-Fi! Open http://%s/\n", WiFi.localIP().toString().c_str());
            Serial.printf("================================================\n");
        }
        // Solid ON (active-LOW) when connected to Home Wi-Fi
        digitalWrite(C3_STATUS_LED_PIN, LOW);
    } else {
        g_logged_sta_ip = false;
        // Heartbeat blink (500ms) while in SoftAP mode
        if (now_ms - last_toggle_ms >= 500) {
            last_toggle_ms = now_ms;
            led_on = !led_on;
            digitalWrite(C3_STATUS_LED_PIN, led_on ? LOW : HIGH);
        }
    }
}

void setup() {
    pinMode(C3_STATUS_LED_PIN, OUTPUT);
    // First-light boot burst: 5 rapid blinks on GPIO 8 blue LED
    for (int i = 0; i < 5; ++i) {
        digitalWrite(C3_STATUS_LED_PIN, LOW);
        delay(70);
        digitalWrite(C3_STATUS_LED_PIN, HIGH);
        delay(70);
    }

    Serial.begin(115200);
    g_cpx_bridge.begin();
    setupWifi();
    g_web_server.begin();
    Serial.println("MCsHAL ESP32-C3 Gateway HTTP Server started on port 80");
}

static void printDiagnostics(uint32_t now_ms) {
    static uint32_t last_diag_ms = 0;
    if (now_ms - last_diag_ms < 2000) return;
    last_diag_ms = now_ms;

    uint32_t age = g_cpx_bridge.lastFrameAgeMs(now_ms);
    mcshal::TelemetryFrame f = g_cpx_bridge.latestTelemetry();
    wifi_mode_t wmode = WiFi.getMode();
    String wifi_str = (WiFi.status() == WL_CONNECTED)
                          ? ("STA Connected -> http://" + WiFi.localIP().toString() + "/")
                          : ("SoftAP '" SOFTAP_SSID "' -> http://" + WiFi.softAPIP().toString() +
                             "/ mode=" + String(static_cast<int>(wmode)) +
                             " bssid=" + WiFi.softAPmacAddress() +
                             " stations=" + String(WiFi.softAPgetStationNum()));

    if (age < 3000) {
        Serial.printf("[DIAG] %s | CPX LINKED (RX=%d,TX=%d, frames=%lu): Temp=%.1fC Light=%u Sound=%.1fdB Score=%u\n",
                      wifi_str.c_str(),
                      g_cpx_bridge.activeRxPin(),
                      g_cpx_bridge.activeTxPin(),
                      static_cast<unsigned long>(g_cpx_bridge.frameCount()),
                      static_cast<double>(f.temperature_c),
                      static_cast<unsigned>(f.light_level),
                      static_cast<double>(f.sound_db),
                      static_cast<unsigned>(f.comfort_score));
    } else {
        // Temporarily detach Serial1 and put all candidate pins in INPUT_PULLUP for 600ms
        // so the UART TX output driver cannot fight/mask the incoming CPX signal.
        Serial1.end();
        const int test_pins[] = {20, 21, 8, 0, 1, 2, 3, 4, 5, 6, 7, 9, 10};
        int last_val[13] = {};
        int trans[13] = {};
        for (int i = 0; i < 13; ++i) {
            pinMode(test_pins[i], INPUT_PULLUP);
            last_val[i] = digitalRead(test_pins[i]);
        }
        uint32_t t0 = millis();
        while (millis() - t0 < 600) {
            for (int i = 0; i < 13; ++i) {
                int v = digitalRead(test_pins[i]);
                if (v != last_val[i]) {
                    trans[i]++;
                    last_val[i] = v;
                }
            }
        }
        g_cpx_bridge.begin();
        pinMode(C3_STATUS_LED_PIN, OUTPUT); // restore status LED after probing GPIO8
        Serial.printf("[DIAG] %s | No frames (RX=%d,TX=%d,raw=%lu) | Transitions(600ms): 20=%d(v=%d) 21=%d(v=%d) 8=%d(v=%d) 0=%d 1=%d 2=%d 3=%d 4=%d 5=%d 6=%d 7=%d 9=%d 10=%d\n",
                      wifi_str.c_str(),
                      g_cpx_bridge.activeRxPin(),
                      g_cpx_bridge.activeTxPin(),
                      static_cast<unsigned long>(g_cpx_bridge.rxBytes()),
                      trans[0], last_val[0],
                      trans[1], last_val[1],
                      trans[2], last_val[2],
                      trans[3], trans[4], trans[5], trans[6], trans[7],
                      trans[8], trans[9], trans[10], trans[11], trans[12]);
    }
}

// Any STA attempt (boot or web-UI '/api/wifi') gets a bounded 20 s window.
// If it doesn't associate, STA is shut down so its constant channel scans
// stop suppressing the 'MCsHAL-Gateway' SoftAP beacons.
static void maintainWiFi(uint32_t now_ms) {
    static uint32_t disconnected_since = 0;
    bool sta_on = (WiFi.getMode() & WIFI_STA) != 0;
    if (sta_on && WiFi.status() != WL_CONNECTED) {
        if (disconnected_since == 0) {
            disconnected_since = now_ms;
        } else if (now_ms - disconnected_since > 20000) {
            disconnected_since = 0;
            WiFi.disconnect(false);
            WiFi.mode(WIFI_AP);
            // Re-assert the SoftAP after the AP_STA -> AP transition; on some
            // core versions the AP interface does not resume beaconing otherwise.
            WiFi.softAP(SOFTAP_SSID, nullptr, 6);
            Serial.println("Home Wi-Fi attempt timed out - STA off, SoftAP stays clean");
        }
    } else {
        disconnected_since = 0;
    }
}

void loop() {
    uint32_t now = millis();
    g_cpx_bridge.poll(now);
    g_web_server.handleClient();
    updateStatusLed(now);
    maintainWiFi(now);
    printDiagnostics(now);
}
