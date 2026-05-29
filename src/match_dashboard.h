#pragma once

static const char MATCH_DASHBOARD_HTML[] = R"rawliteral(
<!DOCTYPE html><html><head>
<title>gol-cam MATCH</title>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
--bg:#0a0a0a;--card:#161616;--border:#262626;--muted:#888;
--accent:#0ff;--green:#0a0;--orange:#f90;--blue:#36c;--red:#c00;--yellow:#ff0
}
body{background:var(--bg);color:#fff;font-family:system-ui,-apple-system,sans-serif;
min-height:100vh;display:flex;flex-direction:column;align-items:center;padding-bottom:24px}
header{display:flex;align-items:center;gap:12px;padding:10px 16px;width:100%;
max-width:920px;border-bottom:1px solid var(--border)}
header .back{color:var(--accent);text-decoration:none;font-size:0.9em}
header h1{margin:0;font-size:1.25em;flex:1;text-align:center;letter-spacing:0.5px}

#config{background:var(--card);border:1px solid var(--border);border-radius:12px;
padding:24px;margin:24px 12px;width:100%;max-width:420px;text-align:center}
#config h2{font-size:1.1em;margin-bottom:14px;color:var(--accent)}
.cfg-row{display:flex;align-items:center;justify-content:space-between;
gap:8px;margin:10px 0}
.cfg-row label{color:var(--muted);font-size:0.85em;flex:0 0 60px;text-align:left}
.cfg-row input{flex:1;background:#0d0d0d;color:#fff;border:1px solid #333;
border-radius:6px;padding:8px 12px;font-size:0.95em;font-family:ui-monospace,monospace}
#cfg-msg{color:var(--muted);font-size:0.8em;margin-top:8px;min-height:1em}

