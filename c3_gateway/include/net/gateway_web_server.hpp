#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include "interfaces/i_cpx_bridge.hpp"
#include "wifi_secrets.h"

namespace mcshal {

static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MCsHAL Living Room Comfort</title>
<style>
  :root { --bg:#0f172a; --card:#1e293b; --text:#f8fafc; --muted:#94a3b8; --accent:#38bdf8; }
  * { box-sizing:border-box; margin:0; padding:0; }
  body { font-family:system-ui,sans-serif; background:var(--bg); color:var(--text); min-height:100vh; display:flex; align-items:center; justify-content:center; padding:16px; }
  .card { background:var(--card); border-radius:20px; padding:24px; width:100%; max-width:460px; box-shadow:0 20px 50px rgba(0,0,0,0.5); }
  .header { display:flex; justify-content:space-between; align-items:center; margin-bottom:18px; }
  h1 { font-size:1.25rem; }
  .badge { font-size:0.75rem; padding:4px 10px; border-radius:999px; background:rgba(56,189,248,0.15); color:var(--accent); font-weight:600; }
  .score-box { text-align:center; padding:20px; border-radius:16px; background:#0f172a; margin-bottom:16px; }
  .score { font-size:3.2rem; font-weight:800; color:#4ade80; }
  .grid { display:grid; grid-template-columns:repeat(3,1fr); gap:10px; margin-bottom:18px; }
  .metric { background:#0f172a; padding:12px; border-radius:12px; text-align:center; }
  .metric .label { font-size:0.75rem; color:var(--muted); }
  .metric .val { font-size:1.15rem; font-weight:700; margin-top:4px; }
  .controls { display:flex; flex-direction:column; gap:10px; border-top:1px solid rgba(255,255,255,0.08); padding-top:14px; }
  label { display:flex; justify-content:space-between; align-items:center; font-size:0.85rem; color:var(--muted); }
  select, input, button { padding:8px 10px; border-radius:8px; border:1px solid rgba(255,255,255,0.15); background:#0f172a; color:var(--text); }
  button { background:var(--accent); color:#0f172a; font-weight:700; border:none; cursor:pointer; margin-top:4px; }
  .btn-secondary { background:#334155; color:var(--text); }
  details { border-top:1px solid rgba(255,255,255,0.08); margin-top:14px; padding-top:12px; font-size:0.85rem; color:var(--muted); }
  summary { cursor:pointer; font-weight:600; margin-bottom:8px; }
  .wifi-form { display:flex; flex-direction:column; gap:8px; }
</style>
</head>
<body>
<div class="card">
  <div class="header">
    <h1>MCsHAL Living Room</h1>
    <span class="badge" id="link">Connecting...</span>
  </div>
  <div class="score-box">
    <div style="font-size:0.8rem;color:var(--muted)">COMFYNESS SCORE</div>
    <div class="score" id="score">--</div>
    <div id="alert" style="font-size:0.85rem;color:#94a3b8">Waiting for CPX UART link...</div>
  </div>
  <div class="grid">
    <div class="metric"><div class="label">Temp</div><div class="val" id="temp">-- °C</div></div>
    <div class="metric"><div class="label">Light</div><div class="val" id="light">--</div></div>
    <div class="metric"><div class="label">Noise</div><div class="val" id="sound">-- dB</div></div>
  </div>
  <form class="controls" onsubmit="applyCfg(event)">
    <label>Gadget Mode
      <select id="mode">
        <option value="0">Comfyness Monitor (MVP)</option>
        <option value="1">Sound Party Spectrum (Sandbox)</option>
        <option value="2">Pomodoro Rainbow (Sandbox)</option>
      </select>
    </label>
    <label>Ideal Temp (°C) <input id="ideal_temp" type="number" step="0.5" value="22.5" style="width:90px"></label>
    <label>Noise Alert Threshold (dB) <input id="max_db" type="number" step="1" value="82" style="width:90px"></label>
    <label>Mute Speaker Alerts <input id="muted" type="checkbox"></label>
    <button type="submit">Send Config to CPX</button>
    <button type="button" class="btn-secondary" onclick="blinkC3()">Blink C3 SuperMini LED (First Light Test)</button>
  </form>

  <details>
    <summary>ESP32-C3 Wi-Fi Settings <span id="ip-info"></span></summary>
    <form class="wifi-form" onsubmit="saveWifi(event)">
      <input id="ssid" placeholder="Home Wi-Fi SSID (2.4 GHz)" required>
      <input id="pass" type="password" placeholder="Wi-Fi Password">
      <button type="submit">Save & Connect to Home Wi-Fi</button>
      <div id="wifi-msg"></div>
    </form>
  </details>
</div>
<script>
async function poll() {
  try {
    const r = await fetch('/api/telemetry');
    const d = await r.json();
    const online = d.age_ms < 3000;
    document.getElementById('link').textContent = online ? 'CPX Linked (UART)' : 'CPX Unplugged (C3 Ready)';
    if (online) {
      document.getElementById('score').textContent = d.score;
      document.getElementById('temp').textContent = d.temp.toFixed(1) + ' °C';
      document.getElementById('light').textContent = d.light;
      document.getElementById('sound').textContent = d.sound.toFixed(1) + ' dB';
      document.getElementById('alert').textContent = d.noisy ? 'TOO NOISY! Alerting...' : (d.muted ? 'Speaker Muted' : 'Quiet & Cozy');
      document.getElementById('alert').style.color = d.noisy ? '#f87171' : '#94a3b8';
    }
    if (d.ip) document.getElementById('ip-info').textContent = '(' + d.ip + ')';
  } catch (e) {}
}
async function applyCfg(e) {
  e.preventDefault();
  const q = new URLSearchParams({
    mode: document.getElementById('mode').value,
    ideal_temp: document.getElementById('ideal_temp').value,
    max_db: document.getElementById('max_db').value,
    muted: document.getElementById('muted').checked ? '1' : '0'
  });
  await fetch('/api/config?' + q.toString(), { method: 'POST' });
}
async function blinkC3() {
  await fetch('/api/blink', { method: 'POST' });
}
async function saveWifi(e) {
  e.preventDefault();
  const ssid = document.getElementById('ssid').value;
  const pass = document.getElementById('pass').value;
  document.getElementById('wifi-msg').textContent = 'Connecting... check serial monitor for new IP.';
  const q = new URLSearchParams({ ssid, pass });
  await fetch('/api/wifi?' + q.toString(), { method: 'POST' });
}
setInterval(poll, 750);
poll();
</script>
</body>
</html>
)rawliteral";

class GatewayWebServer {
public:
    GatewayWebServer(ICpxBridge &bridge, uint16_t port = 80)
        : bridge_(bridge), server_(port) {}

    void begin() {
        server_.on("/", HTTP_GET, [this]() {
            server_.send_P(200, "text/html", DASHBOARD_HTML);
        });

        server_.on("/api/telemetry", HTTP_GET, [this]() {
            TelemetryFrame f = bridge_.latestTelemetry();
            uint32_t age = bridge_.lastFrameAgeMs(millis());
            String ip = (WiFi.status() == WL_CONNECTED)
                            ? WiFi.localIP().toString()
                            : WiFi.softAPIP().toString();
            char json[240];
            snprintf(json, sizeof(json),
                     "{\"temp\":%.1f,\"light\":%u,\"sound\":%.1f,\"score\":%u,"
                     "\"noisy\":%s,\"muted\":%s,\"mode\":%u,\"age_ms\":%lu,\"ip\":\"%s\"}",
                     static_cast<double>(f.temperature_c),
                     static_cast<unsigned>(f.light_level),
                     static_cast<double>(f.sound_db),
                     static_cast<unsigned>(f.comfort_score),
                     f.is_too_noisy ? "true" : "false",
                     f.is_muted ? "true" : "false",
                     static_cast<unsigned>(f.mode),
                     static_cast<unsigned long>(age),
                     ip.c_str());
            server_.send(200, "application/json", json);
        });

        server_.on("/api/config", HTTP_POST, [this]() {
            ConfigCommand cmd = last_cmd_;
            if (server_.hasArg("max_db")) cmd.max_sound_db = server_.arg("max_db").toFloat();
            if (server_.hasArg("ideal_temp")) cmd.ideal_temp_c = server_.arg("ideal_temp").toFloat();
            if (server_.hasArg("muted")) cmd.muted = (server_.arg("muted").toInt() != 0);
            if (server_.hasArg("mode")) {
                cmd.mode = static_cast<GadgetModeId>(server_.arg("mode").toInt());
            }
            last_cmd_ = cmd;
            bridge_.sendCommand(cmd);
            server_.send(200, "text/plain", "OK");
        });

        server_.on("/api/blink", HTTP_POST, [this]() {
            for (int i = 0; i < 6; ++i) {
                digitalWrite(C3_STATUS_LED_PIN, LOW);  // Active-LOW ON
                delay(60);
                digitalWrite(C3_STATUS_LED_PIN, HIGH); // Active-LOW OFF
                delay(60);
            }
            server_.send(200, "text/plain", "BLINKED");
        });

        server_.on("/api/wifi", HTTP_POST, [this]() {
            String ssid = server_.arg("ssid");
            String pass = server_.arg("pass");
            server_.send(200, "text/plain", "CONNECTING");
            if (ssid.length() > 0) {
                Preferences prefs;
                prefs.begin("wifi_cfg", false);
                prefs.putString("ssid", ssid);
                prefs.putString("pass", pass);
                prefs.end();
                WiFi.mode(WIFI_AP_STA);
                WiFi.begin(ssid.c_str(), pass.c_str());
            }
        });

        server_.begin();
    }

    void handleClient() { server_.handleClient(); }

private:
    ICpxBridge &bridge_;
    WebServer server_;
    ConfigCommand last_cmd_{};
};

} // namespace mcshal
