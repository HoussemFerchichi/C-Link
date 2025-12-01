#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <HardwareSerial.h>

// ======================
// TGAM / EEG variables
// ======================

HardwareSerial TGAM(1);

byte payload[256];
int payloadLength = 0;

int signalQuality = 200;   // 0 = best, 200 = worst
int rawEEG        = 512;
int attention     = -1;
int meditation    = -1;
int blinkStrength = -1;

unsigned long lastPacketTime = 0;
unsigned long lastSendTime   = 0;

// Simple filter state (high-pass + notch-ish + low-pass)
float hp_last_raw      = 0;
float hp_last_filtered = 0;
float notchState       = 0;
float filteredEEG      = 0;
float lp_alpha         = 0.2f;   // low-pass smoothing (0–1)

// ======================
// Wi-Fi / Web server
// ======================

const char* AP_SSID = "C-LINK";
const char* AP_PASS = "clink123";   // you can change this

WebServer        server(80);
WebSocketsServer webSocket(81);     // ws://IP:81/

// ---------- HTML PAGE (embedded) ----------

const char* INDEX_HTML = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<title>C-Link Neuro Dashboard</title>
<meta name="viewport" content="width=device-width, initial-scale=1" />
<style>
:root {
    --bg-main: #0f172a;
    --accent: #03bfcf;
    --accent-soft: rgba(3,191,207,0.12);
    --card-bg: rgba(255,255,255,0.85);
    --card-border: rgba(255,255,255,0.7);
    --shadow-soft: 0 18px 40px rgba(15,23,42,0.12);
    --text-main: #0f172a;
    --text-soft: #6b7280;
    --danger: #ef4444;
}

* { box-sizing: border-box; }

body {
    margin: 0;
    font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    /* darker background, everything else same */
    background: radial-gradient(circle at top left, #020617, #0f172a 40%, #1e293b 80%);
    color: var(--text-main);
    min-height: 100vh;
}

.app {
    max-width: 1180px;
    margin: 20px auto;
    padding: 16px;
}

.header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 16px;
}

.brand {
    display: flex;
    align-items: center;
    gap: 10px;
}

.brand-icon {
    width: 34px;
    height: 34px;
    border-radius: 999px;
    background: radial-gradient(circle, #22d3ee 0, #0ea5e9 40%, #1e293b 90%);
    display: flex;
    align-items: center;
    justify-content: center;
    color: white;
    font-weight: 700;
    font-size: 16px;
    box-shadow: 0 12px 30px rgba(56,189,248,0.55);
}

h1 {
    font-size: 20px;
    margin: 0;
}

.subtitle {
    font-size: 12px;
    color: var(--text-soft);
}

.status-pill {
    padding: 6px 12px;
    border-radius: 999px;
    background: rgba(34,197,94,0.08);
    color: #16a34a;
    font-size: 12px;
    display: inline-flex;
    align-items: center;
    gap: 6px;
}

.status-dot {
    width: 8px;
    height: 8px;
    border-radius: 999px;
    background: #22c55e;
    box-shadow: 0 0 0 4px rgba(34,197,94,0.25);
}

.layout {
    display: grid;
    grid-template-columns: minmax(0, 1.6fr) minmax(0, 1.1fr);
    gap: 16px;
}

@media (max-width: 920px) {
    .layout {
        grid-template-columns: minmax(0, 1fr);
    }
}

/* Card */

.card {
    background: var(--card-bg);
    border-radius: 22px;
    padding: 16px 18px 14px 18px;
    box-shadow: var(--shadow-soft);
    border: 1px solid var(--card-border);
    backdrop-filter: blur(18px);
}

.card-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 8px;
}

.card-title {
    font-size: 14px;
    font-weight: 600;
    letter-spacing: 0.03em;
    text-transform: uppercase;
    color: var(--text-soft);
}

.badge {
    font-size: 11px;
    padding: 4px 9px;
    border-radius: 999px;
    background: var(--accent-soft);
    color: var(--accent);
}

