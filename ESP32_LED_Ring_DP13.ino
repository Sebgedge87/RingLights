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
#include <Preferences.h>
#include <driver/i2s_std.h>

// ── Configuration ─────────────────────────────────────────
#define LED_PIN     13
#define NUM_LEDS    24
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

#include "credentials.h"

// ── INMP441 I2S Microphone Pins ───────────────────────────
#define I2S_SCK_PIN   14
#define I2S_WS_PIN    15
#define I2S_SD_PIN    32

// ── Phrase Config ─────────────────────────────────────────
#define MAX_PHRASES    5
#define CONFIG_VERSION 2   // Increment when saved schema changes
// ──────────────────────────────────────────────────────────

CRGB leds[NUM_LEDS];
WebServer server(80);

bool    ledPower      = true;
CRGB    currentColor  = CRGB(74, 140, 58);   // #4a8c3a
uint8_t brightness    = 180;
String  currentEffect = "solid";

unsigned long lastUpdate = 0;
int chasePos      = 0;
int chaseRevStep  = 0;   // steps taken in the current revolution
int chaseLitCount = 1;   // spokes currently lit during build-up
int chaseSpokes   = 8;   // target number of evenly-spaced spokes
int chaseSpeed    = 60;  // ms per step
int rainbowHue    = 0;

int  loaderSpeed      = 80;   // ms per LED step during build-up
int  loaderFlashMs    = 200;  // ms per flash toggle (phase 1)
int  loaderFadeSec    = 21;   // seconds for green fade-in (phase 2)
int  loaderBase       = 1;    // LEDs permanently lit at start of each revolution
int  loaderSweep      = 1;    // current sweep position (index up to NUM_LEDS-1)
int  loaderPhase      = 0;    // 0=buildup  1=flash  2=fadein
int  loaderFlashCount = 0;    // number of on→off cycles completed
bool loaderFlashOn    = false;
unsigned long loaderFadeStart = 0;

// ── Trigger State ─────────────────────────────────────────
CRGB          triggerReturnColor = CRGB(74, 140, 58); // color to restore after trigger expiry

// ── Gaslight Parameters ───────────────────────────────────
int gasMaxBrightness     = 255;
int gasMinBrightness     = 190;
int gasDeepBrightnessMin = 5;
int gasDeepBrightnessMax = 25;

int gasTargetSeconds     = 777;
int gasTotalCycles       = 7;
int gasCycleCounter      = 1;
int gasDescentCycles     = 2;   // final N cycles fade down gradually
int gasVariation         = 15;  // per-LED brightness variation 0–50
int gasWarnProbability   = 10;  // % chance per 100 ms of a pre-descent dip

bool gasCycleRunning    = true;
unsigned long gasCycleStart = 0;

// ── Gaslight Wave State ───────────────────────────────────
float         gasWavePhase          = 0.0f;
unsigned long gasLastWaveMs         = 0;
bool          gasTransientActive    = false;
unsigned long gasTransientStart     = 0;
int           gasTransientDepth     = 0;
unsigned long gasLastTransientCheck = 0;
// ──────────────────────────────────────────────────────────

// ── Phrase Trigger Config ────────────────────────────────
struct PhraseConfig {
  String  phrase;
  String  effect;        // "solid","gaslight","chase","rainbow","loader","off_effect"
  uint8_t r, g, b;
  int     durationSec;   // 0 = stay until manually changed
  String  returnEffect;  // effect to restore after durationSec
};

String       ringName     = "Ring 1";
bool         micEnabled   = false;
bool         micInitDone  = false;
PhraseConfig phraseConfigs[MAX_PHRASES];
int          phraseCount  = 0;

bool          triggerActive   = false;
unsigned long triggerStartMs  = 0;
int           triggerDuration = 0;
String        triggerReturn   = "gaslight";

// ── Mic Sample Buffer ────────────────────────────────────
#define MIC_BUFFER_SIZE 16000          // 1 s at 16 kHz
static int16_t micBuffer[MIC_BUFFER_SIZE];
static int     micBufferPos = 0;

static i2s_chan_handle_t rx_chan = NULL;
// ──────────────────────────────────────────────────────────

// ── Config Page ───────────────────────────────────────────
const char CONFIG_PAGE[] PROGMEM = R"===(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Ring Config</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=Courier+Prime:wght@400;700&display=swap');
:root{--void:#080508;--parchment:#c8b89a;--green-lit:#7aba5a;--green-dim:#4a7a3a;}
*{box-sizing:border-box;margin:0;padding:0;}
body{background:var(--void);color:var(--parchment);font-family:'Courier Prime',monospace;min-height:100vh;padding:24px 16px;max-width:740px;margin:0 auto;}
a{color:var(--green-lit);text-decoration:none;}a:hover{text-decoration:underline;}
h1{font-size:1rem;letter-spacing:.22em;text-transform:uppercase;color:var(--green-lit);margin-bottom:22px;}
h2{font-size:.68rem;letter-spacing:.2em;text-transform:uppercase;color:rgba(200,184,154,.45);margin-bottom:10px;}
section{border:1px solid rgba(74,122,58,.28);padding:14px 16px;margin-bottom:14px;border-radius:4px;}
label{display:flex;flex-direction:column;gap:3px;font-size:.78rem;margin-bottom:8px;}
input[type=text],input[type=number]{background:rgba(255,255,255,.05);border:1px solid rgba(200,184,154,.22);color:var(--parchment);padding:5px 8px;font-family:inherit;font-size:.8rem;border-radius:3px;}
input[type=text]{width:100%;}input[type=number]{width:64px;}
select{background:#110e0d;border:1px solid rgba(200,184,154,.22);color:var(--parchment);padding:5px 8px;font-family:inherit;font-size:.8rem;border-radius:3px;}
input[type=color]{width:34px;height:26px;padding:1px;border:1px solid rgba(200,184,154,.22);background:transparent;border-radius:3px;cursor:pointer;vertical-align:middle;}
.toggle-row{display:flex;flex-direction:row;align-items:center;gap:8px;font-size:.8rem;margin-bottom:4px;}
input[type=checkbox]{width:15px;height:15px;accent-color:var(--green-lit);cursor:pointer;}
.note{font-size:.68rem;color:rgba(200,184,154,.42);margin-top:5px;line-height:1.5;}
.phrase-row{padding:10px 0;border-bottom:1px solid rgba(200,184,154,.08);display:flex;gap:10px;align-items:flex-start;}
.phrase-row:last-child{border-bottom:none;}
.row-num{font-size:.7rem;color:rgba(200,184,154,.35);min-width:16px;padding-top:7px;}
.phrase-inner{flex:1;display:flex;flex-direction:column;gap:6px;}
.ctrl-row{display:flex;flex-wrap:wrap;gap:6px;align-items:center;font-size:.72rem;color:rgba(200,184,154,.5);}
.rgb-row{display:flex;gap:6px;align-items:center;font-size:.72rem;color:rgba(200,184,154,.5);}
.rgb-row label{flex-direction:row;align-items:center;gap:3px;margin:0;}
button[type=submit]{background:rgba(74,122,58,.28);border:1px solid var(--green-dim);color:var(--green-lit);padding:9px 22px;font-family:inherit;font-size:.85rem;letter-spacing:.12em;text-transform:uppercase;cursor:pointer;border-radius:3px;margin-top:6px;}
button[type=submit]:hover{background:rgba(74,122,58,.48);}
#status{margin-top:10px;font-size:.82rem;min-height:1.2em;}
.back{font-size:.72rem;letter-spacing:.1em;display:inline-block;margin-bottom:18px;}
</style>
</head>
<body>
<a class="back" href="/">&#8592; Control Panel</a>
<h1>Ring Configuration</h1>
<form id="configForm">
  <section>
    <h2>Identity</h2>
    <label>Ring Name
      <input type="text" id="ringName" name="ringName" maxlength="32" placeholder="Ring 1">
    </label>
  </section>
  <section>
    <h2>Microphone</h2>
    <div class="toggle-row">
      <input type="checkbox" id="micEnabled">
      <span>Enable INMP441 I2S Microphone</span>
    </div>
    <p class="note">Wiring: SCK&#8594;GPIO14 &nbsp; WS&#8594;GPIO15 &nbsp; SD&#8594;GPIO32 &nbsp; L/R&#8594;GND</p>
    <p class="note">Phrase detection requires an Edge Impulse model &#8212; train at edgeimpulse.com and export as Arduino library.</p>
  </section>
  <section>
    <h2>Phrase Triggers</h2>
    <p class="note" style="margin-bottom:12px;">When a phrase is detected the ring switches to the chosen effect and colour. Leave blank to disable a slot.</p>
    <div id="phraseList"></div>
  </section>
  <button type="submit">Save Configuration</button>
</form>
<div id="status"></div>
<script>
function rgbToHex(r,g,b){return'#'+[r,g,b].map(v=>v.toString(16).padStart(2,'0')).join('');}
function hexToRgb(h){return{r:parseInt(h.slice(1,3),16),g:parseInt(h.slice(3,5),16),b:parseInt(h.slice(5,7),16)};}
function syncRGB(i){const{r,g,b}=hexToRgb(document.getElementById('col'+i).value);document.querySelector('[name=r'+i+']').value=r;document.querySelector('[name=g'+i+']').value=g;document.querySelector('[name=b'+i+']').value=b;}
function syncPicker(i){document.getElementById('col'+i).value=rgbToHex(+document.querySelector('[name=r'+i+']').value,+document.querySelector('[name=g'+i+']').value,+document.querySelector('[name=b'+i+']').value);}
function eff(list,sel){return list.map(e=>'<option value="'+e+'"'+(e===sel?' selected':'')+'>'+e+'</option>').join('');}
function buildRow(i,p){
  const effs=['solid','gaslight','chase','rainbow','loader','off_effect'];
  const hex=rgbToHex(p.r||255,p.g||255,p.b||255);
  return'<div class="phrase-row"><span class="row-num">'+( i+1)+'</span><div class="phrase-inner">'+
    '<input type="text" name="phrase'+i+'" placeholder="e.g. The Abbey" value="'+(p.phrase||'')+'">'+
    '<div class="ctrl-row">Effect:<select name="effect'+i+'">'+eff(effs,p.effect||'solid')+'</select>'+
    'Duration:<input type="number" name="dur'+i+'" min="0" max="3600" value="'+(p.duration||10)+'">s'+
    'then:<select name="ret'+i+'">'+eff(effs,p.returnEffect||'gaslight')+'</select></div>'+
    '<div class="rgb-row"><input type="color" id="col'+i+'" value="'+hex+'" oninput="syncRGB('+i+')">'+
    '<label>R<input type="number" name="r'+i+'" min="0" max="255" value="'+(p.r||255)+'" oninput="syncPicker('+i+')"></label>'+
    '<label>G<input type="number" name="g'+i+'" min="0" max="255" value="'+(p.g||255)+'" oninput="syncPicker('+i+')"></label>'+
    '<label>B<input type="number" name="b'+i+'" min="0" max="255" value="'+(p.b||255)+'" oninput="syncPicker('+i+')"></label></div>'+
    '</div></div>';}
async function loadConfig(){
  try{
    const cfg=await(await fetch('/getConfig')).json();
    document.getElementById('ringName').value=cfg.ringName||'';
    document.getElementById('micEnabled').checked=!!cfg.micEnabled;
    const list=document.getElementById('phraseList');list.innerHTML='';
    for(let i=0;i<5;i++){list.innerHTML+=buildRow(i,(cfg.phrases&&cfg.phrases[i])||{});}
  }catch(e){console.error(e);}
}
document.getElementById('configForm').addEventListener('submit',async function(e){
  e.preventDefault();
  const fd=new FormData(this);fd.set('micEnabled',document.getElementById('micEnabled').checked?'1':'0');
  const res=await fetch('/saveConfig',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(fd).toString()});
  const st=document.getElementById('status');
  st.style.color=res.ok?'#7aba5a':'#c04040';st.textContent=res.ok?'Configuration saved.':'Save failed.';
  setTimeout(()=>st.textContent='',4000);
});
loadConfig();
</script>
</body>
</html>
)===";

