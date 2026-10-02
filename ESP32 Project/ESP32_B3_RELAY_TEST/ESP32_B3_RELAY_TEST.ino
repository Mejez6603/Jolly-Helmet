#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// -------------------------------------------------------------
// STANDALONE BENCH TEST - BOX 3 (B3) RELAY VERIFICATION
//
// Not part of the production firmware. No Shared_Common.h, no ESP-NOW,
// no dependency on any other node in the mesh. Hosts its own Wi-Fi
// access point so every relay/pin on B3 can be exercised from a phone
// or laptop before the rest of the system is even powered on.
//
// Reflash B3 with its real firmware once wiring is confirmed good -
// this sketch has no further purpose after that.
// -------------------------------------------------------------

const char* AP_SSID     = "BOX 3 B3 TEST";
const char* AP_PASSWORD = "12345678";

#define RELAY_ACTIVE   LOW   // matches this project's relay modules (active-LOW)
#define RELAY_INACTIVE HIGH

struct RelayDef {
    uint8_t     pin;
    const char* label;
};

// Matches the B3 pin sheet. 2026-10-01: UV 1 moved GPIO5 -> GPIO27 and Drain Pump 2 moved
// GPIO14 -> GPIO32, because GPIO5 and GPIO14 emit a short PWM burst at power-up that can click a
// relay. Re-wire those two relay inputs on the board before running this version.
// 2026-10-02: Drain Pump 1 moved GPIO13 -> GPIO33 (the relay on 13 stayed ON with no command).
RelayDef relays[] = {
    {23, "ACS Side Door"},
    {22, "Coin Pwr"},
    {27, "UV 1"},
    {21, "UV 2"},
    {33, "Drain Pump 1 (Alcohol)"},
    {32, "Drain Pump 2 (Scented)"},
    {18, "Humidifier 1 (Alcohol)"},
    {19, "Humidifier 2 (Scented)"},
    {25, "Humidifier 3 (Both)"},
    {4,  "Enclosure Lock"},
    {16, "Panel Lock"},
    {17, "Backdoor Lock"},
    {26, "Heater"}
};
const int RELAY_COUNT = sizeof(relays) / sizeof(relays[0]);

bool relayOn[RELAY_COUNT];  // logical state (true = ON), independent of active-LOW wiring
bool singleMode = true;     // default ON - safer for first bring-up: never more than one
                             // high-current load (e.g. Heater + a pump) energized at once

WebServer server(80);

// Captive portal: a DNS server that resolves every hostname to this ESP32's own IP,
// combined with onNotFound() below serving the same page for any URL - together these
// make the phone/laptop's own captive-portal-detection request (which every modern OS
// fires right after joining a new Wi-Fi network) land on our page instead of a 404,
// which is exactly what makes the OS auto-pop the browser.
const byte DNS_PORT = 53;
DNSServer dnsServer;

void applyRelay(int i) {
    digitalWrite(relays[i].pin, relayOn[i] ? RELAY_ACTIVE : RELAY_INACTIVE);
}

void allOff() {
    for (int i = 0; i < RELAY_COUNT; i++) {
        relayOn[i] = false;
        applyRelay(i);
    }
}

