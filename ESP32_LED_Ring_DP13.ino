/*
 * ESP32 LED Ring Web Controller
 * Hardware : ESP32 Dev Board + WS2812B LED Ring
 * Libraries: FastLED
 *
 * Wiring:
 *   LED Ring DATA  ->  GPIO 13
 *   LED Ring VCC   ->  5V
 *   LED Ring GND   ->  GND
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FastLED.h>
#include <math.h>

// ── Configuration ─────────────────────────────────────────
#define LED_PIN     13
#define NUM_LEDS    24
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

const char* WIFI_SSID = "2.4";
const char* WIFI_PASS = "Xt_ew25rl!kqZrV";
// ──────────────────────────────────────────────────────────

CRGB leds[NUM_LEDS];
WebServer server(80);

bool    ledPower      = true;
CRGB    currentColor  = CRGB(74, 140, 58);   // #4a8c3a
uint8_t brightness    = 180;
String  currentEffect = "solid";

unsigned long lastUpdate = 0;
int chasePos = 0;
int rainbowHue = 0;

// ── Gaslight Parameters ───────────────────────────────────
int gasMaxBrightness   = 255;
int gasMinBrightness   = 190;
int gasDeepBrightness  = 15;

int gasTargetSeconds   = 777;
int gasTotalCycles     = 7;
int gasCycleCounter    = 1;

int gasNormalDelayMin  = 30;
int gasNormalDelayMax  = 120;
int gasDeepDelayMin    = 100;
int gasDeepDelayMax    = 400;

bool gasCycleRunning   = true;
unsigned long gasCycleStart = 0;
unsigned long gasNextFlickerAt = 0;
// ──────────────────────────────────────────────────────────

const char HTML_PAGE[] PROGMEM = R"===(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>OSS FIELD UNIT — OCCULT DIV.</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=Special+Elite&family=Courier+Prime:ital,wght@0,400;0,700;1,400&display=swap');

:root {
  --parchment:  #c8b89a;
  --parchment2: #b8a485;
  --ink:        #1a1208;
  --ink-faded:  #3a2e1a;
  --red-stamp:  #8b1a1a;
  --green-glow: #4a7a3a;
  --green-lit:  #7aba5a;
  --amber:      #c87820;
  --blood:      #6b1010;
  --void:       #080508;
}

* { box-sizing: border-box; margin: 0; padding: 0; }

body {
  background: var(--void);
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 20px;
  font-family: 'Courier Prime', monospace;
  overflow-x: hidden;
  position: relative;
}

body::before {
  content: '';
  position: fixed;
  inset: 0;
  background:
    radial-gradient(ellipse at 30% 40%, rgba(26,42,10,0.5) 0%, transparent 55%),
    radial-gradient(ellipse at 70% 60%, rgba(40,10,10,0.4) 0%, transparent 55%);
  animation: voidPulse 8s ease-in-out infinite alternate;
  pointer-events: none;
  z-index: 0;
}
@keyframes voidPulse { 0%{opacity:.7} 100%{opacity:1} }

body::after {
  content: '';
  position: fixed; inset: 0;
  background-image: url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.85' numOctaves='4' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.07'/%3E%3C/svg%3E");
  opacity: 0.45;
  pointer-events: none;
  z-index: 1;
}

.eldritch-bg {
  position: fixed;
  bottom: -60px; right: -60px;
  width: 260px; height: 260px;
  opacity: 0.045;
  pointer-events: none;
  z-index: 1;
  animation: slowRot 40s linear infinite;
}
@keyframes slowRot { to { transform: rotate(360deg); } }

.page {
  position: relative;
  z-index: 2;
  width: 100%;
  max-width: 520px;
}

.dossier {
  background: var(--parchment);
  border: 2px solid var(--parchment2);
  padding: 36px 30px 28px;
  position: relative;
  box-shadow:
    0 0 0 1px rgba(26,18,8,0.4),
    0 12px 50px rgba(0,0,0,0.85),
    inset 0 0 80px rgba(26,18,8,0.12);
}

.dossier::before {
  content: '';
  position: absolute; inset: 0;
  background: repeating-linear-gradient(
    0deg, transparent, transparent 27px,
    rgba(26,18,8,0.055) 27px, rgba(26,18,8,0.055) 28px
  );
  pointer-events: none;
}

.dossier::after {
  content: '';
  position: absolute;
  width: 88px; height: 68px;
  border-radius: 50%;
  top: 14px; right: 22px;
  background: radial-gradient(ellipse, rgba(100,60,10,0.16) 0%, rgba(100,60,10,0.05) 50%, transparent 70%);
  pointer-events: none;
}

.stamp-top {
  position: absolute;
  top: -1px; left: 50%;
  transform: translateX(-50%);
  background: var(--red-stamp);
  color: #f0d8b0;
  font-family: 'Special Elite', serif;
  font-size: 0.53rem;
  letter-spacing: 0.22em;
  padding: 2px 16px;
  white-space: nowrap;
}

.stamp-diagonal {
  position: absolute;
  top: 42px; right: -6px;
  transform: rotate(13deg);
  border: 3px solid var(--red-stamp);
  color: var(--red-stamp);
  font-family: 'Special Elite', serif;
  font-size: 0.68rem;
  letter-spacing: 0.25em;
  padding: 2px 10px;
  opacity: 0.82;
  pointer-events: none;
}

.header {
  text-align: center;
  margin-bottom: 20px;
  border-bottom: 2px solid rgba(26,18,8,0.3);
  padding-bottom: 16px;
}

.header-eyebrow {
  font-size: 0.58rem;
  letter-spacing: 0.22em;
  color: var(--ink-faded);
  margin-bottom: 6px;
  text-transform: uppercase;
}

.header-title {
  font-family: 'Special Elite', serif;
  font-size: 1.25rem;
  color: var(--ink);
  letter-spacing: 0.06em;
  line-height: 1.25;
  text-transform: uppercase;
  animation: flicker 6s linear infinite;
}

@keyframes flicker {
  0%,19%,21%,23%,25%,54%,56%,100% { opacity:1 }
  20%,24%,55% { opacity:.4 }
}

.header-sub {
  font-size: 0.6rem;
  letter-spacing: 0.12em;
  color: var(--ink-faded);
  margin-top: 6px;
  font-style: italic;
}

.redacted {
  background: var(--ink);
  color: var(--ink);
  border-radius: 1px;
  padding: 0 4px;
  user-select: none;
}

.ring-section {
  display: flex;
  justify-content: center;
  margin: 16px 0 20px;
}

.ring-frame {
  position: relative;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}

.ring-frame::before {
  content: 'SIGNAL APERTURE';
  position: absolute;
  top: -18px; left: 50%;
  transform: translateX(-50%);
  font-size: 0.48rem;
  letter-spacing: 0.22em;
  color: rgba(26,18,8,0.45);
  white-space: nowrap;
  text-transform: uppercase;
}

.ring-svg {
  width: 148px; height: 148px;
  transition: filter 0.5s;
  filter: drop-shadow(0 0 10px rgba(74,122,58,0.5));
}

.ring-outer {
  position: absolute;
  inset: -8px;
  border: 1px dashed rgba(26,18,8,0.22);
  border-radius: 50%;
}

.field-label {
  font-size: 0.54rem;
  letter-spacing: 0.22em;
  text-transform: uppercase;
  color: var(--ink-faded);
  margin-bottom: 10px;
  display: flex;
  align-items: center;
  gap: 5px;
}
.field-label::before { content:'▸'; font-size:.55rem; }

.divider {
  border: none;
  border-top: 1px solid rgba(26,18,8,0.25);
  margin: 18px 0;
  position: relative;
}
.divider::after {
  content: '◆';
  position: absolute;
  left: 50%; top: 50%;
  transform: translate(-50%,-50%);
  background: var(--parchment);
  padding: 0 6px;
  font-size: 0.48rem;
  color: rgba(26,18,8,0.3);
}

.power-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.power-status {
  font-family: 'Special Elite', serif;
  font-size: 0.95rem;
  color: var(--ink);
}
.power-status .pstate {
  font-family: 'Courier Prime', monospace;
  font-size: 0.7rem;
  color: var(--green-glow);
  display: block;
  margin-top: 2px;
}
.power-status .pstate.off { color: var(--blood); }

.mil-toggle { position:relative; width:60px; height:30px; }
.mil-toggle input { opacity:0; width:0; height:0; }
.mil-track {
  position:absolute; inset:0;
  background:#8a7a60;
  border:2px solid var(--ink-faded);
  border-radius:3px;
  cursor:pointer;
  transition:background .3s;
  box-shadow:inset 0 2px 4px rgba(0,0,0,.4);
}
.mil-track::before {
  content:'';
  position:absolute;
  width:22px; height:22px;
  left:2px; top:2px;
  background:linear-gradient(135deg,#c8b060,#907840);
  border:1px solid var(--ink-faded);
  border-radius:2px;
  transition:transform .25s;
  box-shadow:0 2px 4px rgba(0,0,0,.5);
}
.mil-toggle input:checked + .mil-track {
  background:#3a5a28;
  border-color:var(--green-glow);
}
.mil-toggle input:checked + .mil-track::before {
  transform:translateX(30px);
  background:linear-gradient(135deg,#a0c870,#60882a);
  box-shadow:0 0 8px rgba(74,122,58,.6),0 2px 4px rgba(0,0,0,.4);
}

.colour-swatches {
  display:flex; gap:9px; flex-wrap:wrap; margin-bottom:12px;
}
.swatch {
  width:28px; height:28px;
  border-radius:2px;
  border:2px solid rgba(26,18,8,.35);
  cursor:pointer;
  transition:transform .15s,border-color .15s,box-shadow .15s;
  position:relative;
}
.swatch:hover { transform:scale(1.12); }
.swatch.active {
  border-color:var(--ink);
  transform:scale(1.12);
  box-shadow:0 0 0 1px var(--ink),2px 2px 0 var(--ink);
}
.swatch::after {
  content:attr(data-num);
  position:absolute;
  bottom:-14px; left:50%;
  transform:translateX(-50%);
  font-size:.43rem;
  color:var(--ink-faded);
  white-space:nowrap;
}

.colour-row {
  display:flex; align-items:center; gap:12px; margin-top:16px;
}
input[type="color"] {
  -webkit-appearance:none;
  width:36px; height:36px;
  border:2px solid var(--ink-faded);
  border-radius:2px;
  background:none; cursor:pointer; padding:0;
}
input[type="color"]::-webkit-color-swatch-wrapper { padding:2px; border-radius:2px; }
input[type="color"]::-webkit-color-swatch { border:none; border-radius:1px; }

.hex-code {
  font-size:.78rem;
  color:var(--ink-faded);
  letter-spacing:.1em;
  font-style:italic;
}

.bright-row { display:flex; align-items:center; gap:10px; }
input[type="range"] {
  -webkit-appearance:none; flex:1; height:8px;
  background:linear-gradient(to right,#5a4a30,#c8a860);
  border:1px solid rgba(26,18,8,.35);
  border-radius:0; cursor:pointer; outline:none;
}
input[type="range"]::-webkit-slider-thumb {
  -webkit-appearance:none;
  width:16px; height:22px;
  background:linear-gradient(180deg,#d8c080,#907840);
  border:2px solid var(--ink-faded);
  border-radius:2px;
  cursor:pointer;
  box-shadow:1px 1px 3px rgba(0,0,0,.5);
}
input[type="range"]::-webkit-slider-thumb:hover {
  background:linear-gradient(180deg,#e8d090,#a08850);
}
.bright-val {
  font-size:.73rem; color:var(--ink-faded);
  min-width:32px; text-align:right;
}

.effects-grid { display:grid; grid-template-columns:1fr 1fr; gap:8px; }
.effect-btn {
  background:rgba(26,18,8,.07);
  border:1px solid rgba(26,18,8,.3);
  border-radius:2px;
  padding:10px 10px;
  font-family:'Courier Prime',monospace;
  font-size:.7rem;
  color:var(--ink-faded);
  cursor:pointer;
  letter-spacing:.06em;
  text-transform:uppercase;
  transition:all .18s;
  text-align:left;
  line-height:1.4;
}
.effect-btn .code {
  display:block; font-size:.48rem;
  letter-spacing:.2em; opacity:.55;
  margin-bottom:2px;
}
.effect-btn:hover {
  background:rgba(26,18,8,.14);
  border-color:var(--ink-faded);
  color:var(--ink);
}
.effect-btn.active {
  background:var(--ink);
  border-color:var(--ink);
  color:var(--parchment);
  box-shadow:inset 0 1px 3px rgba(0,0,0,.5);
}

.gas-grid {
  display:grid;
  grid-template-columns:1fr;
  gap:8px;
  margin-top:8px;
}

.gas-row {
  display:grid;
  grid-template-columns:1.5fr 90px 48px;
  align-items:center;
  gap:10px;
}

.gas-row label {
  font-size:.68rem;
  letter-spacing:.06em;
  color:var(--ink-faded);
  text-transform:uppercase;
}

.gas-row input[type="number"] {
  width:100%;
  background:rgba(26,18,8,0.06);
  border:1px solid rgba(26,18,8,0.28);
  color:var(--ink);
  padding:6px 8px;
  font-family:'Courier Prime', monospace;
  font-size:.78rem;
}

.gas-unit {
  font-size:.62rem;
  color:var(--ink-faded);
  letter-spacing:.12em;
  text-transform:uppercase;
}

.gas-actions {
  display:grid;
  grid-template-columns:1fr 1fr 1fr;
  gap:8px;
  margin-top:12px;
}

.gas-action { text-align:left; }

.gas-telemetry {
  margin-top:10px;
  border:1px solid rgba(26,18,8,0.2);
  background:rgba(26,18,8,0.05);
  padding:8px 10px;
  font-size:.62rem;
  letter-spacing:.12em;
  color:var(--ink-faded);
  text-transform:uppercase;
  display:grid;
  gap:4px;
}

.status-section {
  margin-top:4px;
  border-top:1px solid rgba(26,18,8,.2);
  padding-top:12px;
  display:flex;
  align-items:center;
  justify-content:space-between;
}
.status-left { display:flex; align-items:center; gap:8px; }
.status-light {
  width:8px; height:8px;
  border-radius:50%;
  background:var(--blood);
  box-shadow:0 0 4px var(--blood);
  transition:all .4s; flex-shrink:0;
}
.status-light.online {
  background:var(--green-lit);
  box-shadow:0 0 8px var(--green-glow);
  animation:blink 3s ease-in-out infinite;
}
@keyframes blink { 0%,90%,100%{opacity:1} 95%{opacity:.3} }
.status-text {
  font-size:.56rem; letter-spacing:.14em;
  color:var(--ink-faded); text-transform:uppercase;
}
.status-right {
  font-size:.5rem; letter-spacing:.1em;
  color:rgba(26,18,8,.3); text-align:right; font-style:italic;
}

.footer-stamp {
  text-align:center; margin-top:14px;
  font-size:.5rem; letter-spacing:.18em;
  color:rgba(200,184,154,.25);
  text-transform:uppercase;
  font-family:'Special Elite',serif;
}
</style>
</head>
<body>

<svg class="eldritch-bg" viewBox="0 0 200 200" fill="none" xmlns="http://www.w3.org/2000/svg">
  <g stroke="#c8b89a" stroke-width="1.2">
    <path d="M100 10 Q130 50 100 100 Q70 50 100 10Z" opacity=".8"/>
    <path d="M100 10 Q150 60 130 110 Q80 80 100 10Z" opacity=".6"/>
    <path d="M100 10 Q50 60 70 110 Q120 80 100 10Z" opacity=".6"/>
    <path d="M100 10 Q160 30 170 80 Q120 90 100 10Z" opacity=".4"/>
    <path d="M100 10 Q40 30 30 80 Q80 90 100 10Z" opacity=".4"/>
    <circle cx="100" cy="100" r="30" stroke-dasharray="4 6"/>
    <circle cx="100" cy="100" r="55" stroke-dasharray="2 8"/>
    <circle cx="100" cy="100" r="80" stroke-dasharray="1 10"/>
    <circle cx="100" cy="100" r="8" fill="rgba(200,184,154,0.3)"/>
  </g>
</svg>

<div class="page">
  <div class="dossier">

    <div class="stamp-top">TOP SECRET — OSS CLASSIFICATION DELTA-7</div>
    <div class="stamp-diagonal">RESTRICTED</div>

    <div class="header">
      <div class="header-eyebrow">Office of Strategic Services — Occult Division</div>
      <div class="header-title">Aetheric Signal<br>Control Apparatus</div>
      <div class="header-sub">
        Field Unit Ref: <span class="redacted">███████</span> &nbsp;·&nbsp; Operative: <span class="redacted">████████████</span>
      </div>
    </div>

    <div class="ring-section">
      <div class="ring-frame">
        <div class="ring-outer"></div>
        <svg class="ring-svg" id="ringSvg" viewBox="0 0 160 160">
          <defs>
            <filter id="ledGlow">
              <feGaussianBlur stdDeviation="2.5" result="b"/>
              <feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge>
            </filter>
          </defs>
          <g id="ticks" stroke="rgba(26,18,8,0.18)" stroke-width="1"></g>
          <g id="ringDots" filter="url(#ledGlow)"></g>
        </svg>
      </div>
    </div>

    <hr class="divider">

    <div class="field-label">Apparatus Power — Circuit Alpha</div>
    <div class="power-row">
      <div class="power-status">
        Ritual Illumination
        <span class="pstate" id="powerLabel">ACTIVE</span>
      </div>
      <label class="mil-toggle">
        <input type="checkbox" id="powerToggle" checked>
        <span class="mil-track"></span>
      </label>
    </div>

    <hr class="divider">

    <div class="field-label">Spectral Frequency — Chromatic Band</div>
    <div class="colour-swatches" id="swatches"></div>
    <div class="colour-row">
      <input type="color" id="colorPicker" value="#4a8c3a">
      <span class="hex-code" id="hexDisplay">[ 4A8C3A ]</span>
    </div>

    <hr class="divider">

    <div class="field-label">Intensity Modulator — Candle Power</div>
    <div class="bright-row">
      <input type="range" id="brightness" min="0" max="255" value="180">
      <span class="bright-val" id="brightnessVal">180</span>
    </div>

    <hr class="divider">

    <div class="field-label">Manifestation Protocol — Select Ritual</div>
    <div class="effects-grid">
      <button class="effect-btn active" data-effect="solid">
        <span class="code">PROTO-01</span>Steady Glow
      </button>
      <button class="effect-btn" data-effect="pulse">
        <span class="code">PROTO-02</span>Heartbeat
      </button>
      <button class="effect-btn" data-effect="rainbow">
        <span class="code">PROTO-03</span>Prismatic Rift
      </button>
      <button class="effect-btn" data-effect="chase">
        <span class="code">PROTO-04</span>Elder Sign Chase
      </button>
      <button class="effect-btn" data-effect="sparkle">
        <span class="code">PROTO-05</span>Spectral Flicker
      </button>
      <button class="effect-btn" data-effect="gaslight">
        <span class="code">PROTO-06</span>Gaslight Vigil
      </button>
      <button class="effect-btn" data-effect="off_effect">
        <span class="code">PROTO-00</span>Extinguish
      </button>
    </div>

    <hr class="divider">

    <div class="field-label">Gaslight Parameters — Ritual Timing Matrix</div>

    <div class="gas-grid">
      <div class="gas-row">
        <label for="gasTargetSeconds">Cycle Duration</label>
        <input type="number" id="gasTargetSeconds" min="1" max="99999" value="777">
        <span class="gas-unit">sec</span>
      </div>

      <div class="gas-row">
        <label for="gasTotalCycles">Cycles Before Descent</label>
        <input type="number" id="gasTotalCycles" min="1" max="99" value="7">
        <span class="gas-unit">count</span>
      </div>

      <div class="gas-row">
        <label for="gasMinBrightness">Normal Min Brightness</label>
        <input type="number" id="gasMinBrightness" min="0" max="255" value="190">
        <span class="gas-unit">0–255</span>
      </div>

      <div class="gas-row">
        <label for="gasMaxBrightness">Normal Max Brightness</label>
        <input type="number" id="gasMaxBrightness" min="0" max="255" value="255">
        <span class="gas-unit">0–255</span>
      </div>

      <div class="gas-row">
        <label for="gasDeepBrightness">Descent Brightness</label>
        <input type="number" id="gasDeepBrightness" min="0" max="255" value="15">
        <span class="gas-unit">0–255</span>
      </div>

      <div class="gas-row">
        <label for="gasNormalDelayMin">Normal Delay Min</label>
        <input type="number" id="gasNormalDelayMin" min="1" max="5000" value="30">
        <span class="gas-unit">ms</span>
      </div>

      <div class="gas-row">
        <label for="gasNormalDelayMax">Normal Delay Max</label>
        <input type="number" id="gasNormalDelayMax" min="1" max="5000" value="120">
        <span class="gas-unit">ms</span>
      </div>

      <div class="gas-row">
        <label for="gasDeepDelayMin">Descent Delay Min</label>
        <input type="number" id="gasDeepDelayMin" min="1" max="5000" value="100">
        <span class="gas-unit">ms</span>
      </div>

      <div class="gas-row">
        <label for="gasDeepDelayMax">Descent Delay Max</label>
        <input type="number" id="gasDeepDelayMax" min="1" max="5000" value="400">
        <span class="gas-unit">ms</span>
      </div>
    </div>

    <div class="gas-actions">
      <button class="effect-btn gas-action" id="gasStartBtn">
        <span class="code">RITE-01</span>Begin Vigil
      </button>
      <button class="effect-btn gas-action" id="gasPauseBtn">
        <span class="code">RITE-02</span>Hold Pattern
      </button>
      <button class="effect-btn gas-action" id="gasResetBtn">
        <span class="code">RITE-03</span>Reset Cycle
      </button>
    </div>

    <div class="gas-telemetry">
      <div>Cycle: <span id="gasCycleReadout">1 / 7</span></div>
      <div>Elapsed: <span id="gasElapsedReadout">0 / 777s</span></div>
      <div>State: <span id="gasStateReadout">ACTIVE</span></div>
    </div>

    <hr class="divider" style="margin-bottom:14px">

    <div class="status-section">
      <div class="status-left">
        <div class="status-light" id="statusLight"></div>
        <div class="status-text" id="statusText">AWAITING FIELD UNIT...</div>
      </div>
      <div class="status-right">DO NOT DISCUSS<br>WITH UNINITIATED</div>
    </div>

  </div>
  <div class="footer-stamp">
    Miskatonic University Applied Sciences Div. — <span class="redacted">██████████</span> — 1943
  </div>
</div>

<script>
  const NUM_LEDS = 24;
  const CX = 80, CY = 80, R_LED = 62, DOT_R = 6.5;

  const tickG = document.getElementById('ticks');
  for (let i = 0; i < 48; i++) {
    const a = (i / 48) * 2 * Math.PI - Math.PI / 2;
    const r1 = 74, r2 = (i % 3 === 0) ? 69 : 72;
    const l = document.createElementNS('http://www.w3.org/2000/svg','line');
    l.setAttribute('x1', (CX + r1*Math.cos(a)).toFixed(2));
    l.setAttribute('y1', (CY + r1*Math.sin(a)).toFixed(2));
    l.setAttribute('x2', (CX + r2*Math.cos(a)).toFixed(2));
    l.setAttribute('y2', (CY + r2*Math.sin(a)).toFixed(2));
    tickG.appendChild(l);
  }

  const ringDots = document.getElementById('ringDots');
  const circles = [];
  for (let i = 0; i < NUM_LEDS; i++) {
    const angle = (i / NUM_LEDS) * 2 * Math.PI - Math.PI / 2;
    const x = CX + R_LED * Math.cos(angle);
    const y = CY + R_LED * Math.sin(angle);
    const c = document.createElementNS('http://www.w3.org/2000/svg','circle');
    c.setAttribute('cx', x.toFixed(2));
    c.setAttribute('cy', y.toFixed(2));
    c.setAttribute('r', DOT_R);
    c.setAttribute('fill', '#4a8c3a');
    c.style.transition = 'fill 0.4s, opacity 0.4s';
    ringDots.appendChild(c);
    circles.push(c);
  }

  const PRESETS = [
    { hex: '#2a6b1a', label: 'Marsh' },
    { hex: '#4a8c3a', label: 'Innsm.' },
    { hex: '#c87820', label: 'Amber' },
    { hex: '#8b1a1a', label: 'Blood' },
    { hex: '#1a3a6a', label: 'Deep' },
    { hex: '#6a1a6a', label: 'Void' },
    { hex: '#c8b870', label: 'Bone' },
    { hex: '#d8d0c0', label: 'Pale' },
  ];

  const swatchContainer = document.getElementById('swatches');
  PRESETS.forEach((p, i) => {
    const s = document.createElement('div');
    s.className = 'swatch';
    s.style.background = p.hex;
    s.dataset.color = p.hex;
    s.dataset.num = p.label;
    s.title = p.label;
    s.addEventListener('click', () => setColor(p.hex, s));
    swatchContainer.appendChild(s);
  });

  let state = {
    power: true,
    color: '#4a8c3a',
    brightness: 180,
    effect: 'solid',
    gasTargetSeconds: 777,
    gasTotalCycles: 7,
    gasMinBrightness: 190,
    gasMaxBrightness: 255,
    gasDeepBrightness: 15,
    gasNormalDelayMin: 30,
    gasNormalDelayMax: 120,
    gasDeepDelayMin: 100,
    gasDeepDelayMax: 400,
    gasCycle: 1,
    gasElapsed: 0,
    gasRunning: true
  };

  let debounceTimer;

  const colorPicker  = document.getElementById('colorPicker');
  const hexDisplay   = document.getElementById('hexDisplay');

  colorPicker.addEventListener('input', e => setColor(e.target.value, null));

  function setColor(hex, swatchEl) {
    state.color = hex;
    colorPicker.value = hex;
    hexDisplay.textContent = '[ ' + hex.replace('#','').toUpperCase() + ' ]';
    updateRingColor(hex);
    document.querySelectorAll('.swatch').forEach(s => s.classList.remove('active'));
    if (swatchEl) swatchEl.classList.add('active');
    document.querySelector('.ring-svg').style.filter = `drop-shadow(0 0 12px ${hex}99)`;
    sendDebounced();
  }

  function updateRingColor(hex) {
    circles.forEach((c, i) => {
      if (!state.power || state.effect === 'off_effect') {
        c.setAttribute('fill','#2a1a08');
        return;
      }
      if (state.effect === 'rainbow') {
        c.setAttribute('fill', `hsl(${(i/NUM_LEDS)*360},80%,42%)`);
      } else {
        c.setAttribute('fill', hex);
      }
    });
  }

  const brightnessSlider = document.getElementById('brightness');
  const brightnessVal    = document.getElementById('brightnessVal');
  brightnessSlider.addEventListener('input', e => {
    state.brightness = parseInt(e.target.value, 10);
    brightnessVal.textContent = state.brightness;
    ringDots.style.opacity = 0.12 + (state.brightness / 255) * 0.88;
    sendDebounced();
  });

  const powerToggle = document.getElementById('powerToggle');
  const powerLabel  = document.getElementById('powerLabel');
  powerToggle.addEventListener('change', e => {
    state.power = e.target.checked;
    powerLabel.textContent = state.power ? 'ACTIVE' : 'DORMANT';
    powerLabel.className = 'pstate' + (state.power ? '' : ' off');
    updateRingColor(state.color);
    document.querySelector('.ring-svg').style.filter = state.power
      ? `drop-shadow(0 0 12px ${state.color}99)`
      : 'drop-shadow(0 0 2px rgba(26,18,8,0.4))';
    sendCommand('/power', { state: state.power ? 1 : 0 });
  });

  document.querySelectorAll('.effect-btn[data-effect]').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.effect-btn[data-effect]').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      state.effect = btn.dataset.effect;
      updateRingColor(state.color);
      sendCommand('/effect', { effect: state.effect });
    });
  });

  function sendDebounced() {
    clearTimeout(debounceTimer);
    debounceTimer = setTimeout(sendState, 120);
  }

  async function sendCommand(endpoint, params) {
    const url = new URL(endpoint, window.location.origin);
    Object.entries(params).forEach(([k,v]) => url.searchParams.set(k,v));
    try {
      await fetch(url);
      setStatus(true);
    } catch {
      setStatus(false);
    }
  }

  async function sendState() {
    const hex = state.color.replace('#','');
    sendCommand('/color', {
      r: parseInt(hex.slice(0,2),16),
      g: parseInt(hex.slice(2,4),16),
      b: parseInt(hex.slice(4,6),16),
      brightness: state.brightness
    });
  }

  async function setParam(name, value) {
    state[name] = parseInt(value, 10);
    await sendCommand('/setParam', { name, value: state[name] });
  }

  async function gasControl(action) {
    await sendCommand('/gasControl', { action });
  }

  const statusLight = document.getElementById('statusLight');
  const statusText  = document.getElementById('statusText');
  function setStatus(online) {
    statusLight.className = 'status-light' + (online ? ' online' : '');
    statusText.textContent = online
      ? 'FIELD UNIT RESPONDING — ESP-32'
      : 'SIGNAL LOST — CHECK ÆTHER LINK';
  }

  async function ping() {
    try {
      const r = await fetch('/ping', { signal: AbortSignal.timeout(2000) });
      setStatus(r.ok);
    } catch {
      setStatus(false);
    }
  }

  async function pollStatus() {
    try {
      const r = await fetch('/getStatus', { signal: AbortSignal.timeout(2000) });
      const data = await r.json();

      state.power = data.power;
      state.effect = data.effect;
      state.brightness = data.brightness;
      state.color = data.color;
      state.gasCycle = data.cycle;
      state.gasElapsed = data.elapsed;
      state.gasRunning = data.running;

      state.gasTargetSeconds = data.targetSeconds;
      state.gasTotalCycles = data.totalCycles;
      state.gasMinBrightness = data.minBrightness;
      state.gasMaxBrightness = data.maxBrightness;
      state.gasDeepBrightness = data.deepBrightness;
      state.gasNormalDelayMin = data.normalDelayMin;
      state.gasNormalDelayMax = data.normalDelayMax;
      state.gasDeepDelayMin = data.deepDelayMin;
      state.gasDeepDelayMax = data.deepDelayMax;

      powerToggle.checked = state.power;
      powerLabel.textContent = state.power ? 'ACTIVE' : 'DORMANT';
      powerLabel.className = 'pstate' + (state.power ? '' : ' off');

      colorPicker.value = state.color;
      hexDisplay.textContent = '[ ' + state.color.replace('#','').toUpperCase() + ' ]';
      brightnessSlider.value = state.brightness;
      brightnessVal.textContent = state.brightness;
      ringDots.style.opacity = 0.12 + (state.brightness / 255) * 0.88;

      document.getElementById('gasTargetSeconds').value = state.gasTargetSeconds;
      document.getElementById('gasTotalCycles').value = state.gasTotalCycles;
      document.getElementById('gasMinBrightness').value = state.gasMinBrightness;
      document.getElementById('gasMaxBrightness').value = state.gasMaxBrightness;
      document.getElementById('gasDeepBrightness').value = state.gasDeepBrightness;
      document.getElementById('gasNormalDelayMin').value = state.gasNormalDelayMin;
      document.getElementById('gasNormalDelayMax').value = state.gasNormalDelayMax;
      document.getElementById('gasDeepDelayMin').value = state.gasDeepDelayMin;
      document.getElementById('gasDeepDelayMax').value = state.gasDeepDelayMax;

      document.getElementById('gasCycleReadout').textContent =
        `${state.gasCycle} / ${state.gasTotalCycles}`;
      document.getElementById('gasElapsedReadout').textContent =
        `${state.gasElapsed} / ${state.gasTargetSeconds}s`;
      document.getElementById('gasStateReadout').textContent =
        state.gasRunning ? 'ACTIVE' : 'PAUSED';

      document.querySelectorAll('.effect-btn[data-effect]').forEach(b => {
        b.classList.toggle('active', b.dataset.effect === state.effect);
      });

      document.querySelectorAll('.swatch').forEach(s => {
        s.classList.toggle('active', s.dataset.color.toLowerCase() === state.color.toLowerCase());
      });

      updateRingColor(state.color);
      document.querySelector('.ring-svg').style.filter = state.power
        ? `drop-shadow(0 0 12px ${state.color}99)`
        : 'drop-shadow(0 0 2px rgba(26,18,8,0.4))';

      setStatus(true);
    } catch {
      setStatus(false);
    }
  }

  [
    'gasTargetSeconds',
    'gasTotalCycles',
    'gasMinBrightness',
    'gasMaxBrightness',
    'gasDeepBrightness',
    'gasNormalDelayMin',
    'gasNormalDelayMax',
    'gasDeepDelayMin',
    'gasDeepDelayMax'
  ].forEach(id => {
    document.getElementById(id).addEventListener('change', e => {
      setParam(id, e.target.value);
    });
  });

  document.getElementById('gasStartBtn').addEventListener('click', () => gasControl('start'));
  document.getElementById('gasPauseBtn').addEventListener('click', () => gasControl('pause'));
  document.getElementById('gasResetBtn').addEventListener('click', () => gasControl('reset'));

  setColor('#4a8c3a', swatchContainer.children[1]);
  setInterval(ping, 5000);
  setInterval(pollStatus, 1000);
  ping();
  pollStatus();
</script>
</body>
</html>
)===";

// ── Utilities ─────────────────────────────────────────────
String colorToHex(const CRGB& c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
  return String(buf);
}

int safeRandomRange(int minVal, int maxVal) {
  if (maxVal < minVal) {
    int t = minVal;
    minVal = maxVal;
    maxVal = t;
  }
  if (minVal == maxVal) return minVal;
  return random(minVal, maxVal + 1);
}

void updateGaslightCycle() {
  if (!gasCycleRunning) return;

  unsigned long now = millis();
  unsigned long elapsedMs = now - gasCycleStart;

  if (elapsedMs >= (unsigned long)gasTargetSeconds * 1000UL) {
    gasCycleCounter++;
    if (gasCycleCounter > gasTotalCycles) gasCycleCounter = 1;
    gasCycleStart = now;
    Serial.printf("[GAS] Cycle advanced -> %d / %d\n", gasCycleCounter, gasTotalCycles);
  }
}

void runGaslightEffect() {
  updateGaslightCycle();

  unsigned long now = millis();
  if (now < gasNextFlickerAt) return;

  fill_solid(leds, NUM_LEDS, currentColor);

  if (gasCycleCounter == gasTotalCycles) {
    FastLED.setBrightness(gasDeepBrightness);
    FastLED.show();
    gasNextFlickerAt = now + safeRandomRange(gasDeepDelayMin, gasDeepDelayMax);
  } else {
    FastLED.setBrightness(safeRandomRange(gasMinBrightness, gasMaxBrightness));
    FastLED.show();
    gasNextFlickerAt = now + safeRandomRange(gasNormalDelayMin, gasNormalDelayMax);
  }
}

// ── Effects ───────────────────────────────────────────────
void applyEffect() {
  if (!ledPower) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    return;
  }

  unsigned long now = millis();

  if (currentEffect == "solid") {
    fill_solid(leds, NUM_LEDS, currentColor);
    FastLED.setBrightness(brightness);
    FastLED.show();

  } else if (currentEffect == "pulse") {
    if (now - lastUpdate > 16) {
      lastUpdate = now;
      static float phase = 0.0f;
      phase += 0.05f;
      uint8_t bright = (uint8_t)(127.5f + 127.5f * sinf(phase));
      fill_solid(leds, NUM_LEDS, currentColor);
      FastLED.setBrightness((uint8_t)map(bright, 0, 255, 10, brightness));
      FastLED.show();
    }

  } else if (currentEffect == "rainbow") {
    if (now - lastUpdate > 20) {
      lastUpdate = now;
      rainbowHue += 2;
      fill_rainbow(leds, NUM_LEDS, rainbowHue, 256 / NUM_LEDS);
      FastLED.setBrightness(brightness);
      FastLED.show();
    }

  } else if (currentEffect == "chase") {
    if (now - lastUpdate > 60) {
      lastUpdate = now;
      fill_solid(leds, NUM_LEDS, CRGB::Black);

      leds[chasePos % NUM_LEDS] = currentColor;

      leds[(chasePos + 1) % NUM_LEDS] = currentColor;
      leds[(chasePos + 1) % NUM_LEDS].nscale8(140);

      leds[(chasePos + 2) % NUM_LEDS] = currentColor;
      leds[(chasePos + 2) % NUM_LEDS].nscale8(60);

      chasePos = (chasePos + 1) % NUM_LEDS;
      FastLED.setBrightness(brightness);
      FastLED.show();
    }

  } else if (currentEffect == "sparkle") {
    if (now - lastUpdate > 40) {
      lastUpdate = now;
      for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(180);
      leds[random(NUM_LEDS)] = currentColor;
      FastLED.setBrightness(brightness);
      FastLED.show();
    }

  } else if (currentEffect == "gaslight") {
    runGaslightEffect();

  } else if (currentEffect == "off_effect") {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
  }
}

// ── Handlers ──────────────────────────────────────────────
void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handlePing() {
  server.send(200, "text/plain", "OK");
}

void handlePower() {
  if (server.hasArg("state")) {
    ledPower = server.arg("state").toInt() == 1;
    if (ledPower) {
      FastLED.setBrightness(brightness);
      fill_solid(leds, NUM_LEDS, currentColor);
    } else {
      fill_solid(leds, NUM_LEDS, CRGB::Black);
    }
    FastLED.show();
  }
  server.send(200, "text/plain", ledPower ? "ON" : "OFF");
}

void handleColor() {
  if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    uint8_t r = (uint8_t)server.arg("r").toInt();
    uint8_t g = (uint8_t)server.arg("g").toInt();
    uint8_t b = (uint8_t)server.arg("b").toInt();
    currentColor = CRGB(r, g, b);
  }

  if (server.hasArg("brightness")) {
    int newBright = server.arg("brightness").toInt();
    if (newBright > 0) {
      brightness = (uint8_t)constrain(newBright, 0, 255);
      FastLED.setBrightness(brightness);
    }
  }

  if (ledPower && currentEffect != "gaslight" && currentEffect != "off_effect") {
    fill_solid(leds, NUM_LEDS, currentColor);
    FastLED.show();
  }

  server.send(200, "text/plain", "OK");
}

void handleEffect() {
  if (server.hasArg("effect")) {
    currentEffect = server.arg("effect");
    lastUpdate = 0;
    gasNextFlickerAt = 0;

    if (currentEffect == "solid") {
      fill_solid(leds, NUM_LEDS, currentColor);
      FastLED.setBrightness(brightness);
      FastLED.show();
    } else if (currentEffect == "off_effect") {
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
    } else if (currentEffect == "gaslight") {
      gasCycleStart = millis();
      gasNextFlickerAt = 0;
    }
  }

  server.send(200, "text/plain", currentEffect);
}

void handleSetParam() {
  if (!server.hasArg("name") || !server.hasArg("value")) {
    server.send(400, "text/plain", "Missing name/value");
    return;
  }

  String name = server.arg("name");
  int value = server.arg("value").toInt();

  if      (name == "gasTargetSeconds")  gasTargetSeconds  = max(1, value);
  else if (name == "gasTotalCycles")    gasTotalCycles    = max(1, value);
  else if (name == "gasMinBrightness")  gasMinBrightness  = constrain(value, 0, 255);
  else if (name == "gasMaxBrightness")  gasMaxBrightness  = constrain(value, 0, 255);
  else if (name == "gasDeepBrightness") gasDeepBrightness = constrain(value, 0, 255);
  else if (name == "gasNormalDelayMin") gasNormalDelayMin = max(1, value);
  else if (name == "gasNormalDelayMax") gasNormalDelayMax = max(1, value);
  else if (name == "gasDeepDelayMin")   gasDeepDelayMin   = max(1, value);
  else if (name == "gasDeepDelayMax")   gasDeepDelayMax   = max(1, value);
  else {
    server.send(400, "text/plain", "Unknown param");
    return;
  }

  server.send(200, "text/plain", "OK");
}

void handleGasControl() {
  if (!server.hasArg("action")) {
    server.send(400, "text/plain", "Missing action");
    return;
  }

  String action = server.arg("action");

  if (action == "start") {
    gasCycleRunning = true;
    gasCycleStart = millis();
    gasNextFlickerAt = 0;
  } else if (action == "pause") {
    gasCycleRunning = false;
  } else if (action == "reset") {
    gasCycleCounter = 1;
    gasCycleRunning = true;
    gasCycleStart = millis();
    gasNextFlickerAt = 0;
  }

  server.send(200, "text/plain", action);
}

void handleGetStatus() {
  unsigned long elapsed = (millis() - gasCycleStart) / 1000UL;

  String json = "{";
  json += "\"power\":" + String(ledPower ? "true" : "false") + ",";
  json += "\"effect\":\"" + currentEffect + "\",";
  json += "\"brightness\":" + String(brightness) + ",";
  json += "\"color\":\"" + colorToHex(currentColor) + "\",";
  json += "\"cycle\":" + String(gasCycleCounter) + ",";
  json += "\"totalCycles\":" + String(gasTotalCycles) + ",";
  json += "\"elapsed\":" + String(elapsed) + ",";
  json += "\"targetSeconds\":" + String(gasTargetSeconds) + ",";
  json += "\"running\":" + String(gasCycleRunning ? "true" : "false") + ",";
  json += "\"minBrightness\":" + String(gasMinBrightness) + ",";
  json += "\"maxBrightness\":" + String(gasMaxBrightness) + ",";
  json += "\"deepBrightness\":" + String(gasDeepBrightness) + ",";
  json += "\"normalDelayMin\":" + String(gasNormalDelayMin) + ",";
  json += "\"normalDelayMax\":" + String(gasNormalDelayMax) + ",";
  json += "\"deepDelayMin\":" + String(gasDeepDelayMin) + ",";
  json += "\"deepDelayMax\":" + String(gasDeepDelayMax);
  json += "}";

  server.send(200, "application/json", json);
}

// ── Setup / Loop ──────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);

  randomSeed(micros());

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(brightness);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to %s", WIFI_SSID);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi failed. Restarting in 5 s...");
    delay(5000);
    ESP.restart();
  }

  Serial.println();
  Serial.print("Connected! Open: http://");
  Serial.println(WiFi.localIP());

  server.on("/",          handleRoot);
  server.on("/ping",      handlePing);
  server.on("/power",     handlePower);
  server.on("/color",     handleColor);
  server.on("/effect",    handleEffect);
  server.on("/setParam",  handleSetParam);
  server.on("/gasControl", handleGasControl);
  server.on("/getStatus", handleGetStatus);

  server.begin();
  Serial.println("Web server started.");

  gasCycleStart = millis();

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(0, 200, 80);
    FastLED.show();
    delay(30);
  }
  delay(300);
  fill_solid(leds, NUM_LEDS, currentColor);
  FastLED.setBrightness(brightness);
  FastLED.show();
}

void loop() {
  server.handleClient();
  applyEffect();
}