// ── Player / Observer Page ────────────────────────────────
const char PLAYER_PAGE[] PROGMEM = R"===(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<title>OSS — Observer Port</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=Courier+Prime:ital,wght@0,400;0,700;1,400&display=swap');
:root { --void:#080508; --parchment:#c8b89a; --green-glow:#4a7a3a; --green-lit:#7aba5a; --blood:#6b1010; --ink-faded:#3a2e1a; }
*{box-sizing:border-box;margin:0;padding:0;}
body{
  background:var(--void);
  min-height:100vh;
  display:flex;flex-direction:column;
  align-items:center;justify-content:center;
  font-family:'Courier Prime',monospace;
  gap:20px;
  padding:20px;
}
body::before{
  content:'';position:fixed;inset:0;
  background:radial-gradient(ellipse at 50% 50%,rgba(26,42,10,0.4) 0%,transparent 65%);
  pointer-events:none;z-index:0;
}
.observer-frame{position:relative;z-index:1;display:flex;flex-direction:column;align-items:center;gap:18px;}
.header-txt{font-size:.5rem;letter-spacing:.2em;color:rgba(200,184,154,.28);text-transform:uppercase;text-align:center;}
.ring-wrap{position:relative;display:inline-flex;align-items:center;justify-content:center;}
.ring-wrap::before{
  content:'SIGNAL APERTURE';
  position:absolute;top:-18px;left:50%;transform:translateX(-50%);
  font-size:.46rem;letter-spacing:.22em;color:rgba(200,184,154,.3);white-space:nowrap;
}
.ring-outer-obs{
  position:absolute;inset:-10px;
  border:1px dashed rgba(200,184,154,.15);border-radius:50%;
}
#ringSvgP{width:200px;height:200px;transition:filter .5s;}
.effect-label{
  font-size:.6rem;letter-spacing:.2em;
  color:rgba(200,184,154,.65);text-transform:uppercase;text-align:center;
}
.status-row{display:flex;align-items:center;gap:8px;}
.status-dot{
  width:7px;height:7px;border-radius:50%;
  background:var(--blood);box-shadow:0 0 4px var(--blood);
  transition:all .4s;
}
.status-dot.online{
  background:var(--green-lit);box-shadow:0 0 8px var(--green-glow);
  animation:blink 3s ease-in-out infinite;
}
@keyframes blink{0%,90%,100%{opacity:1}95%{opacity:.3}}
.status-txt{font-size:.5rem;letter-spacing:.14em;color:rgba(200,184,154,.35);text-transform:uppercase;}
.phase-badge{
  font-size:.5rem;letter-spacing:.16em;
  color:rgba(200,184,154,.4);text-transform:uppercase;
  border:1px solid rgba(200,184,154,.12);
  padding:3px 10px;
}
</style>
</head>
<body>
<div class="observer-frame">
  <div class="header-txt">OSS Occult Div. &mdash; Observer Port</div>
  <div class="ring-wrap">
    <div class="ring-outer-obs"></div>
    <svg id="ringSvgP" viewBox="0 0 160 160">
      <defs>
        <filter id="ledGlowP">
          <feGaussianBlur stdDeviation="2.5" result="b"/>
          <feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge>
        </filter>
      </defs>
      <g id="ticksP" stroke="rgba(200,184,154,0.1)" stroke-width="1"></g>
      <g id="ringDotsP" filter="url(#ledGlowP)"></g>
    </svg>
  </div>
  <div class="effect-label" id="effectLabelP">&mdash;</div>
  <div class="phase-badge" id="phaseBadgeP">STANDBY</div>
  <div class="status-row">
    <div class="status-dot" id="statusDotP"></div>
    <div class="status-txt" id="statusTxtP">AWAITING FIELD UNIT...</div>
  </div>
</div>
<script>
const NUM_LEDS=24,CX=80,CY=80,R_LED=62,DOT_R=6.5;
const tickG=document.getElementById('ticksP');
for(let i=0;i<48;i++){
  const a=(i/48)*2*Math.PI-Math.PI/2,r1=74,r2=(i%3===0)?69:72;
  const l=document.createElementNS('http://www.w3.org/2000/svg','line');
  l.setAttribute('x1',(CX+r1*Math.cos(a)).toFixed(2));l.setAttribute('y1',(CY+r1*Math.sin(a)).toFixed(2));
  l.setAttribute('x2',(CX+r2*Math.cos(a)).toFixed(2));l.setAttribute('y2',(CY+r2*Math.sin(a)).toFixed(2));
  tickG.appendChild(l);
}
const ringDotsP=document.getElementById('ringDotsP');
const circles=[];
for(let i=0;i<NUM_LEDS;i++){
  const angle=(i/NUM_LEDS)*2*Math.PI-Math.PI/2;
  const c=document.createElementNS('http://www.w3.org/2000/svg','circle');
  c.setAttribute('cx',(CX+R_LED*Math.cos(angle)).toFixed(2));
  c.setAttribute('cy',(CY+R_LED*Math.sin(angle)).toFixed(2));
  c.setAttribute('r',DOT_R);c.setAttribute('fill','#4a8c3a');
  circles.push(c);ringDotsP.appendChild(c);
}
const EFFECT_NAMES={solid:'Steady Glow',pulse:'Heartbeat',rainbow:'Prismatic Rift',
  chase:'Elder Sign Chase',sparkle:'Spectral Flicker',gaslight:'Gaslight Vigil',loader:'Summoning Seal',off_effect:'Extinguished'};
let state={power:true,color:'#4a8c3a',brightness:180,effect:'solid',
  chaseSpokes:8,chaseSpeed:60,loaderSpeed:80,loaderFlashMs:200,loaderFadeSec:21,
  gasCycle:1,gasTotalCycles:7,gasMinBrightness:190,gasMaxBrightness:255,
  gasDeepBrightnessMin:5,gasDeepBrightnessMax:25,gasDescentCycles:2,
  gasVariation:15,gasWarnProbability:10,descentStart:6};
let animLastTs=0;
// ── Per-effect mutable state (player page) ────────────────
const ES={
  pulse:   {phase:0},
  rainbow: {hue:0},
  chase:   {pos:0,lastTs:0,revStep:0,litCount:1},
  sparkle: {lastTs:0,levels:new Array(NUM_LEDS).fill(0)},
  loader:  {base:1,sweep:1,lastTs:0,phase:0,flashCount:0,flashOn:false,fadeStart:0},
  gaslight:{phase:0,txActive:false,txStart:0,txDepth:0,txCheck:0},
};
const ES_RESET={
  pulse:   e=>{e.phase=0;},
  rainbow: e=>{e.hue=0;},
  chase:   e=>{e.pos=0;e.lastTs=0;e.revStep=0;e.litCount=1;},
  sparkle: e=>{e.lastTs=0;e.levels.fill(0);},
  loader:  e=>{e.base=1;e.sweep=1;e.lastTs=0;e.phase=0;e.flashCount=0;e.flashOn=false;e.fadeStart=0;},
  gaslight:e=>{e.phase=0;e.txActive=false;},
};
const EFFECT_RENDERERS={
  solid:(ts,dt,cr,cg,cb,st)=>{
    circles.forEach(c=>c.setAttribute('fill',st.color));
  },
  pulse:(ts,dt,cr,cg,cb,st)=>{
    const e=ES.pulse;e.phase+=0.05;
    const s=0.04+Math.max(0,0.5+0.5*Math.sin(e.phase))*0.96;
    circles.forEach(c=>c.setAttribute('fill',`rgb(${Math.round(cr*s)},${Math.round(cg*s)},${Math.round(cb*s)})`));
  },
  rainbow:(ts,dt,cr,cg,cb,st)=>{
    const e=ES.rainbow;e.hue=(e.hue+2)%360;
    circles.forEach((c,i)=>c.setAttribute('fill',`hsl(${(e.hue+i*(360/NUM_LEDS))%360},80%,42%)`));
  },
  chase:(ts,dt,cr,cg,cb,st)=>{
    const e=ES.chase,spd=st.chaseSpeed||60,tgt=st.chaseSpokes||8;
    if(ts-e.lastTs>spd){e.lastTs=ts;e.pos=(e.pos+1)%NUM_LEDS;if(e.litCount<NUM_LEDS){e.revStep++;if(e.revStep>=NUM_LEDS){e.revStep=0;e.litCount++;}}}
    circles.forEach(c=>c.setAttribute('fill','#2a1a08'));
    const n=e.litCount<NUM_LEDS?e.litCount:tgt;
    for(let s=0;s<n;s++)circles[(e.pos+Math.round(s*NUM_LEDS/n))%NUM_LEDS].setAttribute('fill',`rgb(${cr},${cg},${cb})`);
  },
  sparkle:(ts,dt,cr,cg,cb,st)=>{
    const e=ES.sparkle;
    if(ts-e.lastTs>40){e.lastTs=ts;e.levels=e.levels.map(v=>v*(180/255));e.levels[Math.floor(Math.random()*NUM_LEDS)]=1;}
    circles.forEach((c,i)=>c.setAttribute('fill',`rgb(${Math.round(cr*e.levels[i])},${Math.round(cg*e.levels[i])},${Math.round(cb*e.levels[i])})`));
  },
  loader:(ts,dt,cr,cg,cb,st)=>{
    const e=ES.loader,spd=st.loaderSpeed||80,fms=st.loaderFlashMs||200,fsec=st.loaderFadeSec||21;
    if(e.phase===0){
      if(ts-e.lastTs>spd){e.lastTs=ts;e.sweep++;if(e.sweep>NUM_LEDS){e.base++;if(e.base>NUM_LEDS){e.phase=1;e.flashCount=0;e.flashOn=true;e.lastTs=ts;}else e.sweep=e.base;}}
      circles.forEach((c,i)=>c.setAttribute('fill',i<e.sweep?`rgb(${cr},${cg},${cb})`:'#2a1a08'));
    }else if(e.phase===1){
      if(ts-e.lastTs>fms){e.lastTs=ts;e.flashOn=!e.flashOn;if(!e.flashOn){e.flashCount++;if(e.flashCount>=7){e.phase=2;e.fadeStart=ts;}}}
      circles.forEach(c=>c.setAttribute('fill',e.flashOn?`rgb(${cr},${cg},${cb})`:'#2a1a08'));
    }else{
      const g=Math.round(Math.min(1,(ts-e.fadeStart)/(fsec*1000))*255);
      circles.forEach(c=>c.setAttribute('fill',`rgb(0,${g},0)`));
    }
  },
  gaslight:(ts,dt,cr,cg,cb,st)=>{
    const e=ES.gaslight;e.phase+=Math.PI*2*1.5*dt;
    const dStart=st.descentStart,inD=st.gasCycle>=dStart,inW=!inD&&st.gasCycle===dStart-1;
    let bMin=inD?st.gasDeepBrightnessMin:st.gasMinBrightness,bMax=inD?st.gasDeepBrightnessMax:st.gasMaxBrightness;
    if(inD&&st.gasDescentCycles>1){const p=(st.gasCycle-dStart)/(st.gasDescentCycles-1);bMin=st.gasMinBrightness+(st.gasDeepBrightnessMin-st.gasMinBrightness)*p;bMax=st.gasMaxBrightness+(st.gasDeepBrightnessMax-st.gasMaxBrightness)*p;}
    let wb=bMin+(0.5+0.5*Math.sin(e.phase))*(bMax-bMin);
    if(!e.txActive&&ts-e.txCheck>100){e.txCheck=ts;if(Math.random()*100<(inW?st.gasWarnProbability*3:st.gasWarnProbability)){e.txActive=true;e.txStart=ts;e.txDepth=40+Math.random()*50;}}
    let fb=wb;if(e.txActive){const age=ts-e.txStart;if(age<80)fb=Math.max(5,wb-e.txDepth);else e.txActive=false;}
    fb=Math.max(5,Math.min(255,fb))/255;
    circles.forEach(c=>{const v=st.gasVariation>0?(Math.random()-0.5)*2*st.gasVariation/255:0;const s=Math.max(0.02,Math.min(1,fb+v));c.setAttribute('fill',`rgb(${Math.round(cr*s)},${Math.round(cg*s)},${Math.round(cb*s)})`);});
  },
};
let prevEffectP='';
function animLoop(ts){
  const dt=animLastTs?Math.min((ts-animLastTs)/1000,0.1):0.016;
  animLastTs=ts;
  if(state.effect!==prevEffectP){const r=ES_RESET[state.effect];if(r&&ES[state.effect])r(ES[state.effect]);prevEffectP=state.effect;}
  const cr=parseInt(state.color.slice(1,3),16),cg=parseInt(state.color.slice(3,5),16),cb=parseInt(state.color.slice(5,7),16);
  if(!state.power||state.effect==='off_effect'){circles.forEach(c=>c.setAttribute('fill','#2a1a08'));}
  else{const render=EFFECT_RENDERERS[state.effect];if(render)render(ts,dt,cr,cg,cb,state);}
  ringDotsP.style.opacity=state.power?(0.12+(state.brightness/255)*0.88):'0.08';
  requestAnimationFrame(animLoop);
}
requestAnimationFrame(animLoop);
async function pollStatus(){
  try{
    const r=await fetch('/getStatus',{signal:AbortSignal.timeout(2000)});
    const d=await r.json();
    state.power=d.power;state.effect=d.effect;state.brightness=d.brightness;state.color=d.color;
    state.chaseSpeed=d.chaseSpeed??state.chaseSpeed;state.chaseSpokes=d.chaseSpokes??state.chaseSpokes;
    state.loaderSpeed=d.loaderSpeed??state.loaderSpeed;state.loaderFlashMs=d.loaderFlashMs??state.loaderFlashMs;state.loaderFadeSec=d.loaderFadeSec??state.loaderFadeSec;
    state.gasCycle=d.cycle;state.gasTotalCycles=d.totalCycles;
    state.gasMinBrightness=d.minBrightness;state.gasMaxBrightness=d.maxBrightness;
    state.gasDeepBrightnessMin=d.deepBrightnessMin;state.gasDeepBrightnessMax=d.deepBrightnessMax;
    state.gasDescentCycles=d.descentCycles;state.gasVariation=d.variation;
    state.gasWarnProbability=d.warnProbability;state.descentStart=d.descentStart;
    const dStart=d.descentStart;
    const inD=d.cycle>=dStart;
    const inW=!inD&&d.cycle===dStart-1;
    document.getElementById('effectLabelP').textContent=EFFECT_NAMES[d.effect]||d.effect;
    document.getElementById('phaseBadgeP').textContent=inD?'DESCENT PHASE':inW?'PRE-DESCENT':d.running?'VIGIL ACTIVE':'PAUSED';
    document.getElementById('ringSvgP').style.filter=d.power?`drop-shadow(0 0 14px ${d.color}99)`:'none';
    document.getElementById('statusDotP').className='status-dot online';
    document.getElementById('statusTxtP').textContent='FIELD UNIT RESPONDING \u2014 ESP-32';
  }catch{
    document.getElementById('statusDotP').className='status-dot';
    document.getElementById('statusTxtP').textContent='SIGNAL LOST';
  }
}
setInterval(pollStatus,1000);
pollStatus();
</script>
</body>
</html>
)===";

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

