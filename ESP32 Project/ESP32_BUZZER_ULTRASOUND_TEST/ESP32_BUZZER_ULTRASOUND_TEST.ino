#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// -------------------------------------------------------------
// ESP32 BUZZER AND ULTRA SOUND TEST
//
// Off-topic bench utility, not part of the vending machine project.
// Standalone sketch - own Wi-Fi access point + captive portal, same
// pattern as ESP32_B3_RELAY_TEST. (Replaces the old ESP32_BUZZER_TEST.)
//
// Connect to the "ESP32 Buzzer and Ultra Sound" Wi-Fi and the page should
// pop up on its own within a few seconds.
//
// WIRING
//   Buzzer signal  -> GPIO13          Buzzer GND   -> any GND pin
//   Sensor TRIG    -> GPIO27          Sensor GND   -> any GND pin
//   Sensor ECHO    -> GPIO14          (see the voltage note below)
//   Sensor VCC     -> VIN (5V) for an HC-SR04 / JSN-SR04T,
//                     or 3V3 only for a 3.3V-capable module
//                     (HC-SR04P, RCWL-1601, ...)
//
// VOLTAGE NOTE - READ BEFORE POWERING THE SENSOR
//   * The 3V3 pin is a regulated 3.3V (not 3.0V).
//   * VIN is the 5V rail: on a USB-powered ESP32 board it is the USB 5V
//     (about 4.7-5.0V after the protection diode).
//   * A 5V HC-SR04 drives ECHO to 5V. ESP32 GPIOs are 3.3V parts (absolute
//     max about 3.6V), so put a divider on ECHO before GPIO14:
//         ECHO --[1k]--+--> GPIO14
//                      |
//                    [2k]
//                      |
//                     GND
//     A 3.3V-capable module does not need the divider.
//   * TRIG is an input on the sensor; GPIO27's 3.3V is normally enough to
//     trigger an HC-SR04.
//
// MODES (toggle on the page)
//   SINGLE   - buzzer tests only, sensor idle (the original behavior).
//   MULTIPLE - buzzer tests AND live sensor readings at the same time.
//   SENSOR   - sensor readings only, buzzer buttons locked and silent.
//
// Quick visual hint for the buzzer even before powering it on: active
// buzzers usually have a rounded epoxy "blob" on top hiding a small driver
// chip; passive buzzers are usually a flatter bare piezo disc with no blob.
// Not 100% reliable, but often an instant giveaway.
// -------------------------------------------------------------

#define BUZZER_PIN 13   // buzzer signal wire
#define TRIG_PIN   27   // ultrasonic TRIGGER (ESP32 output)
#define ECHO_PIN   14   // ultrasonic ECHO    (ESP32 input - mind the 5V note above)

const char* AP_SSID     = "ESP32 Buzzer and Ultra Sound"; // 32 characters is the Wi-Fi limit
const char* AP_PASSWORD = "12345678";

const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);

// -------------------------------------------------------------
// Run mode
// -------------------------------------------------------------
enum RunMode { RUN_SINGLE, RUN_MULTIPLE, RUN_SENSOR };
RunMode runMode = RUN_SINGLE; // default: buzzer only, nothing else active

bool sensorWanted() { return runMode != RUN_SINGLE; }

// -------------------------------------------------------------
// Buzzer: non-blocking test state machine - runs entirely off millis() so
// server.handleClient()/dnsServer.processNextRequest() never stall,
// which is what lets STOP interrupt a test immediately instead of
// only after it finishes on its own.
// -------------------------------------------------------------
enum TestMode { TEST_NONE, TEST_STEADY, TEST_SWEEP };
TestMode currentTest = TEST_NONE;
unsigned long testStartMillis = 0;

const unsigned long STEADY_DURATION_MS = 2000;
const int           SWEEP_START_HZ     = 200;
const int           SWEEP_END_HZ       = 2000;
const int           SWEEP_STEP_HZ      = 20;
const unsigned long SWEEP_STEP_MS      = 30;
const unsigned long SWEEP_STEPS        = (SWEEP_END_HZ - SWEEP_START_HZ) / SWEEP_STEP_HZ + 1;

void stopBuzzer() {
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
}

void updateTest() {
    if (currentTest == TEST_NONE) return;
    unsigned long elapsed = millis() - testStartMillis;

    if (currentTest == TEST_STEADY) {
        if (elapsed >= STEADY_DURATION_MS) {
            stopBuzzer();
            currentTest = TEST_NONE;
        }
        // else: pin was already driven HIGH the moment the test started, nothing more to do
    } else if (currentTest == TEST_SWEEP) {
        unsigned long stepIndex = elapsed / SWEEP_STEP_MS;
        if (stepIndex >= SWEEP_STEPS) {
            stopBuzzer();
            currentTest = TEST_NONE;
        } else {
            tone(BUZZER_PIN, SWEEP_START_HZ + stepIndex * SWEEP_STEP_HZ);
        }
    }
}