.btn{padding:12px 22px;font-size:0.95em;border:none;border-radius:10px;
cursor:pointer;font-weight:700;transition:transform 0.1s,filter 0.15s}
.btn:active{transform:scale(0.96)}
.btn:disabled{filter:grayscale(0.7) brightness(0.7);cursor:default}
.btn-go{background:var(--green);color:#fff;width:100%;margin-top:8px}
.btn-cal{background:var(--orange);color:#000}
.btn-tune{background:var(--blue);color:#fff}
.btn-start{background:var(--green);color:#fff}
.btn-pause{background:var(--yellow);color:#000}
.btn-resume{background:var(--green);color:#fff}
.btn-reset{background:#444;color:#fff}
.btn-end{background:var(--red);color:#fff}

#scoreboard{display:none;width:100%;max-width:920px;padding:14px 0;text-align:center}
#score-line{display:inline-flex;align-items:baseline;gap:18px;font-size:4em;font-weight:800;
line-height:1;letter-spacing:1px}
#score-line .x{color:#444;font-size:0.5em;align-self:center}
#score-line span:first-child,#score-line span:last-child{
text-shadow:0 0 18px rgba(0,255,80,0.35);color:var(--green);min-width:1.2em}
.team-labels{display:flex;justify-content:center;gap:60px;font-size:0.8em;
color:var(--muted);letter-spacing:2px;margin-top:4px}

#feeds{display:none;width:100%;max-width:920px;gap:10px;padding:0 10px;
justify-content:center;flex-wrap:wrap}
.feed-box{flex:1 1 380px;max-width:440px;background:var(--card);
border:1px solid var(--border);border-radius:12px;padding:8px}
.feed-head{display:flex;justify-content:space-between;align-items:center;
font-size:0.85em;color:var(--muted);margin-bottom:6px;padding:0 4px}
.feed-status{font-size:0.7em;padding:2px 8px;border-radius:8px;font-weight:600}
.st-online{background:var(--green);color:#fff}
.st-offline{background:var(--red);color:#fff}
.st-idle{background:#2a2a2a;color:#aaa}
#sb-badge{font-size:0.7em;padding:3px 10px;border-radius:10px;font-weight:600;letter-spacing:0.3px;cursor:default}
.sb-on{background:rgba(0,170,0,0.18);color:#0f0;border:1px solid rgba(0,255,0,0.3)}
.sb-off{background:rgba(150,150,150,0.15);color:#666;border:1px solid #333}
.sb-err{background:rgba(200,0,0,0.18);color:#f44;border:1px solid rgba(255,0,0,0.3)}
.cam-wrap{position:relative;width:100%;line-height:0;border-radius:8px;overflow:hidden}
.cam-wrap img{width:100%;display:block}
.cam-wrap svg{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}

.feed-actions{display:flex;gap:6px;margin:8px 0 4px;flex-wrap:wrap;justify-content:center}
.feed-actions .btn{padding:9px 14px;font-size:0.85em}
.cal-msg{font-size:0.75em;padding:6px 10px;margin:4px 0 0;border-radius:6px;text-align:center;display:none;line-height:1.3}
.cal-msg.cal-ok{display:block;background:rgba(0,170,0,0.18);color:#9f9;border-left:3px solid var(--green)}
.cal-msg.cal-fail{display:block;background:rgba(200,0,0,0.18);color:#f99;border-left:3px solid var(--red)}
.cal-msg.cal-running{display:block;background:rgba(255,153,0,0.15);color:#fc6;border-left:3px solid var(--orange)}
.roi-bar{display:flex;align-items:center;justify-content:space-between;
gap:6px;margin-top:6px;font-size:0.72em;color:var(--muted)}
.roi-bar .dpad{display:grid;grid-template-columns:repeat(3,28px);grid-template-rows:repeat(3,24px);gap:2px}
.roi-bar .dpad button{padding:0;background:#222;color:#fff;border:1px solid #333;
border-radius:4px;font-size:0.8em;cursor:pointer}
.roi-bar .dpad button:active{background:#333}
.roi-bar .resize{display:flex;flex-wrap:wrap;gap:3px;align-items:center;justify-content:center}
.roi-bar .resize button{padding:3px 7px;background:#222;color:#fff;
border:1px solid #333;border-radius:4px;cursor:pointer;min-width:24px}
.roi-bar .resize button:active{background:#333}
.roi-bar .info{flex:1;text-align:right;color:var(--accent);font-family:ui-monospace,monospace;font-size:0.7em}

details.expert{margin-top:8px;background:#0e0e0e;border:1px solid var(--border);
border-radius:8px;overflow:hidden}
details>summary{cursor:pointer;padding:8px 12px;font-size:0.78em;font-weight:600;
color:var(--accent);user-select:none;list-style:none}
details>summary::-webkit-details-marker{display:none}
details>summary::before{content:'\25B8 ';transition:transform 0.15s;display:inline-block}
details[open]>summary::before{transform:rotate(90deg)}
.expert-body{padding:6px 12px 12px}
.cam-sliders{display:grid;grid-template-columns:repeat(2,1fr);gap:5px 12px;
font-size:0.72em;color:var(--muted)}
.cam-sliders .group-title{grid-column:1 / -1;color:var(--accent);font-weight:600;
font-size:0.78em;border-bottom:1px solid var(--border);padding-bottom:2px;margin-top:4px}
.cam-sliders label{display:flex;align-items:center;justify-content:space-between;gap:5px}
.cam-sliders input[type=range]{flex:1;min-width:50px;accent-color:var(--accent)}
.cam-sliders input[type=checkbox]{accent-color:var(--accent)}
.cam-sliders .play{cursor:pointer;color:var(--accent);padding-left:4px}

#controls{display:none;width:100%;max-width:920px;padding:14px 12px}
.match-bar{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;
background:var(--card);border:1px solid var(--border);border-radius:12px;padding:12px}

#gol-log{width:100%;max-width:920px;padding:0 12px;display:flex;flex-direction:column;gap:8px}
.gol-entry{background:var(--card);border:1px solid var(--border);border-radius:8px;
padding:8px;display:flex;gap:10px;align-items:center;cursor:pointer}
.gol-entry:active{background:#1a1a1a}
.gol-entry img{width:90px;border-radius:4px;border:2px solid var(--green)}
.gol-entry .gol-info{flex:1;font-size:0.85em}
.gol-entry .gol-num{color:var(--green);font-size:1.15em;font-weight:bold}
.gol-entry .gol-side{font-size:0.78em;color:var(--muted)}
.gol-entry .gol-time{color:var(--muted);font-size:0.7em}
.gol-annulled{opacity:0.4}
.gol-annulled .gol-num{text-decoration:line-through;color:#f44}
.btn-var{background:var(--red);color:#fff;padding:5px 10px;font-size:0.75em;
border:none;border-radius:5px;cursor:pointer;font-weight:bold}

#gol-flash{position:fixed;inset:0;background:rgba(0,255,0,0.3);pointer-events:none;
opacity:0;transition:opacity 0.5s;z-index:50}
#gol-text{position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);
font-size:6em;font-weight:bold;color:#0f0;opacity:0;transition:opacity 0.5s;
pointer-events:none;text-shadow:0 0 30px #0f0;z-index:51}
.active{opacity:1!important}

#lightbox{position:fixed;inset:0;background:rgba(0,0,0,0.92);
display:none;flex-direction:column;justify-content:center;align-items:center;z-index:100;padding:16px}
#lightbox img{max-width:95%;max-height:65%;border:3px solid var(--green);border-radius:8px}
#lb-title{color:#fff;font-size:1.3em;font-weight:bold;margin:10px 0}
#lb-buttons{display:none;gap:14px;margin:12px 0}
.btn-foi{background:var(--green);color:#fff;padding:14px 28px;font-size:1.1em;
border:none;border-radius:10px;cursor:pointer;font-weight:bold}
.btn-anula{background:var(--red);color:#fff;padding:14px 28px;font-size:1.1em;
border:none;border-radius:10px;cursor:pointer;font-weight:bold}

.lang-picker{position:fixed;top:8px;right:8px;display:flex;gap:4px;z-index:90}
.lang-btn{padding:3px 8px;font-size:0.7em;border:1px solid #333;border-radius:4px;
background:#0f0f0f;color:var(--muted);cursor:pointer;font-weight:bold}
.lang-btn.active{background:var(--accent);color:#000;border-color:var(--accent)}
</style></head><body>
<div class='lang-picker'>
<button class='lang-btn' onclick="setLang('en')">EN</button>
<button class='lang-btn' onclick="setLang('pt')">PT</button>
</div>
<div id='gol-flash'></div>
<div id='gol-text' data-i18n='goal.flash'>GOOOL!</div>
<div id='lightbox'>
<div id='lb-title'></div>
<img id='lb-img'/>
<div id='lb-buttons'>
<button class='btn-foi' id='btn-lb-foi' data-i18n='var.confirm'>Foi Gol</button>
<button class='btn-anula' id='btn-lb-anula' data-i18n='var.annul'>Anula</button>
</div>
</div>
<header>
<a class='back' href='/' data-i18n='nav.menu'>← Menu</a>
<h1 data-i18n='match.title'>gol-cam MATCH</h1>
<div id='sb-badge' class='sb-off' title='scoreboard'>📊 …</div>
</header>

<div id='config'>
<h2 data-i18n='match.board_ips'>Board IPs</h2>
<div class='cfg-row'>
<label data-i18n='match.home'>Home</label><input id='ip-home' placeholder='192.168.x.x'>
</div>
<div class='cfg-row'>
<label data-i18n='match.away'>Away</label><input id='ip-away' placeholder='192.168.x.x'>
</div>
<div class='cfg-row'>
<label data-i18n='match.placar'>Placar</label><input id='ip-placar' placeholder='192.168.x.x'>
</div>
<button class='btn btn-go' id='btn-connect' onclick='connect()' data-i18n='match.connect'>Connect</button>
<div id='cfg-msg'></div>
</div>

<div id='scoreboard'>
<div id='score-line'><span id='score-home'>0</span><span class='x'>×</span><span id='score-away'>0</span></div>
<div class='team-labels'><span id='lbl-home' data-i18n='match.home_label'>HOME</span><span id='lbl-away' data-i18n='match.away_label'>AWAY</span></div>
<div id='placar-status'></div>
</div>

<div id='feeds'>
<!-- HOME -->
<div class='feed-box' id='box-home'>
<div class='feed-head'><span data-i18n='match.home_goal'>Home Goal</span><span id='st-home' class='feed-status st-idle'>...</span></div>
<div class='cam-wrap'>
<img id='cam-home'/>
<svg id='ov-home' viewBox='0 0 320 240' preserveAspectRatio='none'>
<rect class='ov-roi' fill='none' stroke='#ffff00' stroke-width='2'/>
<rect class='ov-dice' fill='none' stroke='#32cd32' stroke-width='2' style='display:none'/>
</svg>
</div>
<div class='roi-bar'>
<div class='dpad'>
<div></div><button onclick="moveRoi('home',0,-8)">▲</button><div></div>
<button onclick="moveRoi('home',-8,0)">◀</button>
<button onclick="resetRoi('home')" style='font-size:0.55em'>RST</button>
<button onclick="moveRoi('home',8,0)">▶</button>
<div></div><button onclick="moveRoi('home',0,8)">▼</button><div></div>
</div>
<div class='resize'>
<span>W</span><button onclick="resizeRoi('home',-8,0)">−</button><button onclick="resizeRoi('home',8,0)">+</button>
<span>H</span><button onclick="resizeRoi('home',0,-8)">−</button><button onclick="resizeRoi('home',0,8)">+</button>
</div>
<div class='info' id='roi-home'></div>
</div>
<div class='feed-actions'>
<button class='btn btn-tune' id='btn-tune-home' onclick='autotune("home")' data-i18n='match.tune'>⚙ Auto-Tune</button>
<button class='btn btn-cal' id='btn-cal-home' onclick='calibrate("home")' data-i18n='match.cal'>🎯 Calibrate</button>
</div>
<div id='cal-msg-home' class='cal-msg'></div>
<details class='expert'>
<summary data-i18n='match.expert'>⚙ Expert</summary>
<div class='expert-body'>
<div class='cam-sliders'>
<div class='group-title' data-i18n='match.cam_section'>Camera</div>
<label>Bri<input type='range' min='-2' max='2' value='-1' onchange="camAdj('home','bri',this.value)"></label>
<label>Con<input type='range' min='-2' max='2' value='1' onchange="camAdj('home','con',this.value)"></label>
<label>Sharp<input type='range' min='-2' max='2' value='1' onchange="camAdj('home','sharp',this.value)"></label>
<label>Exp<input type='range' min='0' max='1200' value='150' onchange="camAdj('home','aec',this.value)"></label>
<label>Gain<input type='range' min='0' max='30' value='8' onchange="camAdj('home','gain',this.value)"></label>
<label>GCeil<input type='range' min='0' max='6' value='1' onchange="camAdj('home','gceil',this.value)"></label>
<label>Gamma<input type='checkbox' onclick="camAdj('home','gma',this.checked?1:0)"></label>
<label>Lens<input type='checkbox' onclick="camAdj('home','lenc',this.checked?1:0)"></label>
<div class='group-title' data-i18n='match.audio_section'>Audio</div>
<label>Vol<input type='range' min='0' max='100' value='70' onchange="setVolume('home',this.value)"><span class='play' onclick="testSound('home')">▶</span></label>
<label>LED<input type='checkbox' onclick="setLed('home',this.checked?1:0)"></label>
</div>
</div>
</details>
</div>

<!-- AWAY -->
<div class='feed-box' id='box-away'>
<div class='feed-head'><span data-i18n='match.away_goal'>Away Goal</span><span id='st-away' class='feed-status st-idle'>...</span></div>
<div class='cam-wrap'>
<img id='cam-away'/>
<svg id='ov-away' viewBox='0 0 320 240' preserveAspectRatio='none'>
<rect class='ov-roi' fill='none' stroke='#ffff00' stroke-width='2'/>
<rect class='ov-dice' fill='none' stroke='#32cd32' stroke-width='2' style='display:none'/>
</svg>
</div>
<div class='roi-bar'>
<div class='dpad'>
<div></div><button onclick="moveRoi('away',0,-8)">▲</button><div></div>
<button onclick="moveRoi('away',-8,0)">◀</button>
<button onclick="resetRoi('away')" style='font-size:0.55em'>RST</button>
<button onclick="moveRoi('away',8,0)">▶</button>
<div></div><button onclick="moveRoi('away',0,8)">▼</button><div></div>
</div>
<div class='resize'>
<span>W</span><button onclick="resizeRoi('away',-8,0)">−</button><button onclick="resizeRoi('away',8,0)">+</button>
<span>H</span><button onclick="resizeRoi('away',0,-8)">−</button><button onclick="resizeRoi('away',0,8)">+</button>
</div>
<div class='info' id='roi-away'></div>
</div>
<div class='feed-actions'>
<button class='btn btn-tune' id='btn-tune-away' onclick='autotune("away")' data-i18n='match.tune'>⚙ Auto-Tune</button>
<button class='btn btn-cal' id='btn-cal-away' onclick='calibrate("away")' data-i18n='match.cal'>🎯 Calibrate</button>
</div>
<div id='cal-msg-away' class='cal-msg'></div>
<details class='expert'>
<summary data-i18n='match.expert'>⚙ Expert</summary>
<div class='expert-body'>
<div class='cam-sliders'>
<div class='group-title' data-i18n='match.cam_section'>Camera</div>
<label>Bri<input type='range' min='-2' max='2' value='-1' onchange="camAdj('away','bri',this.value)"></label>
<label>Con<input type='range' min='-2' max='2' value='1' onchange="camAdj('away','con',this.value)"></label>
<label>Sharp<input type='range' min='-2' max='2' value='1' onchange="camAdj('away','sharp',this.value)"></label>
<label>Exp<input type='range' min='0' max='1200' value='150' onchange="camAdj('away','aec',this.value)"></label>
<label>Gain<input type='range' min='0' max='30' value='8' onchange="camAdj('away','gain',this.value)"></label>
<label>GCeil<input type='range' min='0' max='6' value='1' onchange="camAdj('away','gceil',this.value)"></label>
<label>Gamma<input type='checkbox' onclick="camAdj('away','gma',this.checked?1:0)"></label>
<label>Lens<input type='checkbox' onclick="camAdj('away','lenc',this.checked?1:0)"></label>
<div class='group-title' data-i18n='match.audio_section'>Audio</div>
<label>Vol<input type='range' min='0' max='100' value='70' onchange="setVolume('away',this.value)"><span class='play' onclick="testSound('away')">▶</span></label>
<label>LED<input type='checkbox' onclick="setLed('away',this.checked?1:0)"></label>
</div>
</div>
</details>
</div>
</div>

<div id='controls'>
<div class='match-bar'>
<button class='btn btn-start' id='btn-start' onclick='matchCmd("start")' data-i18n='match.start'>▶ Start Match</button>
<button class='btn btn-pause' id='btn-pause' onclick='matchCmd("pause")' style='display:none' data-i18n='match.pause'>Pause</button>
<button class='btn btn-resume' id='btn-resume' onclick='matchCmd("resume")' style='display:none' data-i18n='match.resume'>Resume</button>
<button class='btn btn-reset' id='btn-reset' onclick='matchReset()' data-i18n='match.reset'>Reset</button>
<button class='btn btn-end' id='btn-stop' onclick='matchCmd("stop")' style='display:none' data-i18n='match.end'>End Match</button>
</div>
</div>

<div id='gol-log'></div>

<script>
var I18N={
en:{
'nav.menu':'← Menu',
'goal.flash':'GOOOL!',
'var.title':'VAR - Video Review',
'var.confirm':'Foi Gol',
'var.annul':'Annul',
'var.annulled':'ANNULLED',
'match.title':'gol-cam MATCH',
'match.board_ips':'Board IPs',
'match.home':'Home','match.away':'Away',
'match.connect':'Connect',
'match.enter_both':'Enter both IPs',
'match.home_label':'HOME','match.away_label':'AWAY',
'match.home_goal':'Home Goal','match.away_goal':'Away Goal',
'match.cal':'🎯 Calibrate','match.tune':'⚙ Auto-Tune',
'match.calibrating':'Calibrating...','match.analyzing':'Analyzing frame...',
'match.cal_net_err':'Network error contacting camera','match.cal_read_err':'Calibrated but could not read status',
'match.expert':'⚙ Expert',
'match.cam_section':'Camera','match.audio_section':'Audio',
'match.start':'▶ Start Match','match.pause':'Pause','match.resume':'Resume',
'match.reset':'Reset','match.end':'End Match',
'match.offline':'OFFLINE','match.ready':'READY','match.idle':'IDLE',
'match.cal_state':'CAL...','match.playing':'PLAYING','match.paused':'PAUSED',
'match.goal_num':'GOL #%d (%s)',
'match.scorer_home':'HOME','match.scorer_away':'AWAY',
'match.placar':'Placar','match.placar_ok':'Placar: online (%d × %d)','match.placar_off':'Placar: offline','match.placar_none':''
},
pt:{
'nav.menu':'← Menu',
'goal.flash':'GOOOL!',
'var.title':'VAR - Revisão',
'var.confirm':'Foi Gol',
'var.annul':'Anula',
'var.annulled':'ANULADO',
'match.title':'gol-cam JOGO',
'match.board_ips':'IPs das Placas',
'match.home':'Casa','match.away':'Fora',
'match.connect':'Conectar',
'match.enter_both':'Digite ambos os IPs',
'match.home_label':'CASA','match.away_label':'FORA',
'match.home_goal':'Gol Casa','match.away_goal':'Gol Fora',
'match.cal':'🎯 Calibrar','match.tune':'⚙ Auto-Ajuste',
'match.calibrating':'Calibrando...','match.analyzing':'Analisando frame...',
'match.cal_net_err':'Erro de rede ao contatar a câmera','match.cal_read_err':'Calibrado mas não foi possível ler status',
'match.expert':'⚙ Avançado',
'match.cam_section':'Câmera','match.audio_section':'Áudio',
'match.start':'▶ Iniciar','match.pause':'Pausar','match.resume':'Continuar',
'match.reset':'Reiniciar','match.end':'Encerrar',
'match.offline':'OFFLINE','match.ready':'PRONTO','match.idle':'AGUARDANDO',
'match.cal_state':'CAL...','match.playing':'JOGANDO','match.paused':'PAUSADO',
'match.goal_num':'GOL #%d (%s)',
'match.scorer_home':'CASA','match.scorer_away':'FORA',
'match.placar':'Placar','match.placar_ok':'Placar: online (%d × %d)','match.placar_off':'Placar: offline','match.placar_none':''
}
};
var curLang='en';
function t(key){
var s=(I18N[curLang]&&I18N[curLang][key])||I18N.en[key]||key;
var args=Array.prototype.slice.call(arguments,1);var i=0;
s=s.replace(/%[ds]/g,function(){return i<args.length?args[i++]:'';});
return s;
}
function setLang(lang){
curLang=lang;localStorage.setItem('gol-lang',lang);
document.querySelectorAll('[data-i18n]').forEach(function(el){
el.textContent=t(el.getAttribute('data-i18n'));});
document.querySelectorAll('.lang-btn').forEach(function(b){
b.classList.toggle('active',b.textContent.toLowerCase()===lang);});
}
(function(){
var saved=localStorage.getItem('gol-lang');
if(saved){curLang=saved;}
else{var nav=navigator.language||'';curLang=nav.startsWith('pt')?'pt':'en';}
})();

const $=id=>document.getElementById(id);
let boards={home:{ip:'',online:false,goals:0,goalSeq:0,state:-1,calibrated:false},
            away:{ip:'',online:false,goals:0,goalSeq:0,state:-1,calibrated:false},
            placar:{ip:'',online:false,a:0,b:0}};
let polling=null;
let varEntry=null,varBtn=null,varSide=null;

try{
  const saved=JSON.parse(localStorage.getItem('gol-match'));
  if(saved){$('ip-home').value=saved.home||'';$('ip-away').value=saved.away||'';
            $('ip-placar').value=saved.placar||'';}
}catch(e){}

(async function(){
  try{
    const r=await fetch('/status');const d=await r.json();
    const myIp=location.hostname;
    if(d.role==='home'){$('ip-home').value=myIp;if(d.peer)$('ip-away').value=d.peer;}
    else if(d.role==='away'){$('ip-away').value=myIp;if(d.peer)$('ip-home').value=d.peer;}
    else{if(!$('ip-home').value)$('ip-home').value=myIp;}
  }catch(e){}
})();

function connect(){
  const h=$('ip-home').value.trim(),a=$('ip-away').value.trim(),p=$('ip-placar').value.trim();
  if(!h||!a){$('cfg-msg').textContent=t('match.enter_both');return;}
  boards.home.ip=h;boards.away.ip=a;boards.placar.ip=p;
  localStorage.setItem('gol-match',JSON.stringify({home:h,away:a,placar:p}));
  $('config').style.display='none';
  $('scoreboard').style.display='block';
  $('feeds').style.display='flex';
  $('controls').style.display='block';
  $('cam-home').src='http://'+h+':81/stream';
  $('cam-away').src='http://'+a+':81/stream';
  if(polling)clearInterval(polling);
  polling=setInterval(poll,500);
  poll();
}

async function poll(){
  await Promise.allSettled([pollBoard('home'),pollBoard('away'),pollPlacar()]);
  updateControls();
}

async function pollPlacar(){
  const p=boards.placar;
  if(!p.ip){$('placar-status').textContent=t('match.placar_none');return;}
  try{
    const r=await fetch('http://'+p.ip+'/status',{signal:AbortSignal.timeout(1500)});
    const d=await r.json();
    p.online=true;p.a=d.a||0;p.b=d.b||0;
    $('placar-status').textContent=t('match.placar_ok',p.a,p.b);
  }catch(e){
    p.online=false;
    $('placar-status').textContent=t('match.placar_off');
  }
}

async function pollBoard(side){
  const b=boards[side];
  try{
    const r=await fetch('http://'+b.ip+'/status',{signal:AbortSignal.timeout(2000)});
    const d=await r.json();
    b.online=true;
    b.state=d.state;b.calibrated=d.calibrated;
    const prevGoals=b.goals;b.goals=d.goals;
    if(d.goalSeq>b.goalSeq&&d.scored){
      b.goalSeq=d.goalSeq;
      flashGoal(side,b.goals);
      addGoalEntry(side,b.goals,b.ip);
    }else if(d.goalSeq>b.goalSeq){b.goalSeq=d.goalSeq;}
    const el=$('st-'+side);
    if(d.state===2){el.textContent=t('match.playing');el.className='feed-status st-online';}
    else if(d.state===3){el.textContent=t('match.paused');el.className='feed-status st-online';}
    else if(d.state===0){el.textContent=d.calibrated?t('match.ready'):t('match.idle');el.className='feed-status st-idle';}
    else{el.textContent=t('match.cal_state');el.className='feed-status st-idle';}
    var ri=$(('roi-'+side));
    if(ri&&d.roiW!==undefined)ri.textContent=d.roiW+'×'+d.roiH+' @'+d.roiX+','+d.roiY;
    if(d.scoreboardIp&&d.scoreboardIp!==scoreboardIp){scoreboardIp=d.scoreboardIp;pollScoreboard();}
    var ov=$(('ov-'+side));
    if(ov&&d.roiW!==undefined){
      var roi=ov.querySelector('.ov-roi');
      var rx=160-d.roiW/2+(d.roiX||0), ry=120-d.roiH/2+(d.roiY||0);
      roi.setAttribute('x',rx);roi.setAttribute('y',ry);
      roi.setAttribute('width',d.roiW);roi.setAttribute('height',d.roiH);
      var dice=ov.querySelector('.ov-dice');
      if(d.diceX!==undefined&&d.diceX>=0){
        dice.setAttribute('x',d.diceX);dice.setAttribute('y',d.diceY);
        dice.setAttribute('width',d.diceW);dice.setAttribute('height',d.diceH);
        dice.style.display='';
      }else{dice.style.display='none';}
    }
  }catch(e){
    b.online=false;
    $('st-'+side).textContent=t('match.offline');
    $('st-'+side).className='feed-status st-offline';
  }
}

function updateControls(){
  $('score-home').textContent=boards.away.goals;
  $('score-away').textContent=boards.home.goals;
  const anyPlaying=boards.home.state===2||boards.away.state===2;
  const anyPaused=boards.home.state===3||boards.away.state===3;
  const inGame=anyPlaying||anyPaused;
  const anyCalibrated=boards.home.calibrated||boards.away.calibrated;
  $('btn-start').style.display=(!inGame&&anyCalibrated)?'':'none';
  $('btn-pause').style.display=anyPlaying?'':'none';
  $('btn-resume').style.display=(anyPaused&&!anyPlaying)?'':'none';
  $('btn-stop').style.display=inGame?'':'none';
}

function flashGoal(side){
  $('gol-flash').classList.add('active');$('gol-text').classList.add('active');
  setTimeout(()=>{$('gol-flash').classList.remove('active');
  $('gol-text').classList.remove('active');},2000);
}

function addGoalEntry(side,goalNum,ip){
  const glog=$('gol-log');
  const scorerKey=side==='home'?'match.scorer_away':'match.scorer_home';
  const scorer=t(scorerKey);
  const totalHome=boards.away.goals,totalAway=boards.home.goals;
  const snapUrl='http://'+ip+'/goal-snapshot?t='+Date.now();
  const e=document.createElement('div');e.className='gol-entry';
  e.onclick=function(){showLightbox(this.querySelector('img').src);};
  const img=document.createElement('img');img.src=snapUrl;
  const info=document.createElement('div');info.className='gol-info';
  const num=document.createElement('div');num.className='gol-num';
  num.textContent=t('match.goal_num',goalNum,scorer);
  const sd=document.createElement('div');sd.className='gol-side';
  sd.textContent=totalHome+' × '+totalAway;
  const tm=document.createElement('div');tm.className='gol-time';
  tm.textContent=new Date().toLocaleTimeString();
  info.appendChild(num);info.appendChild(sd);info.appendChild(tm);
  const vb=document.createElement('button');vb.className='btn-var';
  vb.textContent='VAR';
  vb.onclick=function(ev){ev.stopPropagation();
    if(this.disabled)return;
    showVAR(img.src,e,this,side);return false;};
  e.appendChild(img);e.appendChild(info);e.appendChild(vb);
  glog.insertBefore(e,glog.firstChild);
}

function showLightbox(src){
  varEntry=null;varBtn=null;varSide=null;
  $('lb-img').src=src;$('lb-title').textContent='';
  $('lb-buttons').style.display='none';
  $('lightbox').style.display='flex';
  $('lightbox').onclick=function(){this.style.display='none';};
}
function showVAR(src,entry,btn,side){
  varEntry=entry;varBtn=btn;varSide=side;
  $('lb-img').src=src;$('lb-title').textContent=t('var.title');
  $('lb-buttons').style.display='flex';
  $('lightbox').style.display='flex';
  $('lightbox').onclick=null;
}
$('btn-lb-foi').onclick=function(){$('lightbox').style.display='none';};
$('btn-lb-anula').onclick=function(){
  if(!varEntry||!varSide)return;
  const ip=boards[varSide].ip;
  fetch('http://'+ip+'/deduct').then(()=>{
    varEntry.classList.add('gol-annulled');
    if(varBtn){varBtn.textContent=t('var.annulled');varBtn.disabled=true;
    varBtn.style.background='#555';}
  });
  $('lightbox').style.display='none';
};

async function camAdj(side,param,val){
  const ip=boards[side].ip;if(!ip)return;
  await fetch('http://'+ip+'/cam?'+param+'='+val);}
async function setVolume(side,val){
  const ip=boards[side].ip;if(!ip)return;
  await fetch('http://'+ip+'/volume?val='+val);}
async function setLed(side,val){
  const ip=boards[side].ip;if(!ip)return;
  await fetch('http://'+ip+'/led?val='+val);}
async function testSound(side){
  const ip=boards[side].ip;if(!ip)return;
  await fetch('http://'+ip+'/test-sound');}
async function moveRoi(side,dx,dy){
  const ip=boards[side].ip;if(!ip)return;
  await fetch('http://'+ip+'/roi?dx='+dx+'&dy='+dy);}
async function resizeRoi(side,dw,dh){
  const ip=boards[side].ip;if(!ip)return;
  await fetch('http://'+ip+'/roi?dw='+dw+'&dh='+dh);}
async function resetRoi(side){
  const ip=boards[side].ip;if(!ip)return;
  await fetch('http://'+ip+'/roi?x=0&y=0');}
async function calibrate(side){
  const ip=boards[side].ip;if(!ip)return;
  const btn=$('btn-cal-'+side), msg=$('cal-msg-'+side);
  const origLabel=btn?btn.textContent:'';
  if(btn){btn.disabled=true;btn.textContent=t('match.calibrating');}
  if(msg){msg.className='cal-msg cal-running';msg.textContent=t('match.analyzing');}
  try{await fetch('http://'+ip+'/calibrate');}catch(e){
    if(msg){msg.className='cal-msg cal-fail';msg.textContent=t('match.cal_net_err');}
    if(btn){btn.disabled=false;btn.textContent=origLabel;}
    return;}
  // Calibration runs server-side over ~6 frames + LED flash → ~1.5-2 s.
  // Then read /status for the verdict.
  setTimeout(async()=>{
    try{
      const r=await fetch('http://'+ip+'/status');
      const d=await r.json();
      if(msg){
        msg.className='cal-msg '+(d.calibrated?'cal-ok':'cal-fail');
        msg.textContent=d.calMsg||(d.calibrated?'OK':'FAILED');}
    }catch(e){
      if(msg){msg.className='cal-msg cal-fail';msg.textContent=t('match.cal_read_err');}
    }
    if(btn){btn.disabled=false;btn.textContent=origLabel;}
  },2000);}

function setMatchSlider(s,v){
  if(v===undefined||v===null||Number.isNaN(v))return;
  s.value=v;s.setAttribute('value',v);}

function syncMatchSliders(panel,d,useCur){
  if(!panel)return;
  const g=useCur?d.curGain:d.autoGain, gc=useCur?d.curGceil:d.autoGceil;
  const ae=useCur?d.curAec:d.autoAec, c=useCur?d.curCon:d.autoCon;
  const b=useCur?d.curBri:d.autoBri, sh=useCur?d.curSharp:d.autoSharp;
  const gm=useCur?d.curGma:d.autoGma, le=useCur?d.curLenc:d.autoLenc;
  panel.querySelectorAll('input[type=range]').forEach(s=>{
    const oc=s.getAttribute('onchange')||'';
    if(oc.includes("'aec'"))setMatchSlider(s,ae);
    else if(oc.includes("'gain'"))setMatchSlider(s,g);
    else if(oc.includes("'gceil'"))setMatchSlider(s,gc);
    else if(oc.includes("'con'"))setMatchSlider(s,c);
    else if(oc.includes("'bri'"))setMatchSlider(s,b);
    else if(oc.includes("'sharp'"))setMatchSlider(s,sh);});
  panel.querySelectorAll('input[type=checkbox]').forEach(cb=>{
    const oc=cb.getAttribute('onclick')||'';
    if(oc.includes("'gma'")&&gm!==undefined)cb.checked=!!gm;
    else if(oc.includes("'lenc'")&&le!==undefined)cb.checked=!!le;});
}

async function autotune(side){
  const ip=boards[side].ip;if(!ip)return;
  await fetch('http://'+ip+'/autotune');
  const panel=$('box-'+side);
  const tick=setInterval(async()=>{
    try{const r=await fetch('http://'+ip+'/status');const d=await r.json();
      if(d.autoDone===1){
        clearInterval(tick);
        syncMatchSliders(panel,d,false);
      }else if(d.autoStage>0){
        syncMatchSliders(panel,d,true);
      }
    }catch(e){}},250);
}

async function matchCmd(cmd){
  await Promise.allSettled([
    fetch('http://'+boards.home.ip+'/'+cmd).catch(()=>{}),
    fetch('http://'+boards.away.ip+'/'+cmd).catch(()=>{})
  ]);
}

function matchReset(){
  Promise.allSettled([
    fetch('http://'+boards.home.ip+'/reset').catch(()=>{}),
    fetch('http://'+boards.away.ip+'/reset').catch(()=>{})
  ]);
  if(scoreboardIp)fetch('http://'+scoreboardIp+'/api/reset').catch(()=>{});
  boards.home.goals=0;boards.home.goalSeq=0;boards.home.state=-1;
  boards.away.goals=0;boards.away.goalSeq=0;boards.away.state=-1;
  $('score-home').textContent='0';$('score-away').textContent='0';
  $('gol-log').innerHTML='';
}

let scoreboardIp='';
async function pollScoreboard(){
  if(!scoreboardIp){$('sb-badge').textContent='📊 –';$('sb-badge').className='sb-off';return;}
  try{
    const r=await fetch('http://'+scoreboardIp+'/status',{signal:AbortSignal.timeout(2500)});
    const d=await r.json();
    $('sb-badge').textContent='📊 '+d.a+'×'+d.b;
    $('sb-badge').className='sb-on';
    $('sb-badge').title='scoreboard '+scoreboardIp+' — A:'+d.a+' B:'+d.b;
  }catch(e){
    $('sb-badge').textContent='📊 ⚠';
    $('sb-badge').className='sb-err';
    $('sb-badge').title='scoreboard unreachable: '+scoreboardIp;
  }
}
setInterval(pollScoreboard,2000);

document.addEventListener('DOMContentLoaded',function(){setLang(curLang);});
</script></body></html>
)rawliteral";