/* ── Responsive layout ─────────────────────────────────── */

/* Fluid base: never overflow on tiny screens */
.page { max-width: min(520px, calc(100vw - 32px)); }

/* Tablet: 600 px+ — wider card, bigger ring, 3-col effects, 2-col gas */
@media (min-width: 600px) {
  .page { max-width: 660px; }
  .dossier { padding: 42px 40px 34px; }
  .ring-svg { width: 172px; height: 172px; }
  .header-title { font-size: 1.45rem; }
  .effects-grid { grid-template-columns: repeat(3, 1fr); }
  .gas-grid { grid-template-columns: 1fr 1fr; }
  .gas-actions { grid-template-columns: repeat(3, 1fr); }
  .colour-swatches { gap: 11px; }
  .swatch { width: 32px; height: 32px; }
}

/* Desktop: 960 px+ — two-column dossier body */
@media (min-width: 960px) {
  .page { max-width: 1020px; }
  .dossier { padding: 48px 48px 38px; }
  .header-title { font-size: 1.65rem; }
  .ring-svg { width: 210px; height: 210px; }
  .dossier-body {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0 40px;
    align-items: start;
  }
  /* column dividers */
  .dossier-col + .dossier-col {
    border-left: 1px solid rgba(26,18,8,0.18);
    padding-left: 40px;
  }
  .effects-grid { grid-template-columns: 1fr 1fr; }
  .gas-grid { grid-template-columns: 1fr 1fr; }
  .swatch { width: 34px; height: 34px; }
}

