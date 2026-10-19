#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include "domain/gadget_director.hpp"
#include "interfaces/i_cpx_bridge.hpp"
#include "wifi_secrets.h"

namespace mcshal {

static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MCsHAL Living Room</title>
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
  label { display:flex; justify-content:space-between; align-items:center; font-size:0.85rem; color:var(--muted); gap:8px; }
  select, input, button, textarea { padding:8px 10px; border-radius:8px; border:1px solid rgba(255,255,255,0.15); background:#0f172a; color:var(--text); }
  button { background:var(--accent); color:#0f172a; font-weight:700; border:none; cursor:pointer; }
  .btn-secondary { background:#334155; color:var(--text); }
  .remote { border-top:1px solid rgba(255,255,255,0.08); padding-top:14px; margin-bottom:14px; }
  .remote h2 { font-size:0.95rem; margin-bottom:10px; color:var(--muted); text-align:center; }
  .dpad { display:grid; grid-template-columns:repeat(3,64px); grid-template-rows:repeat(3,52px); gap:8px; justify-content:center; margin-bottom:8px; }
  .dpad button { font-size:1.15rem; }
  .remote-row { display:flex; gap:8px; justify-content:center; }
  .remote-row button { flex:0 0 auto; min-width:80px; }
  details { border-top:1px solid rgba(255,255,255,0.08); margin-top:14px; padding-top:12px; font-size:0.85rem; color:var(--muted); }
  summary { cursor:pointer; font-weight:600; margin-bottom:8px; }
  .wifi-form, .theme-form, .policy-form { display:flex; flex-direction:column; gap:8px; }
  textarea { font-family:ui-monospace,monospace; font-size:0.72rem; min-height:180px; white-space:pre; }
  .msg { font-size:0.8rem; min-height:1em; }
</style>
</head>
<body>
<div class="card">
  <div class="header">
    <h1>MCsHAL Living Room</h1>
    <span class="badge" id="link">Connecting...</span>
  </div>
  <div class="score-box">
    <div style="font-size:0.8rem;color:var(--muted)">STATE: <b id="state">--</b></div>
    <div class="score" id="score">--</div>
    <div id="alert" style="font-size:0.85rem;color:#94a3b8">Waiting for CPX UART link...</div>
  </div>
  <div class="grid">
    <div class="metric"><div class="label">Temp</div><div class="val" id="temp">-- °C</div></div>
    <div class="metric"><div class="label">Light</div><div class="val" id="light">--</div></div>
    <div class="metric"><div class="label">Noise</div><div class="val" id="sound">-- dB</div></div>
  </div>

  <div class="remote">
    <h2>TV Remote (LG / IR)</h2>
    <div class="dpad">
      <span></span><button onclick="tv('up')">&#9650;</button><span></span>
      <button onclick="tv('left')">&#9664;</button><button onclick="tv('ok')">OK</button><button onclick="tv('right')">&#9654;</button>
      <span></span><button onclick="tv('down')">&#9660;</button><span></span>
    </div>
    <div class="remote-row">
      <button onclick="tv('power')" style="background:#f87171">&#x23FB; Power</button>
      <button class="btn-secondary" onclick="tv('back')">&#8617; Back</button>
      <button class="btn-secondary" onclick="greet()">&#10024; Greet</button>
    </div>
  </div>

  <form class="controls" onsubmit="applyCfg(event)">
    <label>Gadget Mode
      <select id="mode">
        <option value="3">Seasonal Costume</option>
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
    <summary>Seasonal Theme (costume + tune)</summary>
    <div class="theme-form">
      <label>Theme <select id="theme" style="width:200px"></select></label>
      <div class="remote-row">
        <button type="button" onclick="applyTheme()">Apply &amp; Greet</button>
        <button type="button" class="btn-secondary" onclick="loadTheme()">Load JSON</button>
        <button type="button" class="btn-secondary" onclick="saveTheme()">Save JSON</button>
      </div>
      <textarea id="theme_json" spellcheck="false" placeholder='{"name":"mytheme","season":{"from":"10-15","to":"11-05"},"costume":{"animation":"pulse","anim_speed":5,"pixels":[{"r":0,"g":0,"b":0}]},"melody":{"notes":[{"f":440,"d":300}]}}'></textarea>
      <div class="msg" id="theme-msg"></div>
    </div>
  </details>

  <details>
    <summary>Wake / Sleep Policy</summary>
    <form class="policy-form" onsubmit="savePolicy(event)">
      <label>Light-on threshold <input id="pol_light" type="number" min="0" max="1023" style="width:90px"></label>
      <label>Day window start <input id="pol_day0" type="time" value="07:00" style="width:110px"></label>
      <label>Day window end <input id="pol_day1" type="time" value="23:00" style="width:110px"></label>
      <label>Idle timeout (min) <input id="pol_idle" type="number" min="1" max="60" value="5" style="width:90px"></label>
      <button type="submit">Save &amp; Push Policy</button>
      <div class="msg" id="policy-msg"></div>
    </form>
  </details>

  <details>
    <summary>ESP32-C3 Wi-Fi Settings <span id="ip-info"></span></summary>
    <form class="wifi-form" onsubmit="saveWifi(event)">
      <input id="ssid" placeholder="Home Wi-Fi SSID (2.4 GHz)" required>
      <input id="pass" type="password" placeholder="Wi-Fi Password">
      <button type="submit">Save &amp; Connect to Home Wi-Fi</button>
      <div class="msg" id="wifi-msg"></div>
    </form>
  </details>
</div>
<script>
const STATES = ['Idle','Greeting','Active'];
async function poll() {
  try {
    const r = await fetch('/api/telemetry');
    const d = await r.json();
    const online = d.age_ms < 15000;
    document.getElementById('link').textContent = online ? 'CPX Linked (UART)' : 'CPX Unplugged (C3 Ready)';
    if (online) {
      document.getElementById('state').textContent = STATES[d.state] || d.state;
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
async function tv(key) { await fetch('/api/tv?key=' + key, { method: 'POST' }); }
async function greet() { await fetch('/api/greet', { method: 'POST' }); }
async function blinkC3() { await fetch('/api/blink', { method: 'POST' }); }

async function refreshThemes() {
  try {
    const d = await (await fetch('/api/themes')).json();
    const sel = document.getElementById('theme');
    sel.innerHTML = '';
    for (const n of d.themes) {
      const o = document.createElement('option');
      o.value = n; o.textContent = n + (n === d.active ? ' (active)' : '');
      sel.appendChild(o);
    }
    if (d.active) sel.value = d.active;
  } catch (e) {}
}
async function applyTheme() {
  const n = document.getElementById('theme').value;
  await fetch('/api/apply_theme?name=' + encodeURIComponent(n), { method: 'POST' });
  document.getElementById('theme-msg').textContent = 'Applied ' + n;
  refreshThemes();
}
async function loadTheme() {
  const n = document.getElementById('theme').value;
  const r = await fetch('/api/theme?name=' + encodeURIComponent(n));
  document.getElementById('theme_json').value = await r.text();
}
async function saveTheme() {
  const n = document.getElementById('theme').value;
  const r = await fetch('/api/theme?name=' + encodeURIComponent(n), {
    method: 'POST', body: document.getElementById('theme_json').value
  });
  document.getElementById('theme-msg').textContent = await r.text();
  refreshThemes();
}
async function refreshPolicy() {
  try {
    const d = await (await fetch('/api/policy')).json();
    document.getElementById('pol_light').value = d.light_on_threshold;
    const fmt = m => String(Math.floor(m/60)).padStart(2,'0') + ':' + String(m%60).padStart(2,'0');
    document.getElementById('pol_day0').value = fmt(d.day_start_min);
    document.getElementById('pol_day1').value = fmt(d.day_end_min);
    document.getElementById('pol_idle').value = d.idle_timeout_min;
  } catch (e) {}
}
async function savePolicy(e) {
  e.preventDefault();
  const hm = s => { const [h,m] = s.split(':'); return (parseInt(h)||0)*60 + (parseInt(m)||0); };
  const q = new URLSearchParams({
    light: document.getElementById('pol_light').value,
    day_start: hm(document.getElementById('pol_day0').value),
    day_end: hm(document.getElementById('pol_day1').value),
    idle_min: document.getElementById('pol_idle').value
  });
  const r = await fetch('/api/policy?' + q.toString(), { method: 'POST' });
  document.getElementById('policy-msg').textContent = await r.text();
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
refreshThemes();
refreshPolicy();
</script>
</body>
</html>
)rawliteral";

class GatewayWebServer {
public:
    GatewayWebServer(ICpxBridge &bridge, GadgetDirector &director,
                     ThemeCatalog &catalog, uint16_t port = 80)
        : bridge_(bridge), director_(director), catalog_(catalog), server_(port) {}

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
            char json[256];
            snprintf(json, sizeof(json),
                     "{\"temp\":%.1f,\"light\":%u,\"sound\":%.1f,\"score\":%u,"
                     "\"noisy\":%s,\"muted\":%s,\"mode\":%u,\"state\":%u,\"age_ms\":%lu,\"ip\":\"%s\"}",
                     static_cast<double>(f.temperature_c),
                     static_cast<unsigned>(f.light_level),
                     static_cast<double>(f.sound_db),
                     static_cast<unsigned>(f.comfort_score),
                     f.is_too_noisy ? "true" : "false",
                     f.is_muted ? "true" : "false",
                     static_cast<unsigned>(f.mode),
                     static_cast<unsigned>(f.state),
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

        server_.on("/api/tv", HTTP_POST, [this]() {
            IrKeyId key;
            if (!parseKey(server_.arg("key"), key)) {
                server_.send(400, "text/plain", "unknown key");
                return;
            }
            bridge_.sendRemote(key);
            server_.send(200, "text/plain", "IR sent");
        });

        server_.on("/api/greet", HTTP_POST, [this]() {
            bridge_.sendGreet();
            server_.send(200, "text/plain", "Greeting requested");
        });

        server_.on("/api/themes", HTTP_GET, [this]() {
            String json = "{\"active\":\"" + director_.activeTheme() + "\",\"themes\":[";
            bool first = true;
            for (const String &n : catalog_.list()) {
                if (!first) json += ',';
                json += "\"" + n + "\"";
                first = false;
            }
            json += "]}";
            server_.send(200, "application/json", json);
        });

        server_.on("/api/theme", HTTP_GET, [this]() {
            Theme t;
            if (!catalog_.load(server_.arg("name"), t)) {
                server_.send(404, "text/plain", "theme not found");
                return;
            }
            // Stream the raw file back so edits round-trip verbatim
            File f = LittleFS.open("/themes/" + server_.arg("name") + ".json", "r");
            server_.streamFile(f, "application/json");
            f.close();
        });

        server_.on("/api/theme", HTTP_POST, [this]() {
            String name = server_.arg("name");
            if (!name.length()) {
                server_.send(400, "text/plain", "missing ?name=");
                return;
            }
            String err;
            if (!catalog_.save(name, server_.arg("plain"), err)) {
                server_.send(400, "text/plain", "save failed: " + err);
                return;
            }
            server_.send(200, "text/plain", "saved " + name);
        });

        server_.on("/api/apply_theme", HTTP_POST, [this]() {
            String name = server_.arg("name");
            if (!director_.applyTheme(name)) {
                server_.send(404, "text/plain", "theme not found");
                return;
            }
            bridge_.sendGreet(); // show it off immediately
            server_.send(200, "text/plain", "applied " + name);
        });

        server_.on("/api/policy", HTTP_GET, [this]() {
            const WakePolicy &w = director_.policy();
            char json[160];
            snprintf(json, sizeof(json),
                     "{\"light_on_threshold\":%u,\"day_start_min\":%u,\"day_end_min\":%u,\"idle_timeout_min\":%u}",
                     static_cast<unsigned>(w.light_on_threshold),
                     static_cast<unsigned>(w.day_start_min),
                     static_cast<unsigned>(w.day_end_min),
                     static_cast<unsigned>(w.idle_timeout_min));
            server_.send(200, "application/json", json);
        });

        server_.on("/api/policy", HTTP_POST, [this]() {
            WakePolicy w = director_.policy();
            if (server_.hasArg("light")) w.light_on_threshold = server_.arg("light").toInt();
            if (server_.hasArg("day_start")) w.day_start_min = server_.arg("day_start").toInt();
            if (server_.hasArg("day_end")) w.day_end_min = server_.arg("day_end").toInt();
            if (server_.hasArg("idle_min")) w.idle_timeout_min = server_.arg("idle_min").toInt();
            director_.setPolicy(w);
            server_.send(200, "text/plain", "policy saved + pushed");
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
    static bool parseKey(const String &name, IrKeyId &out) {
        if (name == "power") { out = IrKeyId::Power; return true; }
        if (name == "up")    { out = IrKeyId::Up; return true; }
        if (name == "down")  { out = IrKeyId::Down; return true; }
        if (name == "left")  { out = IrKeyId::Left; return true; }
        if (name == "right") { out = IrKeyId::Right; return true; }
        if (name == "ok")    { out = IrKeyId::Ok; return true; }
        if (name == "back")  { out = IrKeyId::Back; return true; }
        return false;
    }

    ICpxBridge &bridge_;
    GadgetDirector &director_;
    ThemeCatalog &catalog_;
    WebServer server_;
    ConfigCommand last_cmd_{};
};

} // namespace mcshal
