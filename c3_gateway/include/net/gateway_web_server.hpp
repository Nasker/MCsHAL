#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "interfaces/i_cpx_bridge.hpp"

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
  .badge { font-size:0.75rem; padding:4px 10px; border-radius:999px; background:rgba(56,189,248,0.15); color:var(--accent); }
  .score-box { text-align:center; padding:20px; border-radius:16px; background:#0f172a; margin-bottom:16px; }
  .score { font-size:3.2rem; font-weight:800; color:#4ade80; }
  .grid { display:grid; grid-template-columns:repeat(3,1fr); gap:10px; margin-bottom:18px; }
  .metric { background:#0f172a; padding:12px; border-radius:12px; text-align:center; }
  .metric .label { font-size:0.75rem; color:var(--muted); }
  .metric .val { font-size:1.15rem; font-weight:700; margin-top:4px; }
  .controls { display:flex; flex-direction:column; gap:10px; border-top:1px solid rgba(255,255,255,0.08); padding-top:14px; }
  label { display:flex; justify-content:space-between; font-size:0.85rem; color:var(--muted); }
  select, input, button { padding:8px 10px; border-radius:8px; border:1px solid rgba(255,255,255,0.15); background:#0f172a; color:var(--text); }
  button { background:var(--accent); color:#0f172a; font-weight:700; border:none; cursor:pointer; margin-top:4px; }
</style>
</head>
<body>
<div class="card">
  <div class="header">
    <h1>Living Room Comfort</h1>
    <span class="badge" id="link">Waiting for CPX...</span>
  </div>
  <div class="score-box">
    <div style="font-size:0.8rem;color:var(--muted)">COMFYNESS SCORE</div>
    <div class="score" id="score">--</div>
    <div id="alert" style="font-size:0.85rem;color:#94a3b8">Quiet & Cozy</div>
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
        <option value="1">Sound Party VU (Sandbox)</option>
        <option value="2">Pomodoro Focus (Sandbox)</option>
      </select>
    </label>
    <label>Ideal Temp (°C) <input id="ideal_temp" type="number" step="0.5" value="22.5" style="width:90px"></label>
    <label>Noise Beep Threshold (dB) <input id="max_db" type="number" step="1" value="70" style="width:90px"></label>
    <label>Mute Speaker Alerts <input id="muted" type="checkbox"></label>
    <button type="submit">Send to CPX</button>
  </form>
</div>
<script>
async function poll() {
  try {
    const r = await fetch('/api/telemetry');
    const d = await r.json();
    document.getElementById('score').textContent = d.score;
    document.getElementById('temp').textContent = d.temp.toFixed(1) + ' °C';
    document.getElementById('light').textContent = d.light;
    document.getElementById('sound').textContent = d.sound.toFixed(1) + ' dB';
    document.getElementById('link').textContent = d.age_ms < 3000 ? 'CPX Online' : 'CPX Offline';
    document.getElementById('alert').textContent = d.noisy ? 'TOO NOISY! Beeping...' : (d.muted ? 'Speaker Muted' : 'Quiet & Cozy');
    document.getElementById('alert').style.color = d.noisy ? '#f87171' : '#94a3b8';
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
            char json[192];
            snprintf(json, sizeof(json),
                     "{\"temp\":%.1f,\"light\":%u,\"sound\":%.1f,\"score\":%u,"
                     "\"noisy\":%s,\"muted\":%s,\"mode\":%u,\"age_ms\":%lu}",
                     static_cast<double>(f.temperature_c),
                     static_cast<unsigned>(f.light_level),
                     static_cast<double>(f.sound_db),
                     static_cast<unsigned>(f.comfort_score),
                     f.is_too_noisy ? "true" : "false",
                     f.is_muted ? "true" : "false",
                     static_cast<unsigned>(f.mode),
                     static_cast<unsigned long>(age));
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

        server_.begin();
    }

    void handleClient() { server_.handleClient(); }

private:
    ICpxBridge &bridge_;
    WebServer server_;
    ConfigCommand last_cmd_{};
};

} // namespace mcshal