/* Scale-up font sizes smoothly between 320 and 960 px */
@media (min-width: 480px) {
  .field-label  { font-size: clamp(.54rem, 1.1vw, .65rem); }
  .effect-btn   { font-size: clamp(.7rem,  1.3vw, .82rem); }
  .gas-row label{ font-size: clamp(.68rem, 1.2vw, .78rem); }
  .status-text  { font-size: clamp(.56rem, 1vw,  .65rem);  }
}

/* Explainer hints */
.field-hint {
  font-size: .5rem;
  letter-spacing: .08em;
  color: rgba(26,18,8,.48);
  font-style: italic;
  margin: -6px 0 10px;
  line-height: 1.55;
}
.gas-hint {
  display: block;
  font-size: .44rem;
  letter-spacing: .06em;
  color: rgba(26,18,8,.38);
  font-style: italic;
  font-weight: normal;
  line-height: 1.4;
  margin-top: 2px;
}

/* Chase parameter section */
.chase-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
  margin-top: 8px;
}
.chase-row {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.chase-row label {
  font-size: .64rem;
  letter-spacing: .06em;
  color: var(--ink-faded);
  text-transform: uppercase;
}
.chase-row input[type="number"] {
  width: 100%;
  background: rgba(26,18,8,0.06);
  border: 1px solid rgba(26,18,8,0.28);
  color: var(--ink);
  padding: 6px 8px;
  font-family: 'Courier Prime', monospace;
  font-size: .78rem;
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

    <div class="dossier-body"><div class="dossier-col">

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
    <p class="field-hint">Enables or disables all LED output. Ring holds its last colour when dormant.</p>
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
    <p class="field-hint">Select a spectral preset or dial in a custom hue. Affects all colour-based effects.</p>
    <div class="colour-swatches" id="swatches"></div>
    <div class="colour-row">
      <input type="color" id="colorPicker" value="#4a8c3a">
      <span class="hex-code" id="hexDisplay">[ 4A8C3A ]</span>
    </div>

    <hr class="divider">

    <div class="field-label">Intensity Modulator — Candle Power</div>
    <p class="field-hint">Global brightness ceiling applied to every effect. 0 = off, 255 = maximum output.</p>
    <div class="bright-row">
      <input type="range" id="brightness" min="0" max="255" value="180">
      <span class="bright-val" id="brightnessVal">180</span>
    </div>

    </div><!-- /dossier-col left -->
    <div class="dossier-col">

    <div class="field-label">Manifestation Protocol — Select Ritual</div>
    <p class="field-hint">Choose the active light behaviour. Each ritual uses the selected colour and brightness above.</p>
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
      <button class="effect-btn" data-effect="loader">
        <span class="code">PROTO-07</span>Summoning Seal
      </button>
      <button class="effect-btn" data-effect="gaslight">
        <span class="code">PROTO-06</span>Gaslight Vigil
      </button>
      <button class="effect-btn" data-effect="off_effect">
        <span class="code">PROTO-00</span>Extinguish
      </button>
    </div>

    <hr class="divider">

    <div class="field-label">Elder Sign Chase — Ritual Parameters</div>
    <p class="field-hint">Controls the build-up sequence and steady-state formation. The ring builds from 1 spoke to the target count, adding one per revolution, then holds the even formation.</p>
    <div class="chase-grid">
      <div class="chase-row">
        <label for="chaseSpokes">Spoke Count<span class="gas-hint">LEDs in steady formation (1–24)</span></label>
        <input type="number" id="chaseSpokes" min="1" max="24" value="8">
      </div>
      <div class="chase-row">
        <label for="chaseSpeed">Step Speed<span class="gas-hint">Milliseconds per step (10–500)</span></label>
        <input type="number" id="chaseSpeed" min="10" max="500" value="60">
      </div>
    </div>

    <hr class="divider">

    <div class="field-label">Summoning Seal — Ritual Parameters</div>
    <p class="field-hint">Each step lights one additional LED sequentially around the ring. After all 24 revolutions, the ring flashes then fades in green.</p>
    <div class="chase-grid">
      <div class="chase-row">
        <label for="loaderSpeed">Step Speed<span class="gas-hint">Milliseconds per LED (20–1000)</span></label>
        <input type="number" id="loaderSpeed" min="20" max="1000" value="80">
      </div>
      <div class="chase-row">
        <label for="loaderFlashMs">Flash Speed<span class="gas-hint">Milliseconds per flash toggle (50–2000)</span></label>
        <input type="number" id="loaderFlashMs" min="50" max="2000" value="200">
      </div>
      <div class="chase-row">
        <label for="loaderFadeSec">Fade Duration<span class="gas-hint">Seconds for green fade-in (1–300)</span></label>
        <input type="number" id="loaderFadeSec" min="1" max="300" value="21">
      </div>
    </div>

    <hr class="divider">

    <div class="field-label">Gaslight Parameters — Ritual Timing Matrix</div>
    <p class="field-hint">Tune the Gaslight Vigil behaviour. The ritual runs through Normal cycles, then fades during Descent cycles, creating a slow, imperceptible dimming effect.</p>

    <div class="gas-grid">
      <div class="gas-row">
        <label for="gasTargetSeconds">Cycle Duration<span class="gas-hint">Seconds each brightness cycle lasts</span></label>
        <input type="number" id="gasTargetSeconds" min="1" max="99999" value="777">
        <span class="gas-unit">sec</span>
      </div>

      <div class="gas-row">
        <label for="gasTotalCycles">Total Cycles<span class="gas-hint">Total number of cycles before the vigil ends</span></label>
        <input type="number" id="gasTotalCycles" min="1" max="99" value="7">
        <span class="gas-unit">count</span>
      </div>

      <div class="gas-row">
        <label for="gasDescentCycles">Descent Cycles<span class="gas-hint">Final N cycles that gradually dim to Descent range</span></label>
        <input type="number" id="gasDescentCycles" min="1" max="99" value="2">
        <span class="gas-unit">count</span>
      </div>

      <div class="gas-row">
        <label for="gasMinBrightness">Normal Min<span class="gas-hint">Lowest brightness during normal cycles (must be &lt; Normal Max)</span></label>
        <input type="number" id="gasMinBrightness" min="0" max="255" value="190">
        <span class="gas-unit">0–255</span>
      </div>

      <div class="gas-row">
        <label for="gasMaxBrightness">Normal Max<span class="gas-hint">Peak brightness during normal cycles</span></label>
        <input type="number" id="gasMaxBrightness" min="0" max="255" value="255">
        <span class="gas-unit">0–255</span>
      </div>

      <div class="gas-row">
        <label for="gasDeepBrightnessMin">Descent Min<span class="gas-hint">Lowest brightness reached during descent (must be &lt; Descent Max)</span></label>
        <input type="number" id="gasDeepBrightnessMin" min="0" max="255" value="5">
        <span class="gas-unit">0–255</span>
      </div>

      <div class="gas-row">
        <label for="gasDeepBrightnessMax">Descent Max<span class="gas-hint">Peak brightness during the dimmed descent phase</span></label>
        <input type="number" id="gasDeepBrightnessMax" min="0" max="255" value="25">
        <span class="gas-unit">0–255</span>
      </div>

      <div class="gas-row">
        <label for="gasVariation">Per-LED Variation<span class="gas-hint">Random flicker spread between individual LEDs. 0 = uniform.</span></label>
        <input type="number" id="gasVariation" min="0" max="50" value="15">
        <span class="gas-unit">0–50</span>
      </div>

      <div class="gas-row">
        <label for="gasWarnProbability">Warning Chance<span class="gas-hint">Probability per 100 ms of a brief flicker transient</span></label>
        <input type="number" id="gasWarnProbability" min="0" max="100" value="10">
        <span class="gas-unit">%/100ms</span>
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

    </div><!-- /dossier-col right -->
    </div><!-- /dossier-body -->

  </div>
  <div class="footer-stamp">
    Miskatonic University Applied Sciences Div. — <span class="redacted">██████████</span> — 1943
    &nbsp;&nbsp;|&nbsp;&nbsp;<a href="/config" style="color:rgba(200,184,154,.4);font-size:.65rem;letter-spacing:.1em;text-decoration:none;">&#9881; Config</a>
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
    chaseSpokes: 8,
    chaseSpeed: 60,
    loaderSpeed: 80,
    loaderFlashMs: 200,
    loaderFadeSec: 21,
    gasTargetSeconds: 777,
    gasTotalCycles: 7,
    gasDescentCycles: 2,
    gasMinBrightness: 190,
    gasMaxBrightness: 255,
    gasDeepBrightnessMin: 5,
    gasDeepBrightnessMax: 25,
    gasVariation: 15,
    gasWarnProbability: 10,
    gasCycle: 1,
    gasElapsed: 0,
    gasRunning: true,
    descentStart: 6
  };

  // ── Animation State ──────────────────────────────────────
  let animLastTs = 0;
  const ES = {
    pulse:    { phase: 0 },
    rainbow:  { hue: 0 },
    chase:    { pos: 0, lastTs: 0, revStep: 0, litCount: 1 },
    sparkle:  { lastTs: 0, levels: new Array(NUM_LEDS).fill(0) },
    loader:   { base: 1, sweep: 1, lastTs: 0, phase: 0, flashCount: 0, flashOn: false, fadeStart: 0 },
    gaslight: { phase: 0, txActive: false, txStart: 0, txDepth: 0, txCheck: 0 },
  };
  const ES_RESET = {
    pulse:    e => { e.phase = 0; },
    rainbow:  e => { e.hue = 0; },
    chase:    e => { e.pos = 0; e.lastTs = 0; e.revStep = 0; e.litCount = 1; },
    sparkle:  e => { e.lastTs = 0; e.levels.fill(0); },
    loader:   e => { e.base = 1; e.sweep = 1; e.lastTs = 0; e.phase = 0; e.flashCount = 0; e.flashOn = false; e.fadeStart = 0; },
    gaslight: e => { e.phase = 0; e.txActive = false; },
  };
  const EFFECT_RENDERERS = {
    solid: (ts, dt, cr, cg, cb) => {
      circles.forEach(c => c.setAttribute('fill', state.color));
    },
    pulse: (ts, dt, cr, cg, cb) => {
      const e = ES.pulse; e.phase += 0.05;
      const s = 0.04 + Math.max(0, 0.5 + 0.5 * Math.sin(e.phase)) * 0.96;
      circles.forEach(c => c.setAttribute('fill', `rgb(${Math.round(cr*s)},${Math.round(cg*s)},${Math.round(cb*s)})`));
    },
    rainbow: (ts, dt, cr, cg, cb) => {
      const e = ES.rainbow; e.hue = (e.hue + 2) % 360;
      circles.forEach((c, i) => c.setAttribute('fill', `hsl(${(e.hue + i*(360/NUM_LEDS))%360},80%,42%)`));
    },
    chase: (ts, dt, cr, cg, cb) => {
      const e = ES.chase, spd = state.chaseSpeed || 60, tgt = state.chaseSpokes || 8;
      if (ts - e.lastTs > spd) {
        e.lastTs = ts; e.pos = (e.pos + 1) % NUM_LEDS;
        if (e.litCount < NUM_LEDS) { e.revStep++; if (e.revStep >= NUM_LEDS) { e.revStep = 0; e.litCount++; } }
      }
      circles.forEach(c => c.setAttribute('fill', '#2a1a08'));
      const n = e.litCount < NUM_LEDS ? e.litCount : tgt;
      for (let s = 0; s < n; s++)
        circles[(e.pos + Math.round(s * NUM_LEDS / n)) % NUM_LEDS].setAttribute('fill', `rgb(${cr},${cg},${cb})`);
    },
    sparkle: (ts, dt, cr, cg, cb) => {
      const e = ES.sparkle;
      if (ts - e.lastTs > 40) { e.lastTs = ts; e.levels = e.levels.map(v => v * (180/255)); e.levels[Math.floor(Math.random() * NUM_LEDS)] = 1.0; }
      circles.forEach((c, i) => { const sl = e.levels[i]; c.setAttribute('fill', `rgb(${Math.round(cr*sl)},${Math.round(cg*sl)},${Math.round(cb*sl)})`); });
    },
    loader: (ts, dt, cr, cg, cb) => {
      const e = ES.loader, spd = state.loaderSpeed || 80, fms = state.loaderFlashMs || 200, fsec = state.loaderFadeSec || 21;
      if (e.phase === 0) {
        if (ts - e.lastTs > spd) {
          e.lastTs = ts; e.sweep++;
          if (e.sweep > NUM_LEDS) { e.base++; if (e.base > NUM_LEDS) { e.phase = 1; e.flashCount = 0; e.flashOn = true; e.lastTs = ts; } else e.sweep = e.base; }
        }
        circles.forEach((c, i) => c.setAttribute('fill', i < e.sweep ? `rgb(${cr},${cg},${cb})` : '#2a1a08'));
      } else if (e.phase === 1) {
        if (ts - e.lastTs > fms) { e.lastTs = ts; e.flashOn = !e.flashOn; if (!e.flashOn) { e.flashCount++; if (e.flashCount >= 7) { e.phase = 2; e.fadeStart = ts; } } }
        circles.forEach(c => c.setAttribute('fill', e.flashOn ? `rgb(${cr},${cg},${cb})` : '#2a1a08'));
      } else {
        const g = Math.round(Math.min(1, (ts - e.fadeStart) / (fsec * 1000)) * 255);
        circles.forEach(c => c.setAttribute('fill', `rgb(0,${g},0)`));
      }
    },
    gaslight: (ts, dt, cr, cg, cb) => {
      const e = ES.gaslight; e.phase += Math.PI * 2 * 1.5 * dt;
      const dStart = state.descentStart, inD = state.gasCycle >= dStart, inW = !inD && state.gasCycle === dStart - 1;
      let bMin = inD ? state.gasDeepBrightnessMin : state.gasMinBrightness;
      let bMax = inD ? state.gasDeepBrightnessMax : state.gasMaxBrightness;
      if (inD && state.gasDescentCycles > 1) {
        const p = (state.gasCycle - dStart) / (state.gasDescentCycles - 1);
        bMin = state.gasMinBrightness + (state.gasDeepBrightnessMin - state.gasMinBrightness) * p;
        bMax = state.gasMaxBrightness + (state.gasDeepBrightnessMax - state.gasMaxBrightness) * p;
      }
      let wb = bMin + (0.5 + 0.5 * Math.sin(e.phase)) * (bMax - bMin);
      if (!e.txActive && ts - e.txCheck > 100) {
        e.txCheck = ts;
        if (Math.random() * 100 < (inW ? state.gasWarnProbability * 3 : state.gasWarnProbability))
          { e.txActive = true; e.txStart = ts; e.txDepth = 40 + Math.random() * 50; }
      }
      let fb = wb;
      if (e.txActive) { const age = ts - e.txStart; if (age < 80) fb = Math.max(5, wb - e.txDepth); else e.txActive = false; }
      fb = Math.max(5, Math.min(255, fb)) / 255;
      circles.forEach(c => { const v = state.gasVariation > 0 ? (Math.random() - 0.5) * 2 * state.gasVariation / 255 : 0; const s = Math.max(0.02, Math.min(1, fb + v)); c.setAttribute('fill', `rgb(${Math.round(cr*s)},${Math.round(cg*s)},${Math.round(cb*s)})`); });
    },
  };

  // ── Animation Loop ───────────────────────────────────────
  function resetAnimState(effect) {
    const reset = ES_RESET[effect];
    if (reset && ES[effect]) reset(ES[effect]);
  }

  function animLoop(ts) {
    const dt = animLastTs ? Math.min((ts - animLastTs) / 1000, 0.1) : 0.016;
    animLastTs = ts;
    const cr = parseInt(state.color.slice(1,3), 16);
    const cg = parseInt(state.color.slice(3,5), 16);
    const cb = parseInt(state.color.slice(5,7), 16);
    if (!state.power || state.effect === 'off_effect') {
      circles.forEach(c => c.setAttribute('fill', '#2a1a08'));
    } else {
      const render = EFFECT_RENDERERS[state.effect];
      if (render) render(ts, dt, cr, cg, cb);
    }
    ringDots.style.opacity = state.power ? (0.12 + (state.brightness / 255) * 0.88) : '0.08';
    requestAnimationFrame(animLoop);
  }
  requestAnimationFrame(animLoop);
  // ─────────────────────────────────────────────────────────

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
    sendDebounced();
  }

  function updateRingColor(hex) {
    // Circle colours are driven by the animation loop; just update the glow.
    document.getElementById('ringSvg').style.filter = state.power
      ? `drop-shadow(0 0 12px ${hex}99)`
      : 'drop-shadow(0 0 2px rgba(26,18,8,0.4))';
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
    sendCommand('/power', { state: state.power ? 1 : 0 });
  });

  document.querySelectorAll('.effect-btn[data-effect]').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.effect-btn[data-effect]').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      state.effect = btn.dataset.effect;
      resetAnimState(state.effect);
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

  // Only update an input if the user isn't currently editing it
  function safeSetInput(id, val) {
    const el = document.getElementById(id);
    if (el && document.activeElement !== el) el.value = val;
  }

  async function pollStatus() {
    try {
      const r = await fetch('/getStatus', { signal: AbortSignal.timeout(2000) });
      const data = await r.json();

      const prevEffect = state.effect;
      state.power      = data.power;
      state.effect     = data.effect;
      state.brightness = data.brightness;
      state.color      = data.color;
      state.gasCycle   = data.cycle;
      state.gasElapsed = data.elapsed;
      state.gasRunning = data.running;
      state.descentStart = data.descentStart;

      state.chaseSpokes         = data.chaseSpokes    ?? state.chaseSpokes;
      state.chaseSpeed          = data.chaseSpeed     ?? state.chaseSpeed;
      state.loaderSpeed         = data.loaderSpeed    ?? state.loaderSpeed;
      state.loaderFlashMs       = data.loaderFlashMs  ?? state.loaderFlashMs;
      state.loaderFadeSec       = data.loaderFadeSec  ?? state.loaderFadeSec;
      state.gasTargetSeconds    = data.targetSeconds;
      state.gasTotalCycles      = data.totalCycles;
      state.gasDescentCycles    = data.descentCycles;
      state.gasMinBrightness    = data.minBrightness;
      state.gasMaxBrightness    = data.maxBrightness;
      state.gasDeepBrightnessMin = data.deepBrightnessMin;
      state.gasDeepBrightnessMax = data.deepBrightnessMax;
      state.gasVariation        = data.variation;
      state.gasWarnProbability  = data.warnProbability;

      if (prevEffect !== state.effect) resetAnimState(state.effect);

      powerToggle.checked = state.power;
      powerLabel.textContent = state.power ? 'ACTIVE' : 'DORMANT';
      powerLabel.className = 'pstate' + (state.power ? '' : ' off');

      colorPicker.value = state.color;
      hexDisplay.textContent = '[ ' + state.color.replace('#','').toUpperCase() + ' ]';
      brightnessSlider.value = state.brightness;
      brightnessVal.textContent = state.brightness;

      safeSetInput('chaseSpokes',   state.chaseSpokes);
      safeSetInput('chaseSpeed',    state.chaseSpeed);
      safeSetInput('loaderSpeed',   state.loaderSpeed);
      safeSetInput('loaderFlashMs', state.loaderFlashMs);
      safeSetInput('loaderFadeSec', state.loaderFadeSec);
      safeSetInput('gasTargetSeconds',    state.gasTargetSeconds);
      safeSetInput('gasTotalCycles',      state.gasTotalCycles);
      safeSetInput('gasDescentCycles',    state.gasDescentCycles);
      safeSetInput('gasMinBrightness',    state.gasMinBrightness);
      safeSetInput('gasMaxBrightness',    state.gasMaxBrightness);
      safeSetInput('gasDeepBrightnessMin', state.gasDeepBrightnessMin);
      safeSetInput('gasDeepBrightnessMax', state.gasDeepBrightnessMax);
      safeSetInput('gasVariation',        state.gasVariation);
      safeSetInput('gasWarnProbability',  state.gasWarnProbability);

      const inDescent = state.gasCycle >= state.descentStart;
      const inWarning = !inDescent && state.gasCycle === state.descentStart - 1;
      const phaseLabel = inDescent ? 'DESCENT' : inWarning ? 'WARNING' : 'NORMAL';

      document.getElementById('gasCycleReadout').textContent =
        `${state.gasCycle} / ${state.gasTotalCycles}`;
      document.getElementById('gasElapsedReadout').textContent =
        `${state.gasElapsed} / ${state.gasTargetSeconds}s`;
      document.getElementById('gasStateReadout').textContent =
        state.gasRunning ? phaseLabel : 'PAUSED';

      document.querySelectorAll('.effect-btn[data-effect]').forEach(b => {
        b.classList.toggle('active', b.dataset.effect === state.effect);
      });

      document.querySelectorAll('.swatch').forEach(s => {
        s.classList.toggle('active', s.dataset.color.toLowerCase() === state.color.toLowerCase());
      });

      updateRingColor(state.color);
      setStatus(true);
    } catch {
      setStatus(false);
    }
  }

  const GAS_PARAM_PAIRS = {
    gasMinBrightness:    'gasMaxBrightness',
    gasDeepBrightnessMin:'gasDeepBrightnessMax'
  };

  [
    'gasTargetSeconds',
    'gasTotalCycles',
    'gasDescentCycles',
    'gasMinBrightness',
    'gasMaxBrightness',
    'gasDeepBrightnessMin',
    'gasDeepBrightnessMax',
    'gasVariation',
    'gasWarnProbability'
  ].forEach(id => {
    const el = document.getElementById(id);
    el.addEventListener('change', e => {
      // Validate min < max pairs
      const pairId = GAS_PARAM_PAIRS[id];
      if (pairId) {
        const pairEl = document.getElementById(pairId);
        if (pairEl && parseInt(e.target.value) > parseInt(pairEl.value)) {
          e.target.style.outline = '2px solid #8b1a1a';
          return; // don't send invalid value
        }
      }
      e.target.style.outline = '';
      setParam(id, e.target.value);
    });
    el.addEventListener('input', e => {
      // Clear error highlight as soon as user corrects it
      const pairId = GAS_PARAM_PAIRS[id];
      if (pairId) {
        const pairEl = document.getElementById(pairId);
        if (pairEl && parseInt(e.target.value) <= parseInt(pairEl.value)) {
          e.target.style.outline = '';
        }
      }
    });
  });

  ['chaseSpokes', 'chaseSpeed', 'loaderSpeed', 'loaderFlashMs', 'loaderFadeSec'].forEach(id => {
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
// ── Preferences helpers ───────────────────────────────────
Preferences prefs;

void loadConfig() {
  prefs.begin("ringcfg", true);
  int savedVer = prefs.getInt("cfgVersion", 0);
  if (savedVer != CONFIG_VERSION)
    Serial.printf("[CFG] Schema version mismatch: stored=%d current=%d — new fields will use defaults.\n",
                  savedVer, CONFIG_VERSION);
  ringName   = prefs.getString("ringName",   "Ring 1");
  micEnabled = prefs.getBool  ("micEnabled", false);
  phraseCount= prefs.getInt   ("phraseCount", 0);
  if (phraseCount > MAX_PHRASES) phraseCount = MAX_PHRASES;
  for (int i = 0; i < phraseCount; i++) {
    String px = "p" + String(i);
    phraseConfigs[i].phrase       = prefs.getString((px+"phr").c_str(), "");
    phraseConfigs[i].effect       = prefs.getString((px+"eff").c_str(), "solid");
    phraseConfigs[i].r            = (uint8_t)prefs.getInt((px+"r").c_str(), 255);
    phraseConfigs[i].g            = (uint8_t)prefs.getInt((px+"g").c_str(), 255);
    phraseConfigs[i].b            = (uint8_t)prefs.getInt((px+"b").c_str(), 255);
    phraseConfigs[i].durationSec  = prefs.getInt((px+"dur").c_str(), 10);
    phraseConfigs[i].returnEffect = prefs.getString((px+"ret").c_str(), "gaslight");
  }
  prefs.end();
  Serial.printf("[CFG] Loaded: name=%s mic=%d phrases=%d\n",
                ringName.c_str(), micEnabled, phraseCount);
}

void saveConfig() {
  prefs.begin("ringcfg", false);
  prefs.putInt   ("cfgVersion",  CONFIG_VERSION);
  prefs.putString("ringName",    ringName);
  prefs.putBool  ("micEnabled",  micEnabled);
  prefs.putInt   ("phraseCount", phraseCount);
  for (int i = 0; i < phraseCount; i++) {
    String px = "p" + String(i);
    prefs.putString((px+"phr").c_str(), phraseConfigs[i].phrase);
    prefs.putString((px+"eff").c_str(), phraseConfigs[i].effect);
    prefs.putInt   ((px+"r").c_str(),   phraseConfigs[i].r);
    prefs.putInt   ((px+"g").c_str(),   phraseConfigs[i].g);
    prefs.putInt   ((px+"b").c_str(),   phraseConfigs[i].b);
    prefs.putInt   ((px+"dur").c_str(), phraseConfigs[i].durationSec);
    prefs.putString((px+"ret").c_str(), phraseConfigs[i].returnEffect);
  }
  prefs.end();
  Serial.printf("[CFG] Saved: name=%s mic=%d phrases=%d\n",
                ringName.c_str(), micEnabled, phraseCount);
}

// ── INMP441 I2S init (new ESP-IDF i2s_std driver) ────────
void micInit() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_new_channel(&chan_cfg, NULL, &rx_chan);

  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_SCK_PIN,
      .ws   = (gpio_num_t)I2S_WS_PIN,
      .dout = I2S_GPIO_UNUSED,
      .din  = (gpio_num_t)I2S_SD_PIN,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv   = false,
      },
    },
  };
  i2s_channel_init_std_mode(rx_chan, &std_cfg);
  i2s_channel_enable(rx_chan);
  micInitDone = true;
  Serial.println("[MIC] INMP441 initialised (SCK=14 WS=15 SD=32).");
}