// -------------------------------------------------------------
// Ultrasonic sensor: also non-blocking. A 10us pulse on TRIG starts a
// measurement; an interrupt on ECHO times how long the pin stays HIGH, and
// loop() collects the result. No pulseIn(), so the buzzer sweep and the web
// page are never held up waiting for an echo that may not come.
// -------------------------------------------------------------
enum SensorStatus { SS_WAITING, SS_OK, SS_NO_ECHO, SS_TOO_CLOSE, SS_FAR };
SensorStatus sensorStatus = SS_WAITING;

volatile unsigned long echoRiseUs   = 0;
volatile unsigned long echoWidthUs  = 0;
volatile bool          echoRiseSeen = false;
volatile bool          echoDone     = false;

const unsigned long SENSOR_PERIOD_MS  = 100;   // gap between readings
const unsigned long SENSOR_TIMEOUT_MS = 50;    // no falling edge by then = NO ECHO
const unsigned long MIN_ECHO_US       = 100;   // about 1.7 cm - shorter is noise / too close
const unsigned long MAX_ECHO_US       = 25000; // about 4.3 m  - longer is nothing in range
// 343 m/s at roughly 20 C: distance(cm) = echo_us * 0.0343 / 2
const float         CM_PER_US         = 0.01715f;

bool sensorRunning = false;
bool sensorPending = false;
unsigned long sensorTriggerMs = 0;
unsigned long sensorNextMs    = 0;

float         lastCm      = 0;
unsigned long lastWidthUs = 0;
float         recentCm[5];
uint8_t       recentCount = 0;
uint8_t       recentIdx   = 0;
unsigned long sensorOkCount  = 0;
unsigned long sensorBadCount = 0;
unsigned long lastPrintMs    = 0;

void IRAM_ATTR echoISR() {
    if (digitalRead(ECHO_PIN)) {
        echoRiseUs   = micros();
        echoRiseSeen = true;
    } else if (echoRiseSeen) {
        echoWidthUs  = micros() - echoRiseUs;
        echoRiseSeen = false;
        echoDone     = true;
    }
}

void sensorStart() {
    if (sensorRunning) return;
    sensorStatus   = SS_WAITING;
    recentCount    = 0;
    recentIdx      = 0;
    sensorOkCount  = 0;
    sensorBadCount = 0;
    lastCm         = 0;
    lastWidthUs    = 0;
    sensorPending  = false;
    sensorNextMs   = 0;
    echoDone       = false;
    echoRiseSeen   = false;
    attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echoISR, CHANGE);
    sensorRunning  = true;
}

void sensorStop() {
    if (!sensorRunning) return;
    detachInterrupt(digitalPinToInterrupt(ECHO_PIN)); // a floating ECHO pin must not keep firing interrupts
    digitalWrite(TRIG_PIN, LOW);
    sensorPending = false;
    sensorRunning = false;
}

float averageCm() {
    if (recentCount == 0) return 0;
    float sum = 0;
    for (uint8_t i = 0; i < recentCount; i++) sum += recentCm[i];
    return sum / recentCount;
}

// Takes a plain int (a SensorStatus value) on purpose: the Arduino IDE auto-generates a prototype
// for every function and puts it ABOVE the enum definition, so a custom type in the signature
// fails with "'SensorStatus' was not declared in this scope".
void recordReading(int statusCode, unsigned long widthUs) {
    SensorStatus status = (SensorStatus)statusCode;
    sensorStatus = status;
    lastWidthUs  = widthUs;
    if (status == SS_OK) {
        lastCm = widthUs * CM_PER_US;
        recentCm[recentIdx] = lastCm;
        recentIdx = (recentIdx + 1) % 5;
        if (recentCount < 5) recentCount++;
        sensorOkCount++;
    } else {
        sensorBadCount++;
    }
    unsigned long now = millis();
    if (now - lastPrintMs >= 500) { // keep the serial monitor readable
        lastPrintMs = now;
        switch (status) {
            case SS_OK:       Serial.printf("[SENSOR] %.1f cm (echo %lu us)\n", lastCm, widthUs); break;
            case SS_NO_ECHO:  Serial.println("[SENSOR] NO ECHO - check wiring, power and the ECHO divider"); break;
            case SS_TOO_CLOSE:Serial.printf("[SENSOR] too close / noise (echo %lu us)\n", widthUs); break;
            case SS_FAR:      Serial.printf("[SENSOR] out of range (echo %lu us)\n", widthUs); break;
            default: break;
        }
    }
}