// -------------------------------------------------------------
// Web page - one self-contained HTML/CSS/JS response, no external assets
// -------------------------------------------------------------
const char* PAGE_HEAD = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BOX 3 (B3) Relay Test</title>
<style>
body{font-family:Arial,Helvetica,sans-serif;background:#14161a;color:#eee;margin:0;padding:16px}
h1{font-size:18px;margin:0 0 4px}
.sub{color:#888;font-size:12px;margin:0 0 18px}
.mode{display:flex;align-items:center;gap:10px;background:#1e2128;border:1px solid #333;
  border-radius:8px;padding:10px 14px;margin-bottom:18px}
.mode b{font-size:13px}
.switch{position:relative;width:52px;height:28px;flex:0 0 auto}
.switch input{opacity:0;width:0;height:0}
.slider{position:absolute;inset:0;background:#555;border-radius:28px;cursor:pointer;transition:.2s}
.slider:before{content:"";position:absolute;width:22px;height:22px;left:3px;top:3px;
  background:#fff;border-radius:50%;transition:.2s}
input:checked + .slider{background:#c0392b}
input:checked + .slider:before{transform:translateX(24px)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:10px}
.relay{background:#1e2128;border:1px solid #333;border-radius:8px;padding:12px;text-align:center;
  cursor:pointer;user-select:none;transition:.15s}
.relay .name{font-size:13px;margin-bottom:8px;line-height:1.3;min-height:34px}
.relay .pin{font-size:11px;color:#888;margin-bottom:8px}
.relay .state{font-family:monospace;font-weight:bold;font-size:13px;padding:4px 0;border-radius:4px;
  background:#2a2d34;color:#7a8}
.relay.on{border-color:#c0392b;background:#2a1414}
.relay.on .state{background:#c0392b;color:#fff}
.foot{margin-top:20px;font-size:11px;color:#666}
</style></head><body>
<h1>BOX 3 (B3) &mdash; Relay Test</h1>
<p class="sub">Standalone bench test. Not connected to the rest of the system.</p>
<div class="mode">
  <label class="switch">
    <input type="checkbox" id="modeSwitch" onchange="setMode()">
    <span class="slider"></span>
  </label>
  <b id="modeLabel">SINGLE MODE</b>
  <span style="color:#888;font-size:12px">&mdash; toggle for Multiple Mode</span>
</div>
<div class="grid" id="grid"></div>
<p class="foot">Reflash with the real B3 firmware once wiring is confirmed.</p>
<script>
)HTML";

const char* PAGE_TAIL = R"HTML(
</script></body></html>
)HTML";

void handleRoot() {
    String page = String(PAGE_HEAD);

    page += "const RELAYS = [";
    for (int i = 0; i < RELAY_COUNT; i++) {
        page += "{\"i\":" + String(i) + ",\"label\":\"" + relays[i].label + "\",\"pin\":" + String(relays[i].pin) + "}";
        if (i < RELAY_COUNT - 1) page += ",";
    }
    page += "];\n";

    page += R"JS(
let state = { mode: "single", relays: [] };

function render() {
  document.getElementById('modeSwitch').checked = (state.mode === "multiple");
  document.getElementById('modeLabel').textContent = state.mode === "multiple" ? "MULTIPLE MODE" : "SINGLE MODE";
  const grid = document.getElementById('grid');
  grid.innerHTML = "";
  RELAYS.forEach(r => {
    const on = state.relays[r.i];
    const el = document.createElement('div');
    el.className = 'relay' + (on ? ' on' : '');
    el.onclick = () => toggleRelay(r.i);
    el.innerHTML = '<div class="name">' + r.label + '</div>' +
                    '<div class="pin">GPIO ' + r.pin + '</div>' +
                    '<div class="state">' + (on ? 'ON' : 'OFF') + '</div>';
    grid.appendChild(el);
  });
}

async function refresh() {
  const res = await fetch('/status');
  state = await res.json();
  render();
}

async function toggleRelay(i) {
  await fetch('/toggle?relay=' + i, { method: 'POST' });
  refresh();
}

async function setMode() {
  const multiple = document.getElementById('modeSwitch').checked;
  await fetch('/mode?value=' + (multiple ? 'multiple' : 'single'), { method: 'POST' });
  refresh();
}

refresh();
)JS";

    page += String(PAGE_TAIL);
    server.send(200, "text/html", page);
}

void sendStatus() {
    String json = "{\"mode\":\"";
    json += singleMode ? "single" : "multiple";
    json += "\",\"relays\":[";
    for (int i = 0; i < RELAY_COUNT; i++) {
        json += relayOn[i] ? "true" : "false";
        if (i < RELAY_COUNT - 1) json += ",";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleToggle() {
    if (!server.hasArg("relay")) { server.send(400, "text/plain", "missing relay"); return; }
    int i = server.arg("relay").toInt();
    if (i < 0 || i >= RELAY_COUNT) { server.send(400, "text/plain", "bad index"); return; }

    bool turningOn = !relayOn[i];

    if (singleMode) {
        // At most one relay energized at a time. Pressing the already-active relay
        // turns it off; pressing a different one turns that one on and everything
        // else off in the same request.
        for (int j = 0; j < RELAY_COUNT; j++) {
            relayOn[j] = false;
            applyRelay(j);
        }
        relayOn[i] = turningOn;
        applyRelay(i);
    } else {
        relayOn[i] = turningOn;
        applyRelay(i);
    }

    sendStatus();
}

void handleMode() {
    if (!server.hasArg("value")) { server.send(400, "text/plain", "missing value"); return; }
    bool newSingle = (server.arg("value") == "single");

    if (newSingle != singleMode) {
        allOff(); // switching modes always clears state, so you never inherit an
                  // ambiguous "which one wins" situation from the other mode
        singleMode = newSingle;
    }
    sendStatus();
}

void setup() {
    Serial.begin(115200);
    delay(200);

    for (int i = 0; i < RELAY_COUNT; i++) {
        digitalWrite(relays[i].pin, RELAY_INACTIVE); // level first, so the pin never pulses LOW when it becomes an output
        pinMode(relays[i].pin, OUTPUT);
        relayOn[i] = false;
        applyRelay(i); // every relay starts OFF/INACTIVE
    }

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP()); // "*" = resolve every hostname to us

    Serial.println("\n=======================================================");
    Serial.println("   BOX 3 (B3) RELAY TEST - STANDALONE, NOT PART OF THE MESH");
    Serial.println("=======================================================");
    Serial.printf("[B3 TEST] Connect Wi-Fi to \"%s\" (password: %s)\n", AP_SSID, AP_PASSWORD);
    Serial.println("[B3 TEST] The page should pop up on its own within a few seconds.");
    Serial.print("[B3 TEST] If it doesn't, open http://");
    Serial.println(WiFi.softAPIP()); // normally 192.168.4.1

    server.on("/", handleRoot);
    server.on("/status", HTTP_GET, sendStatus);
    server.on("/toggle", HTTP_POST, handleToggle);
    server.on("/mode", HTTP_POST, handleMode);
    server.onNotFound(handleRoot); // any other path (incl. every OS's captive-portal probe) -> the page
    server.begin();
}

void loop() {
    dnsServer.processNextRequest();
    server.handleClient();
}