// ── Edge Impulse inference stub ───────────────────────────
// When your model is ready:
//   1. Train phrases on edgeimpulse.com
//   2. Export as "Arduino library" and install via Sketch > Include Library > Add .ZIP
//   3. Add:  #include <your_project_inferencing.h>
//   4. Replace the stub body with the commented code below
//
String runInference(int16_t* buf, size_t len) {
  (void)buf; (void)len;
  // --- Uncomment after adding your Edge Impulse library ---
  // signal_t signal;
  // numpy::signal_from_buffer(buf, len, &signal);
  // ei_impulse_result_t result = { 0 };
  // EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
  // if (err != EI_IMPULSE_OK) return "";
  // float best = 0.7f;  int bestIdx = -1;  // 0.7 confidence threshold
  // for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
  //   if (result.classification[i].value > best) {
  //     best = result.classification[i].value;  bestIdx = (int)i;
  //   }
  // }
  // if (bestIdx >= 0) return String(ei_classifier_inferencing_categories[bestIdx]);
  return "";  // no detection until EI library is added
}

// ── Phrase trigger ────────────────────────────────────────
void activateEffect(const String& id, CRGB colour) {
  currentColor  = colour;
  currentEffect = id;
  FastLED.setBrightness(brightness);
  const EffectDef* e = findEffect(id);
  if (e) { lastUpdate = 0; e->onActivate(); }
}