void updateSensor() {
    if (!sensorWanted()) { sensorStop(); return; }
    sensorStart(); // no-op once running

    unsigned long now = millis();
    if (!sensorPending) {
        if (now < sensorNextMs) return;
        echoDone     = false;
        echoRiseSeen = false;
        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIG_PIN, LOW);
        sensorTriggerMs = now;
        sensorPending   = true;
        return;
    }

    if (echoDone) {
        unsigned long width = echoWidthUs;
        echoDone = false;
        if (width < MIN_ECHO_US)      recordReading(SS_TOO_CLOSE, width);
        else if (width > MAX_ECHO_US) recordReading(SS_FAR, width);
        else                          recordReading(SS_OK, width);
        sensorPending = false;
        sensorNextMs  = now + SENSOR_PERIOD_MS;
    } else if (now - sensorTriggerMs > SENSOR_TIMEOUT_MS) {
        recordReading(SS_NO_ECHO, 0);
        sensorPending = false;
        sensorNextMs  = now + SENSOR_PERIOD_MS;
    }
}

const char* sensorStatusName() {
    switch (sensorStatus) {
        case SS_OK:        return "ok";
        case SS_NO_ECHO:   return "no_echo";
        case SS_TOO_CLOSE: return "too_close";
        case SS_FAR:       return "far";
        default:           return "waiting";
    }
}