/* Canvas */

.chart-wrapper {
    border-radius: 18px;
    background: linear-gradient(145deg, #ffffff, #eff6ff);
    padding: 6px 8px 6px 8px;
    border: 1px solid rgba(148,163,184,0.25);
    position: relative;
    overflow: hidden;
}

canvas {
    width: 100%;
    height: 190px;
    display: block;
}

/* Metrics panel */

.metrics-grid {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 10px;
    margin-top: 8px;
}

.metric {
    padding: 10px 12px;
    border-radius: 16px;
    background: linear-gradient(135deg, #ffffff, #eff6ff);
    border: 1px solid rgba(226,232,240,0.9);
    display: flex;
    flex-direction: column;
    gap: 4px;
}

.metric-label {
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.07em;
    color: var(--text-soft);
}

.metric-value {
    font-size: 18px;
    font-weight: 600;
}

.metric-sub {
    font-size: 11px;
    color: var(--text-soft);
}

/* Progress bar */

.bar-track {
    width: 100%;
    height: 7px;
    border-radius: 999px;
    background: #e5e7eb;
    overflow: hidden;
}

.bar-fill {
    height: 100%;
    border-radius: 999px;
    background: linear-gradient(90deg, #22c55e, #0ea5e9);
    width: 40%;
}

/* Mode buttons */

.mode-row {
    display: flex;
    gap: 8px;
    margin-top: 6px;
    flex-wrap: wrap;
}

.mode-btn {
    border-radius: 999px;
    border: 1px solid rgba(148,163,184,0.6);
    background: rgba(255,255,255,0.95);
    padding: 5px 11px;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: var(--text-soft);
    cursor: pointer;
    transition: all 0.15s ease;
}

.mode-btn.active {
    background: var(--accent);
    color: white;
    border-color: var(--accent);
    box-shadow: 0 10px 22px rgba(3,191,207,0.55);
}

.mode-btn:hover {
    border-color: var(--accent);
}

/* Small labels */

.small-tag {
    font-size: 11px;
    color: var(--text-soft);
}
.small-tag span {
    font-weight: 600;
    color: var(--accent);
}

/* Tiny console */

.console {
    margin-top: 10px;
    padding: 8px 10px;
    font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
    font-size: 11px;
    border-radius: 16px;
    background: rgba(15,23,42,0.93);
    color: #e5e7eb;
    max-height: 120px;
    overflow-y: auto;
    border: 1px solid rgba(148,163,184,0.4);
}

.console-line {
    white-space: nowrap;
}

/* Meditation side card */

.stack {
    display: flex;
    flex-direction: column;
    gap: 12px;
}

.kpi-row {
    display: flex;
    justify-content: space-between;
    font-size: 12px;
    color: var(--text-soft);
}

.kpi-row span.value {
    font-weight: 600;
    color: var(--text-main);
}

/* Error state */

.status-pill.error {
    background: rgba(239,68,68,0.10);
    color: var(--danger);
}

.status-pill.error .status-dot {
    background: #ef4444;
    box-shadow: 0 0 0 4px rgba(239,68,68,0.25);
}
</style>
</head>
<body>
<div class="app">
    <div class="header">
        <div class="brand">
            <div class="brand-icon">C</div>
            <div>
                <h1>C-Link Neural Interface</h1>
                <div class="subtitle">Real-time brain activity stream from your headband</div>
            </div>
        </div>
        <div>
            <div id="status-pill" class="status-pill">
                <div class="status-dot" id="status-dot"></div>
                <span id="status-text">Connecting…</span>
            </div>
        </div>
    </div>

    <div class="layout">
        <!-- LEFT: EEG & Attention -->
        <div class="stack">

            <div class="card">
                <div class="card-header">
                    <div class="card-title">Filtered EEG</div>
                    <div class="badge">Realtime • 512 samples</div>
                </div>
                <div class="chart-wrapper">
                    <canvas id="eegCanvas"></canvas>
                </div>
                <div class="mode-row">
                    <button class="mode-btn active" data-mode="raw">Raw</button>
                    <button class="mode-btn" data-mode="filtered">Filtered</button>
                    <button class="mode-btn" data-mode="ready">Ready-to-use</button>
                    <button class="mode-btn" data-mode="strength">Hardware Link</button>
                </div>
                <div class="kpi-row" style="margin-top:6px;">
                    <span class="small-tag">Raw EEG: <span id="val-raw">--</span></span>
                    <span class="small-tag">Filtered: <span id="val-filtered">--</span></span>
                </div>
            </div>

            <div class="card">
                <div class="card-header">
                    <div class="card-title">Cognitive Metrics</div>
                    <div class="badge">Attention / Meditation / Blink / SQ</div>
                </div>
                <div class="metrics-grid">
                    <div class="metric">
                        <div class="metric-label">Attention</div>
                        <div class="metric-value" id="val-att">--</div>
                        <div class="metric-sub">Focus intensity (0-100)</div>
                    </div>
                    <div class="metric">
                        <div class="metric-label">Meditation</div>
                        <div class="metric-value" id="val-med">--</div>
                        <div class="metric-sub">Calmness level (0-100)</div>
                    </div>
                    <div class="metric">
                        <div class="metric-label">Blink</div>
                        <div class="metric-value" id="val-blink">--</div>
                        <div class="metric-sub">Latest blink strength</div>
                    </div>
                    <div class="metric">
                        <div class="metric-label">Signal Quality</div>
                        <div class="metric-value" id="val-sq">--</div>
                        <div class="metric-sub">Lower is better (0 = perfect)</div>
                    </div>
                </div>
                <div class="kpi-row" style="margin-top:8px;">
                    <span class="small-tag">Packets / s: <span id="val-fps">0</span></span>
                    <span class="small-tag">Uptime: <span id="val-uptime">0s</span></span>
                </div>
            </div>

        </div>

        <!-- RIGHT: Graphs + Link strength -->
        <div class="stack">

            <div class="card">
                <div class="card-header">
                    <div class="card-title">Mind-state Graphs</div>
                    <div class="badge">Smooth live traces</div>
                </div>
                <div class="chart-wrapper" style="margin-bottom:8px;">
                    <canvas id="attCanvas"></canvas>
                </div>
                <div class="chart-wrapper">
                    <canvas id="medCanvas"></canvas>
                </div>
            </div>

            <div class="card">
                <div class="card-header">
                    <div class="card-title">Hardware Link</div>
                    <div class="badge">ESP32 ↔ TGAM</div>
                </div>
                <div class="metric" style="margin-bottom:8px;">
                    <div class="metric-label">Link Strength</div>
                    <div class="metric-value" id="val-hw">--</div>
                    <div class="bar-track" style="margin-top:6px;">
                        <div class="bar-fill" id="hw-bar"></div>
                    </div>
                    <div class="metric-sub" id="hw-sub">Waiting for data…</div>
                </div>

                <div class="console" id="console"></div>
            </div>

        </div>
    </div>
</div>

<script>
// =========================
// Basic chart helpers
// =========================
const EEG_LEN = 512;
const METRIC_LEN = 256;

let eegData   = new Array(EEG_LEN).fill(0);
let attData   = new Array(METRIC_LEN).fill(0);
let medData   = new Array(METRIC_LEN).fill(0);

let lastEEGSmooth = 0;

// Smooth a value (0..1 = how much of new sample we keep)
function smooth(prev, next, alpha) {
    return prev + alpha * (next - prev);
}

// Canvas setup
function setupCanvas(id, heightPx = 190) {
    const canvas = document.getElementById(id);
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    canvas.width  = rect.width * dpr;
    canvas.height = heightPx * dpr;
    const ctx = canvas.getContext('2d');
    ctx.scale(dpr, dpr);
    return { canvas, ctx, dpr, width: rect.width, height: heightPx };
}

const eegChart = setupCanvas('eegCanvas');
const attChart = setupCanvas('attCanvas', 130);
const medChart = setupCanvas('medCanvas', 130);

// Draw line chart
function drawSeries(chart, data, minY, maxY, color) {
    const { canvas, ctx, width, height } = chart;
    const w = width;
    const h = height;

    ctx.clearRect(0, 0, w, h);

    if (!data || data.length === 0) return;

    ctx.lineWidth = 1.0;
    ctx.strokeStyle = color;
    ctx.beginPath();

    const len = data.length;
    for (let i = 0; i < len; i++) {
        const x = (i / (len - 1)) * (w - 2) + 1;
        let v = data[i];
        if (v < minY) v = minY;
        if (v > maxY) v = maxY;
        const t = (v - minY) / (maxY - minY || 1);
        const y = h - 4 - t * (h - 8);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }

    ctx.stroke();
}

// =========================
// UI elements
// =========================

const statusPill = document.getElementById('status-pill');
const statusDot  = document.getElementById('status-dot');
const statusText = document.getElementById('status-text');

const valRaw   = document.getElementById('val-raw');
const valFilt  = document.getElementById('val-filtered');
const valAtt   = document.getElementById('val-att');
const valMed   = document.getElementById('val-med');
const valBlink = document.getElementById('val-blink');
const valSQ    = document.getElementById('val-sq');
const valHW    = document.getElementById('val-hw');
const hwBar    = document.getElementById('hw-bar');
const hwSub    = document.getElementById('hw-sub');
const valFPS   = document.getElementById('val-fps');
const valUptime= document.getElementById('val-uptime');
const consoleEl= document.getElementById('console');

let lastPacketTime = performance.now();
let packetCount = 0;
let lastFpsUpdate = performance.now();

// modes
let currentMode = 'raw';
document.querySelectorAll('.mode-btn').forEach(btn => {
    btn.addEventListener('click', () => {
        document.querySelectorAll('.mode-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        currentMode = btn.dataset.mode;
    });
});

// =========================
// WebSocket connection
// =========================

let ws = null;
let wsTimer = null;

function logLine(text) {
    const line = document.createElement('div');
    line.className = 'console-line';
    const ts = new Date().toLocaleTimeString();
    line.textContent = `[${ts}] ${text}`;
    consoleEl.appendChild(line);
    while (consoleEl.children.length > 80) {
        consoleEl.removeChild(consoleEl.firstChild);
    }
    consoleEl.scrollTop = consoleEl.scrollHeight;
}

function setStatus(connected) {
    if (connected) {
        statusPill.classList.remove('error');
        statusText.textContent = 'Streaming from headband';
    } else {
        statusPill.classList.add('error');
        statusText.textContent = 'Disconnected – retrying…';
    }
}

function connectWS() {
    // ESP32 AP mode: page is at 192.168.4.1 → WebSocket at same IP, port 81, root path
    const url = "ws://192.168.4.1:81/ws";
    ws = new WebSocket(url);

    ws.onopen = () => {
        setStatus(true);
        logLine('WebSocket connected');
    };

    ws.onclose = () => {
        setStatus(false);
        logLine('WebSocket closed, will retry…');
        ws = null;
        clearTimeout(wsTimer);
        wsTimer = setTimeout(connectWS, 1500);
    };

    ws.onerror = (err) => {
        console.warn(err);
    };

    ws.onmessage = (event) => {
        try {
            const p = JSON.parse(event.data);
            handlePacket(p);
        } catch (e) {
            console.warn('Bad packet', e);
        }
    };
}

connectWS();

// =========================
// Packet handling
// =========================

let lastFilteredDisplay = 0;

function handlePacket(p) {
    const now = performance.now();
    lastPacketTime = now;
    packetCount++;

    // Update FPS every second
    if (now - lastFpsUpdate > 1000) {
        valFPS.textContent = packetCount.toString();
        packetCount = 0;
        lastFpsUpdate = now;
    }

    // Uptime (page-based)
    valUptime.textContent = Math.round(now / 1000) + 's';

    // Numbers (with fallbacks)
    const raw   = typeof p.raw === 'number'   ? p.raw   : null;
    const f_raw = typeof p.f_raw === 'number' ? p.f_raw : null;
    const att   = typeof p.att === 'number'   ? p.att   : null;
    const med   = typeof p.med === 'number'   ? p.med   : null;
    const blink = typeof p.blink === 'number' ? p.blink : null;
    const sq    = typeof p.sq === 'number'    ? p.sq    : null;
    const hw    = typeof p.hw === 'number'    ? p.hw    : null;

    if (raw  !== null) valRaw.textContent  = raw.toString();
    if (f_raw!== null) {
        lastFilteredDisplay = smooth(lastFilteredDisplay, f_raw, 0.30);
        valFilt.textContent = Math.round(lastFilteredDisplay).toString();
    }
    if (att  !== null) valAtt.textContent   = att.toString();
    if (med  !== null) valMed.textContent   = med.toString();
    if (blink!== null) valBlink.textContent = blink.toString();
    if (sq   !== null) valSQ.textContent    = sq.toString();

    if (hw !== null) {
        valHW.textContent = hw + '%';
        hwBar.style.width = hw + '%';
        if (hw >= 80) {
            hwSub.textContent = 'Link is excellent';
        } else if (hw >= 40) {
            hwSub.textContent = 'Link is usable – adjust electrodes if needed';
        } else {
            hwSub.textContent = 'Weak link – check power / GND / TX wiring';
        }
    }

    // ==== Graphs ====

    // EEG graph depends on mode
    let eegSampleCore = 0;
    let eegMin = -300;
    let eegMax = 300;

    if (currentMode === 'raw') {
        eegSampleCore = raw !== null ? raw : 0;
    } else if (currentMode === 'filtered') {
        eegSampleCore = (f_raw !== null) ? f_raw : (raw !== null ? raw : 0);
    } else if (currentMode === 'ready') {
        // show attention as main trace
        eegSampleCore = att !== null ? att : 0;
        eegMin = 0;
        eegMax = 100;
    } else if (currentMode === 'strength') {
        eegSampleCore = hw !== null ? hw : 0;
        eegMin = 0;
        eegMax = 100;
    }

    lastEEGSmooth = smooth(lastEEGSmooth, eegSampleCore, 0.35);
    eegData.push(lastEEGSmooth);
    if (eegData.length > EEG_LEN) eegData.shift();
    drawSeries(eegChart, eegData, eegMin, eegMax, '#00d0ff');

    // Attention
    if (att !== null) {
        attData.push(att);
        if (attData.length > METRIC_LEN) attData.shift();
        drawSeries(attChart, attData, 0, 100, '#22c55e');
    }

    // Meditation
    if (med !== null) {
        medData.push(med);
        if (medData.length > METRIC_LEN) medData.shift();
        drawSeries(medChart, medData, 0, 100, '#ec4899');
    }
}

// Watchdog: if no packets, show disconnected
setInterval(() => {
    const now = performance.now();
    if (now - lastPacketTime > 2500) {
        setStatus(false);
    }
}, 1500);
</script>
</body>
</html>
)HTML";

// ======================
// Helper: map SQ→strength
// ======================

int mapSQtoStrength(int sq) {
    if (sq < 0 || sq > 200) return 0;
    int s = 100 - (sq / 2);    // sq=0 →100, sq=200→0
    if (s < 0)   s = 0;
    if (s > 100) s = 100;
    return s;
}

// ======================
// TGAM parsing & filter
// ======================

int filterEEG(int raw) {
    // very simple high-pass-ish
    float hp = raw - hp_last_raw + 0.99f * hp_last_filtered;
    hp_last_raw      = raw;
    hp_last_filtered = hp;

    // crude notch / extra smoothing
    notchState = hp - 0.95f * notchState;

    // low-pass
    filteredEEG = lp_alpha * notchState + (1.0f - lp_alpha) * filteredEEG;
    return (int)filteredEEG;
}

void parsePacket(byte *payload, int len) {
  int i = 0;

  while (i < len) {
    byte code = payload[i++];

    switch (code) {

      case 0x02: // Signal quality
        signalQuality = payload[i++];
        break;

      case 0x04: // Attention
        attention = payload[i++];
        break;

      case 0x05: // Meditation
        meditation = payload[i++];
        break;

      case 0x16: // Blink
        blinkStrength = payload[i++];
        break;

      case 0x80: { // Raw EEG (two bytes)
        int high = payload[i++];
        int low  = payload[i++];
        int value = (high << 8) | low;
        if (value > 32767) value -= 65536;
        rawEEG = value;
        break;
      }

      default:
        i++;
        break;
    }
  }
}

// ======================
// WebSockets event
// ======================

void onWsEvent(uint8_t num, WStype_t type, uint8_t * payloadData, size_t length) {
    if (type == WStype_CONNECTED) {
        Serial.printf("[WS] Client %u connected\n", num);
    } else if (type == WStype_DISCONNECTED) {
        Serial.printf("[WS] Client %u disconnected\n", num);
    }
}

// ======================
// Setup / Loop
// ======================

void setup() {
    Serial.begin(115200);
    delay(300);

    // TGAM UART
    TGAM.begin(57600, SERIAL_8N1, 16, 17);
    Serial.println("C-Link ESP32 + TGAM + Web UI");

    // Wi-Fi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP: ");
    Serial.println(IP);

    // HTTP server
    server.on("/", []() {
        server.send(200, "text/html", INDEX_HTML);
    });
    server.begin();

    // WebSocket server
    webSocket.begin();
    webSocket.onEvent(onWsEvent);
}

void loop() {
    server.handleClient();
    webSocket.loop();

    // TGAM sync
    if (TGAM.available() && TGAM.read() == 0xAA) {
        while (!TGAM.available());
        if (TGAM.read() != 0xAA) return;

        while (!TGAM.available());
        payloadLength = TGAM.read();
        if (payloadLength <= 0 || payloadLength > 169) return;

        for (int i = 0; i < payloadLength; i++) {
            while (!TGAM.available());
            payload[i] = TGAM.read();
        }

        while (!TGAM.available());
        byte checksum = TGAM.read();
        (void)checksum;

        lastPacketTime = millis();
        parsePacket(payload, payloadLength);
    }

    // SEND JSON (50 Hz)
    unsigned long now = millis();
    if (now - lastSendTime >= 20) {
        lastSendTime = now;

        int f = filterEEG(rawEEG);

        // old working hardwareStrength logic
        int hwStrength;
        unsigned long age = now - lastPacketTime;

        if (age < 50)        hwStrength = 100;
        else if (age < 100) hwStrength = 70;
        else if (age < 200) hwStrength = 40;
        else if (age < 500) hwStrength = 10;
        else                hwStrength = 0;

        // JSON
        String json = "{";
        json += "\"raw\":";   json += rawEEG;                json += ",";
        json += "\"f_raw\":"; json += f;                     json += ",";
        json += "\"sq\":";    json += signalQuality;         json += ",";
        json += "\"att\":";   json += (attention     < 0 ? 0 : attention);     json += ",";
        json += "\"med\":";   json += (meditation    < 0 ? 0 : meditation);    json += ",";
        json += "\"blink\":"; json += (blinkStrength < 0 ? 0 : blinkStrength); json += ",";
        json += "\"hw\":";    json += hwStrength;
        json += "}";


        webSocket.broadcastTXT(json);
        Serial.println(json);
    }
}