void triggerPhrase(const String& detected) {
  for (int i = 0; i < phraseCount; i++) {
    if (phraseConfigs[i].phrase.length() == 0) continue;
    if (!detected.equalsIgnoreCase(phraseConfigs[i].phrase)) continue;
    Serial.printf("[MIC] Matched: \"%s\" -> %s\n",
                  phraseConfigs[i].phrase.c_str(), phraseConfigs[i].effect.c_str());
    activateEffect(phraseConfigs[i].effect,
                   CRGB(phraseConfigs[i].r, phraseConfigs[i].g, phraseConfigs[i].b));
    if (phraseConfigs[i].durationSec > 0) {
      triggerActive      = true;
      triggerStartMs     = millis();
      triggerDuration    = phraseConfigs[i].durationSec;
      triggerReturn      = phraseConfigs[i].returnEffect;
      triggerReturnColor = currentColor;  // save colour before trigger overwrites it
    }
    break;
  }
}

void checkTriggerExpiry() {
  if (!triggerActive) return;
  if (millis() - triggerStartMs >= (unsigned long)triggerDuration * 1000UL) {
    triggerActive = false;
    activateEffect(triggerReturn, triggerReturnColor);
    Serial.println("[MIC] Trigger expired, returning to idle effect.");
  }
}

