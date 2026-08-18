#ifndef INDEX_HTML_H
#define INDEX_HTML_H

const char htmlUI[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Unit A1 Dashboard</title>
  <style>
    body { font-family: sans-serif; text-align: center; background: #1a1a1a; color: #fff; margin: 0; padding: 15px; }
    .card { background: #2a2a2a; padding: 18px; margin: 12px auto; max-width: 480px; border-radius: 8px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
    h3 { margin-top: 0; color: #0078D7; border-bottom: 1px solid #444; padding-bottom: 8px; }
    input, button { padding: 10px; margin: 5px; border-radius: 5px; border: none; font-size: 14px; }
    input[type="text"], input[type="number"] { width: 85%; background: #333; color: #fff; border: 1px solid #555; }
    button { background: #0078D7; color: white; font-weight: bold; cursor: pointer; width: 90%; }
    .btn-green { background: #28a745; }
    .btn-red { background: #dc3545; }
    .status-row { display: flex; justify-content: space-between; align-items: center; padding: 6px 10px; margin: 4px 0; background: #333; border-radius: 4px; }
    .badge { padding: 3px 8px; border-radius: 3px; font-weight: bold; font-size: 12px; }
    .bg-open { background: #dc3545; } .bg-closed { background: #28a745; }
    .big-stat { font-size: 24px; font-weight: bold; color: #28a745; margin: 5px 0; }
  </style>
</head>
<body>
  <h2>Unit A1 Control & Configuration</h2>

  <div class="card">
    <h3>⚙️ Timeline & Pricing Settings</h3>
    <label>Required Coins (PHP):</label><br>
    <input type="number" id="cfgPrice" value="20"><br>
    
    <label>Sanitization Duration (Seconds):</label><br>
    <input type="number" id="cfgDuration" value="120"><br>

    <label>Welcome Text (Step 1.1a):</label><br>
    <input type="text" id="cfgMsgWelcome" value="Insert Coin (P%d) to Start"><br>

    <label>Instruction Text (Step 2):</label><br>
    <input type="text" id="cfgMsgInstruction" value="Please Place Headgear Inside"><br>

    <button class="btn-green" onclick="saveConfiguration()">UPDATE MACHINE CONFIG</button>
  </div>

  <div class="card">
    <h3>📊 Live Machine Monitor</h3>
    <div class="big-stat">PHP <span id="st-coins">0</span>.00 Deposited</div>
    
    <div class="status-row"><span>Enclosure Door:</span><span id="st-enc" class="badge bg-open">...</span></div>
    <div class="status-row"><span>Panel Door:</span><span id="st-panel" class="badge bg-open">...</span></div>
    <div class="status-row"><span>Back Door:</span><span id="st-back" class="badge bg-open">...</span></div>
    <div class="status-row"><span>Helmet Distance:</span><span id="st-helm" class="badge">...</span></div>
    
    <button class="btn-red" onclick="sendCmd('/reset_coins')" style="margin-top: 10px;">Reset Coin Balance</button>
  </div>

  <div class="card">
    <h3>🛠️ Manual Debug & Hardware Test</h3>
    <button onclick="sendCmd('/toggle?pin=21')">Toggle Enclosure Lock (Pin 21)</button>
    <button onclick="sendCmd('/toggle?pin=16')">Toggle Panel Lock (Pin 16)</button>
    <button onclick="sendCmd('/toggle?pin=17')">Toggle Backdoor Lock (Pin 17)</button>
    <button onclick="sendCmd('/toggle?pin=5')">Toggle UV Light (Pin 5)</button>
    <button onclick="sendCmd('/toggle?pin=18')">Toggle Humidifier (Pin 18)</button>
    <button onclick="sendCmd('/toggle?pin=19')">Toggle Buzzer (Pin 19)</button>
  </div>

  <script>
    function sendCmd(path) { fetch(path).catch(e => console.log(e)); }

    function saveConfiguration() {
      let price = document.getElementById('cfgPrice').value;
      let dur = document.getElementById('cfgDuration').value;
      let welcome = encodeURIComponent(document.getElementById('cfgMsgWelcome').value);
      let inst = encodeURIComponent(document.getElementById('cfgMsgInstruction').value);
      
      fetch(`/update_config?price=${price}&dur=${dur}&welcome=${welcome}&inst=${inst}`)
        .then(() => alert("Configuration sent to ESP32 A1!"))
        .catch(e => alert("Error updating configuration"));
    }

    function updateStatus() {
      fetch('/get_sensors')
        .then(res => res.json())
        .then(d => {
          document.getElementById('st-coins').innerText = d.totalCoins;
          setBadge('st-enc', d.encDoor === 0, 'CLOSED', 'OPEN');
          setBadge('st-panel', d.panelDoor === 0, 'CLOSED', 'OPEN');
          setBadge('st-back', d.backDoor === 0, 'CLOSED', 'OPEN');

          let hEl = document.getElementById('st-helm');
          if (d.helmDetected) {
            hEl.className = 'badge bg-closed';
            hEl.innerText = 'DETECTED (' + d.helmDist + 'cm)';
          } else {
            hEl.className = 'badge bg-open';
            hEl.innerText = 'NO HELMET (' + d.helmDist + 'cm)';
          }
        }).catch(e => {});
    }

    function setBadge(id, isClosed, tTrue, tFalse) {
      let el = document.getElementById(id);
      el.className = isClosed ? 'badge bg-closed' : 'badge bg-open';
      el.innerText = isClosed ? tTrue : tFalse;
    }

    setInterval(updateStatus, 1000);
  </script>
</body>
</html>
)rawliteral";

#endif