// -------------------------------------------------------------
// Web page
// -------------------------------------------------------------
const char* PAGE = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Buzzer and Ultra Sound Test</title>
<style>
body{font-family:Arial,Helvetica,sans-serif;background:#14161a;color:#eee;margin:0;padding:16px}
h1{font-size:18px;margin:0 0 4px}
.sub{color:#888;font-size:12px;margin:0 0 16px}
.seg{display:grid;grid-template-columns:repeat(3,1fr);border:1px solid #333;border-radius:8px;overflow:hidden;margin-bottom:16px}
.seg button{border:0;border-radius:0;padding:12px 4px;background:#1e2128;font-size:13px}
.seg button+button{border-left:1px solid #333}
.seg button.on{background:#2d6cdf;color:#fff}
.status{padding:12px 16px;border-radius:8px;font-weight:bold;text-align:center;margin-bottom:12px;
  border:1px solid #333;background:#1e2128}
.status.idle{color:#7a8}
.status.running{color:#fff;background:#c0392b;border-color:#c0392b}
.status.locked{color:#888}
.btnrow{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:20px}
button{font-family:inherit;font-size:14px;font-weight:bold;padding:16px 8px;border-radius:8px;
  border:1px solid #333;background:#1e2128;color:#eee;cursor:pointer}
button span{display:block;font-weight:normal;font-size:11px;color:#888;margin-top:4px}
button:active{background:#2a2d34}
button:disabled{opacity:.35;cursor:not-allowed}
button.stop{grid-column:1 / span 2;background:#3a1414;border-color:#c0392b;color:#f5b5ae}
.sensor{background:#1e2128;border:1px solid #333;border-radius:8px;padding:14px 16px;margin-bottom:20px}
.sensor h2{font-size:12px;letter-spacing:.06em;text-transform:uppercase;color:#888;margin:0 0 8px}
.dist{font-size:40px;font-weight:bold;font-family:ui-monospace,Consolas,monospace;margin:0}
.dist.ok{color:#4ade80}
.dist.bad{color:#f5b5ae;font-size:18px;line-height:1.3}
.dist.off{color:#666;font-size:15px;font-weight:normal}
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:6px 12px;margin-top:10px;font-size:12px;color:#aaa}
.grid b{color:#fff;font-family:ui-monospace,Consolas,monospace}
.howto{background:#1e2128;border:1px solid #333;border-radius:8px;padding:14px 16px;font-size:13px;line-height:1.5}
.howto h3{font-size:13px;margin:12px 0 4px}
.howto h3:first-child{margin-top:0}
.howto b{color:#fff}
.foot{margin-top:16px;font-size:11px;color:#666}
</style></head><body>
<h1>ESP32 Buzzer and Ultra Sound Test</h1>
<p class="sub">Buzzer on GPIO13 &middot; TRIG on GPIO27 &middot; ECHO on GPIO14. Standalone, not part of the vending machine project.</p>

<div class="seg" role="group" aria-label="Mode">
  <button id="m-single"   onclick="setMode('single')">SINGLE<span>buzzer only</span></button>
  <button id="m-multiple" onclick="setMode('multiple')">MULTIPLE<span>buzzer + sensor</span></button>
  <button id="m-sensor"   onclick="setMode('sensor')">SENSOR<span>sensor only</span></button>
</div>

<div class="status idle" id="statusBox">Idle</div>
<div class="btnrow">
  <button class="buzz" onclick="runTest(1)">ACTIVE TEST<span>Steady DC (2s)</span></button>
  <button class="buzz" onclick="runTest(2)">PASSIVE TEST<span>Frequency Sweep (~3s)</span></button>
  <button class="buzz stop" onclick="runStop()">STOP</button>
</div>

<div class="sensor">
  <h2>Ultra sound sensor</h2>
  <p class="dist off" id="dist">Sensor is off in SINGLE mode. Pick MULTIPLE or SENSOR.</p>
  <div class="grid" id="sensorGrid" hidden>
    <div>Echo width <b id="echoUs">-</b></div>
    <div>Average of last 5 <b id="avgCm">-</b></div>
    <div>Good readings <b id="okCount">0</b></div>
    <div>Bad readings <b id="badCount">0</b></div>
  </div>
</div>

<div class="howto">
  <h3>Modes</h3>
  <p><b>SINGLE</b> &mdash; buzzer tests only.<br>
     <b>MULTIPLE</b> &mdash; buzzer tests and live sensor readings together.<br>
     <b>SENSOR</b> &mdash; sensor readings only; the buzzer is locked and silent.</p>
  <h3>Active Test &mdash; Steady DC</h3>
  <p>Continuous steady beep the whole time &rarr; <b>Active</b> buzzer, confirmed.<br>
     Silence, or one faint click at the start &rarr; not Active, try the Passive Test.</p>
  <h3>Passive Test &mdash; Frequency Sweep (200Hz&ndash;2000Hz)</h3>
  <p>Pitch clearly rises from low to high &rarr; <b>Passive</b> buzzer, confirmed.<br>
     One flat pitch throughout, or a buzzy noise that never changes &rarr; not Passive, it's Active.</p>
  <h3>Sensor readings</h3>
  <p>Hold a flat object 10&ndash;50 cm in front of the sensor; the distance should follow it.<br>
     <b>NO ECHO</b> &rarr; check power, TRIG/ECHO wires and the ECHO voltage divider.<br>
     Echo width is the raw time ECHO stays HIGH; distance = width &times; 0.01715 cm per &micro;s.</p>
</div>
<p class="foot">This is a bench tool only &mdash; no other part of the project is affected.</p>
<script>
const MODES = ['single','multiple','sensor'];
const MESSAGES = {
  waiting:   'Waiting for the first reading...',
  no_echo:   'NO ECHO - check power, wiring and the ECHO divider',
  too_close: 'TOO CLOSE or noise (echo under 100 µs)',
  far:       'OUT OF RANGE - nothing in front (echo over 25 ms)'
};
async function setMode(m) {
  await fetch('/mode?value=' + m, { method: 'POST' });
  poll();
}
async function runTest(n) {
  await fetch('/test' + n, { method: 'POST' });
  poll();
}
async function runStop() {
  await fetch('/stop', { method: 'POST' });
  poll();
}
async function poll() {
  let s;
  try { s = await (await fetch('/status')).json(); } catch (e) { return; }
  MODES.forEach(m => document.getElementById('m-' + m).classList.toggle('on', s.mode === m));

  const locked = s.mode === 'sensor';
  document.querySelectorAll('.buzz').forEach(b => b.disabled = locked);
  const box = document.getElementById('statusBox');
  if (locked) {
    box.className = 'status locked';
    box.textContent = 'Buzzer locked (SENSOR mode)';
  } else if (s.running) {
    box.className = 'status running';
    box.textContent = s.test === 'steady' ? 'Running: Active Test...' : 'Running: Passive Test...';
  } else {
    box.className = 'status idle';
    box.textContent = 'Idle';
  }

  const d = document.getElementById('dist');
  const grid = document.getElementById('sensorGrid');
  const z = s.sensor;
  if (!z.active) {
    d.className = 'dist off';
    d.textContent = 'Sensor is off in SINGLE mode. Pick MULTIPLE or SENSOR.';
    grid.hidden = true;
    return;
  }
  grid.hidden = false;
  if (z.status === 'ok') {
    d.className = 'dist ok';
    d.textContent = z.cm.toFixed(1) + ' cm';
  } else {
    d.className = 'dist bad';
    d.textContent = MESSAGES[z.status] || z.status;
  }
  document.getElementById('echoUs').textContent = z.us ? z.us + ' µs' : '-';
  document.getElementById('avgCm').textContent = z.avg > 0 ? z.avg.toFixed(1) + ' cm' : '-';
  document.getElementById('okCount').textContent = z.ok;
  document.getElementById('badCount').textContent = z.bad;
}
setInterval(poll, 300);
poll();
</script>
</body></html>
)HTML";

void handleRoot()   { server.send(200, "text/html", PAGE); }

void handleTest1() {
    if (runMode == RUN_SENSOR) { server.send(409, "text/plain", "buzzer locked in SENSOR mode"); return; }
    currentTest = TEST_STEADY;
    testStartMillis = millis();
    digitalWrite(BUZZER_PIN, HIGH);
    server.send(200, "text/plain", "ok");
}
void handleTest2() {
    if (runMode == RUN_SENSOR) { server.send(409, "text/plain", "buzzer locked in SENSOR mode"); return; }
    currentTest = TEST_SWEEP;
    testStartMillis = millis();
    server.send(200, "text/plain", "ok");
}
void handleStop() {
    currentTest = TEST_NONE;
    stopBuzzer();
    server.send(200, "text/plain", "ok");
}

void handleMode() {
    String v = server.arg("value");
    RunMode newMode = (v == "multiple") ? RUN_MULTIPLE : (v == "sensor") ? RUN_SENSOR : RUN_SINGLE;
    if (newMode != runMode) {
        runMode = newMode;
        if (runMode == RUN_SENSOR) { // buzzer is locked out in SENSOR mode - make sure it is quiet
            currentTest = TEST_NONE;
            stopBuzzer();
        }
        // updateSensor() starts/stops the sensor from here on, per runMode
    }
    server.send(200, "text/plain", "ok");
}

void handleStatus() {
    String json = "{\"mode\":\"";
    json += runMode == RUN_SINGLE ? "single" : (runMode == RUN_MULTIPLE ? "multiple" : "sensor");
    json += "\",\"running\":";
    json += (currentTest != TEST_NONE) ? "true" : "false";
    json += ",\"test\":\"";
    json += currentTest == TEST_STEADY ? "steady" : (currentTest == TEST_SWEEP ? "sweep" : "none");
    json += "\",\"sensor\":{\"active\":";
    json += sensorWanted() ? "true" : "false";
    json += ",\"status\":\"";
    json += sensorStatusName();
    json += "\",\"cm\":";
    json += String(lastCm, 1);
    json += ",\"avg\":";
    json += String(averageCm(), 1);
    json += ",\"us\":";
    json += String(lastWidthUs);
    json += ",\"ok\":";
    json += String(sensorOkCount);
    json += ",\"bad\":";
    json += String(sensorBadCount);
    json += "}}";
    server.send(200, "application/json", json);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    pinMode(TRIG_PIN, OUTPUT);
    digitalWrite(TRIG_PIN, LOW);
    pinMode(ECHO_PIN, INPUT_PULLDOWN); // unwired ECHO reads LOW instead of floating

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    Serial.println("\n=======================================================");
    Serial.println("   ESP32 BUZZER AND ULTRA SOUND TEST - STANDALONE UTILITY");
    Serial.println("=======================================================");
    Serial.printf("[TEST] Buzzer GPIO%d, TRIG GPIO%d, ECHO GPIO%d\n", BUZZER_PIN, TRIG_PIN, ECHO_PIN);
    Serial.printf("[TEST] Connect Wi-Fi to \"%s\" (password: %s)\n", AP_SSID, AP_PASSWORD);
    Serial.println("[TEST] The page should pop up on its own within a few seconds.");
    Serial.print("[TEST] If it doesn't, open http://");
    Serial.println(WiFi.softAPIP()); // normally 192.168.4.1

    server.on("/", handleRoot);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/mode", HTTP_POST, handleMode);
    server.on("/test1", HTTP_POST, handleTest1);
    server.on("/test2", HTTP_POST, handleTest2);
    server.on("/stop", HTTP_POST, handleStop);
    server.onNotFound(handleRoot); // captive-portal probes land on the page too
    server.begin();

    // If tone()/noTone() fail to compile ("was not declared"), your installed ESP32
    // Arduino core predates their support - let me know and I'll switch this sketch
    // to the ledcSetup()/ledcWriteTone() API instead.
}

void loop() {
    dnsServer.processNextRequest();
    server.handleClient();
    updateTest();
    updateSensor();
}