// ── Mic sampling loop ────────────────────────────────────
void handleMicLoop() {
  if (!micEnabled || !micInitDone) return;
  int16_t chunk[256];
  size_t  bytesRead = 0;
  i2s_channel_read(rx_chan, chunk, sizeof(chunk), &bytesRead, 10 / portTICK_PERIOD_MS);
  size_t n = bytesRead / sizeof(int16_t);
  for (size_t i = 0; i < n && micBufferPos < MIC_BUFFER_SIZE; i++)
    micBuffer[micBufferPos++] = chunk[i];
  if (micBufferPos >= MIC_BUFFER_SIZE) {
    String detected = runInference(micBuffer, MIC_BUFFER_SIZE);
    if (detected.length() > 0) triggerPhrase(detected);
    // Slide window by half for overlap
    memcpy(micBuffer, micBuffer + MIC_BUFFER_SIZE / 2,
           (MIC_BUFFER_SIZE / 2) * sizeof(int16_t));
    micBufferPos = MIC_BUFFER_SIZE / 2;
  }
}
// ──────────────────────────────────────────────────────────

String colorToHex(const CRGB& c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
  return String(buf);
}

// Escape a string for embedding inside a JSON "..." value
String jsonStr(const String& s) {
  String out;
  out.reserve(s.length() + 4);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if      (c == '"')  out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else                out += c;
  }
  return out;
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

  // Rate-limit to ~60 fps
  if (now - gasLastWaveMs < 16) return;

  // Time-based wave phase (~1.5 Hz)
  float dt = (gasLastWaveMs == 0)
    ? 0.016f
    : min((now - gasLastWaveMs) / 1000.0f, 0.1f);
  gasLastWaveMs = now;
  gasWavePhase += TWO_PI * 1.5f * dt;
  if (gasWavePhase > TWO_PI * 100.0f) gasWavePhase -= TWO_PI * 100.0f;

  // Determine which descent phase we are in
  int descentStart = max(1, gasTotalCycles - gasDescentCycles + 1);
  bool inDescent   = (gasCycleCounter >= descentStart);
  bool inWarning   = !inDescent && (gasCycleCounter == descentStart - 1);

  // Brightness range for current cycle position
  int bMin, bMax;
  if (inDescent) {
    float p = (gasDescentCycles > 1)
      ? (float)(gasCycleCounter - descentStart) / (float)(gasDescentCycles - 1)
      : 1.0f;
    bMin = (int)(gasMinBrightness + (gasDeepBrightnessMin - gasMinBrightness) * p);
    bMax = (int)(gasMaxBrightness + (gasDeepBrightnessMax - gasMaxBrightness) * p);
  } else {
    bMin = gasMinBrightness;
    bMax = gasMaxBrightness;
  }

  // Slow sine-wave base (simulates gas pressure rise/fall)
  float sineVal  = 0.5f + 0.5f * sinf(gasWavePhase);
  int   waveBright = bMin + (int)(sineVal * (float)(bMax - bMin));

  // Occasional transient dip (pressure spike / turbulence)
  if (!gasTransientActive && (now - gasLastTransientCheck > 100)) {
    gasLastTransientCheck = now;
    int prob = inWarning ? gasWarnProbability * 3 : gasWarnProbability;
    if ((int)random(100) < prob) {
      gasTransientActive = true;
      gasTransientStart  = now;
      gasTransientDepth  = (int)random(40, 90);
    }
  }

  int finalBright = waveBright;
  if (gasTransientActive) {
    unsigned long age = now - gasTransientStart;
    if (age < 80UL) {
      finalBright = max(5, waveBright - gasTransientDepth);
    } else {
      gasTransientActive = false;
    }
  }
  finalBright = constrain(finalBright, 5, 255);

  // Render with per-LED variation
  FastLED.setBrightness(brightness);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = currentColor;
    int var = (gasVariation > 0)
      ? (int)random(0, gasVariation * 2 + 1) - gasVariation
      : 0;
    leds[i].nscale8((uint8_t)constrain(finalBright + var, 5, 255));
  }
  FastLED.show();
}

// ── Per-effect activate / render functions ────────────────

void activateSolid() {
  fill_solid(leds, NUM_LEDS, currentColor);
  FastLED.setBrightness(brightness);
  FastLED.show();
}
void renderSolid() {
  unsigned long now = millis();
  if (now - lastUpdate > 50) {   // 20 fps is plenty for a static colour
    lastUpdate = now;
    fill_solid(leds, NUM_LEDS, currentColor);
    FastLED.show();
  }
}

static float pulsePhaseC = 0.0f;
void activatePulse() { pulsePhaseC = 0.0f; lastUpdate = 0; }
void renderPulse() {
  unsigned long now = millis();
  if (now - lastUpdate > 16) {
    lastUpdate = now;
    pulsePhaseC += 0.05f;
    uint8_t bright = (uint8_t)(127.5f + 127.5f * sinf(pulsePhaseC));
    fill_solid(leds, NUM_LEDS, currentColor);
    FastLED.setBrightness((uint8_t)map(bright, 0, 255, 10, brightness));
    FastLED.show();
  }
}

void activateRainbow() { rainbowHue = 0; lastUpdate = 0; }
void renderRainbow() {
  unsigned long now = millis();
  if (now - lastUpdate > 20) {
    lastUpdate = now;
    rainbowHue += 2;
    fill_rainbow(leds, NUM_LEDS, rainbowHue, 256 / NUM_LEDS);
    FastLED.setBrightness(brightness);
    FastLED.show();
  }
}

void activateChase() { chasePos = 0; chaseRevStep = 0; chaseLitCount = 1; lastUpdate = 0; }
void renderChase() {
  unsigned long now = millis();
  if (now - lastUpdate > (unsigned long)chaseSpeed) {
    lastUpdate = now;
    chasePos = (chasePos + 1) % NUM_LEDS;
    if (chaseLitCount < NUM_LEDS) {
      chaseRevStep++;
      if (chaseRevStep >= NUM_LEDS) { chaseRevStep = 0; chaseLitCount++; }
    }
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    int n = (chaseLitCount < NUM_LEDS) ? chaseLitCount : chaseSpokes;
    for (int s = 0; s < n; s++) {
      int idx = (chasePos + (int)round((float)s * NUM_LEDS / n)) % NUM_LEDS;
      leds[idx] = currentColor;
    }
    FastLED.setBrightness(brightness);
    FastLED.show();
  }
}

void activateSparkle() { lastUpdate = 0; }
void renderSparkle() {
  unsigned long now = millis();
  if (now - lastUpdate > 40) {
    lastUpdate = now;
    for (int i = 0; i < NUM_LEDS; i++) leds[i].nscale8(180);
    leds[random(NUM_LEDS)] = currentColor;
    FastLED.setBrightness(brightness);
    FastLED.show();
  }
}

void activateLoader() {
  loaderBase = 1; loaderSweep = 1; loaderPhase = 0;
  loaderFlashCount = 0; loaderFlashOn = false;
  lastUpdate = 0;
}
void renderLoader() {
  unsigned long now = millis();
  if (loaderPhase == 0) {
    if (now - lastUpdate > (unsigned long)loaderSpeed) {
      lastUpdate = now;
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      for (int i = 0; i < loaderSweep; i++) leds[i] = currentColor;
      FastLED.setBrightness(brightness);
      FastLED.show();
      loaderSweep++;
      if (loaderSweep > NUM_LEDS) {
        loaderBase++;
        if (loaderBase > NUM_LEDS) {
          loaderPhase = 1; loaderFlashCount = 0; loaderFlashOn = true;
          fill_solid(leds, NUM_LEDS, currentColor);
          FastLED.setBrightness(brightness);
          FastLED.show();
          lastUpdate = now;
        } else {
          loaderSweep = loaderBase;
        }
      }
    }
  } else if (loaderPhase == 1) {
    if (now - lastUpdate > (unsigned long)loaderFlashMs) {
      lastUpdate    = now;
      loaderFlashOn = !loaderFlashOn;
      fill_solid(leds, NUM_LEDS, loaderFlashOn ? currentColor : CRGB::Black);
      FastLED.setBrightness(brightness);
      FastLED.show();
      if (!loaderFlashOn) {
        loaderFlashCount++;
        if (loaderFlashCount >= 7) {
          fill_solid(leds, NUM_LEDS, CRGB::Black);
          FastLED.show();
          loaderPhase = 2;
          loaderFadeStart = now;
        }
      }
    }
  } else if (loaderPhase == 2) {
    unsigned long elapsed     = now - loaderFadeStart;
    unsigned long fadeDurMs   = (unsigned long)loaderFadeSec * 1000UL;
    float t = (elapsed >= fadeDurMs) ? 1.0f : (float)elapsed / (float)fadeDurMs;
    uint8_t g = (uint8_t)(t * 255.0f);
    fill_solid(leds, NUM_LEDS, CRGB(0, g, 0));
    FastLED.setBrightness(brightness);
    FastLED.show();
    if (t >= 1.0f) {
      // Sequence complete — hand off to solid green
      currentColor  = CRGB(0, 255, 0);
      currentEffect = "solid";
    }
  }
}

void activateGaslight() {
  gasCycleStart = millis(); gasWavePhase = 0.0f;
  gasLastWaveMs = 0;        gasTransientActive = false;
}
void renderGaslight() { runGaslightEffect(); }

void activateOff() { fill_solid(leds, NUM_LEDS, CRGB::Black); FastLED.show(); }
void renderOff()   { fill_solid(leds, NUM_LEDS, CRGB::Black); FastLED.show(); }

// ── Effect registry ───────────────────────────────────────
struct EffectDef {
  const char* id;
  void (*onActivate)();
  void (*onRender)();
};

const EffectDef EFFECTS[] = {
  { "solid",      activateSolid,    renderSolid    },
  { "pulse",      activatePulse,    renderPulse    },
  { "rainbow",    activateRainbow,  renderRainbow  },
  { "chase",      activateChase,    renderChase    },
  { "sparkle",    activateSparkle,  renderSparkle  },
  { "loader",     activateLoader,   renderLoader   },
  { "gaslight",   activateGaslight, renderGaslight },
  { "off_effect", activateOff,      renderOff      },
};
const int NUM_EFFECTS = (int)(sizeof(EFFECTS) / sizeof(EFFECTS[0]));

const EffectDef* findEffect(const String& id) {
  for (int i = 0; i < NUM_EFFECTS; i++)
    if (id == EFFECTS[i].id) return &EFFECTS[i];
  return nullptr;
}

// ── Effect dispatch ───────────────────────────────────────
void applyEffect() {
  if (!ledPower) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    return;
  }
  const EffectDef* e = findEffect(currentEffect);
  if (e) e->onRender();
}

// ── Handlers ──────────────────────────────────────────────

void handleGetConfig() {
  String json = "{";
  json += "\"ringName\":\"" + jsonStr(ringName) + "\",";
  json += "\"micEnabled\":" + String(micEnabled ? "true" : "false") + ",";
  json += "\"phrases\":[";
  for (int i = 0; i < phraseCount; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"phrase\":\""       + jsonStr(phraseConfigs[i].phrase)       + "\",";
    json += "\"effect\":\""       + jsonStr(phraseConfigs[i].effect)       + "\",";
    json += "\"r\":"              + String(phraseConfigs[i].r)             + ",";
    json += "\"g\":"              + String(phraseConfigs[i].g)             + ",";
    json += "\"b\":"              + String(phraseConfigs[i].b)             + ",";
    json += "\"duration\":"       + String(phraseConfigs[i].durationSec)   + ",";
    json += "\"returnEffect\":\"" + jsonStr(phraseConfigs[i].returnEffect) + "\"";
    json += "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleConfig() {
  server.send_P(200, "text/html", CONFIG_PAGE);
}

void handleSaveConfig() {
  if (server.hasArg("ringName"))   ringName   = server.arg("ringName");
  if (server.hasArg("micEnabled")) micEnabled = server.arg("micEnabled").toInt() == 1;

  phraseCount = 0;
  for (int i = 0; i < MAX_PHRASES; i++) {
    String pKey = "phrase" + String(i);
    if (!server.hasArg(pKey) || server.arg(pKey).length() == 0) continue;
    phraseConfigs[phraseCount].phrase       = server.arg(pKey);
    phraseConfigs[phraseCount].effect       = server.hasArg("effect"+String(i)) ? server.arg("effect"+String(i)) : "solid";
    phraseConfigs[phraseCount].r            = server.hasArg("r"+String(i))      ? (uint8_t)server.arg("r"+String(i)).toInt()   : 255;
    phraseConfigs[phraseCount].g            = server.hasArg("g"+String(i))      ? (uint8_t)server.arg("g"+String(i)).toInt()   : 255;
    phraseConfigs[phraseCount].b            = server.hasArg("b"+String(i))      ? (uint8_t)server.arg("b"+String(i)).toInt()   : 255;
    phraseConfigs[phraseCount].durationSec  = server.hasArg("dur"+String(i))    ? server.arg("dur"+String(i)).toInt()           : 10;
    phraseConfigs[phraseCount].returnEffect = server.hasArg("ret"+String(i))    ? server.arg("ret"+String(i))                   : "gaslight";
    phraseCount++;
  }

  saveConfig();

  if (micEnabled && !micInitDone) micInit();

  server.send(200, "text/plain", "OK");
}

void handlePlayer() {
  server.send_P(200, "text/html", PLAYER_PAGE);
}

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
    String id = server.arg("effect");
    const EffectDef* e = findEffect(id);
    if (e) {
      currentEffect = id;
      lastUpdate    = 0;
      e->onActivate();
    }
  }
  server.send(200, "text/plain", currentEffect);
}

// Shared param logic — returns false if name is unrecognised
bool applyParam(const String& name, int value) {
  if      (name == "chaseSpokes")          { chaseSpokes = constrain(value, 1, NUM_LEDS); chaseLitCount = 1; chaseRevStep = 0; }
  else if (name == "chaseSpeed")           chaseSpeed           = constrain(value, 10, 500);
  else if (name == "loaderSpeed")          loaderSpeed          = constrain(value, 20, 1000);
  else if (name == "loaderFlashMs")        loaderFlashMs        = constrain(value, 50, 2000);
  else if (name == "loaderFadeSec")        loaderFadeSec        = constrain(value, 1, 300);
  else if (name == "gasTargetSeconds")     gasTargetSeconds     = max(1, value);
  else if (name == "gasTotalCycles")       gasTotalCycles       = max(1, value);
  else if (name == "gasDescentCycles")     gasDescentCycles     = constrain(value, 1, gasTotalCycles);
  else if (name == "gasMinBrightness")     gasMinBrightness     = constrain(value, 0, 255);
  else if (name == "gasMaxBrightness")     gasMaxBrightness     = constrain(value, 0, 255);
  else if (name == "gasDeepBrightnessMin") gasDeepBrightnessMin = constrain(value, 0, 255);
  else if (name == "gasDeepBrightnessMax") gasDeepBrightnessMax = constrain(value, 0, 255);
  else if (name == "gasVariation")         gasVariation         = constrain(value, 0, 50);
  else if (name == "gasWarnProbability")   gasWarnProbability   = constrain(value, 0, 100);
  else return false;
  return true;
}

static unsigned long lastSetParamMs = 0;

// Single-param endpoint (rate-limited)
void handleSetParam() {
  unsigned long now = millis();
  if (now - lastSetParamMs < 50) { server.send(429, "text/plain", "Rate limit"); return; }
  lastSetParamMs = now;

  if (!server.hasArg("name") || !server.hasArg("value")) {
    server.send(400, "text/plain", "Missing name/value");
    return;
  }
  if (!applyParam(server.arg("name"), server.arg("value").toInt())) {
    server.send(400, "text/plain", "Unknown param");
    return;
  }
  server.send(200, "text/plain", "OK");
}

// Multi-param atomic endpoint — apply all args in one request
void handleSetParams() {
  for (int i = 0; i < server.args(); i++)
    applyParam(server.argName(i), server.arg(i).toInt());
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
    gasCycleStart   = millis();
  } else if (action == "pause") {
    gasCycleRunning = false;
  } else if (action == "reset") {
    gasCycleCounter    = 1;
    gasCycleRunning    = true;
    gasCycleStart      = millis();
    gasWavePhase       = 0.0f;
    gasLastWaveMs      = 0;
    gasTransientActive = false;
  }

  server.send(200, "text/plain", action);
}

void handleGetStatus() {
  unsigned long elapsed = (millis() - gasCycleStart) / 1000UL;

  String json = "{";
  json += "\"power\":" + String(ledPower ? "true" : "false") + ",";
  json += "\"effect\":\"" + jsonStr(currentEffect) + "\",";
  json += "\"brightness\":" + String(brightness) + ",";
  json += "\"color\":\"" + colorToHex(currentColor) + "\",";
  json += "\"chaseSpokes\":"  + String(chaseSpokes)  + ",";
  json += "\"chaseSpeed\":"   + String(chaseSpeed)   + ",";
  json += "\"loaderSpeed\":"  + String(loaderSpeed)  + ",";
  json += "\"loaderFlashMs\":" + String(loaderFlashMs) + ",";
  json += "\"loaderFadeSec\":" + String(loaderFadeSec) + ",";
  json += "\"loaderPhase\":"  + String(loaderPhase)  + ",";
  json += "\"cycle\":" + String(gasCycleCounter) + ",";
  json += "\"totalCycles\":" + String(gasTotalCycles) + ",";
  json += "\"elapsed\":" + String(elapsed) + ",";
  json += "\"targetSeconds\":" + String(gasTargetSeconds) + ",";
  json += "\"running\":" + String(gasCycleRunning ? "true" : "false") + ",";
  json += "\"minBrightness\":"     + String(gasMinBrightness)     + ",";
  json += "\"maxBrightness\":"     + String(gasMaxBrightness)     + ",";
  json += "\"deepBrightnessMin\":" + String(gasDeepBrightnessMin) + ",";
  json += "\"deepBrightnessMax\":" + String(gasDeepBrightnessMax) + ",";
  json += "\"descentCycles\":"     + String(gasDescentCycles)     + ",";
  json += "\"variation\":"         + String(gasVariation)         + ",";
  json += "\"warnProbability\":"   + String(gasWarnProbability)   + ",";
  json += "\"descentStart\":"      + String(max(1, gasTotalCycles - gasDescentCycles + 1));
  json += "}";

  server.send(200, "application/json", json);
}

// ── Setup / Loop ──────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);

  loadConfig();

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

  server.on("/",           handleRoot);
  server.on("/player",     handlePlayer);
  server.on("/ping",       handlePing);
  server.on("/power",      handlePower);
  server.on("/color",      handleColor);
  server.on("/effect",     handleEffect);
  server.on("/setParam",   handleSetParam);
  server.on("/setParams",  handleSetParams);
  server.on("/gasControl", handleGasControl);
  server.on("/getStatus",  handleGetStatus);
  server.on("/config",     handleConfig);
  server.on("/getConfig",  handleGetConfig);
  server.on("/saveConfig", HTTP_POST, handleSaveConfig);

  server.begin();
  Serial.println("Web server started.");

  if (micEnabled) micInit();

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
  checkTriggerExpiry();
  handleMicLoop();
  applyEffect();
}