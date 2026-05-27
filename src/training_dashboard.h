#pragma once

static const char TRAINING_DASHBOARD_HTML[] = R"rawliteral(
<!DOCTYPE html><html><head>
<title>gol-cam</title>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
*{box-sizing:border-box}
:root{
--bg:#0a0a0a;--card:#161616;--border:#262626;--muted:#888;
--accent:#0ff;--green:#0a0;--orange:#f90;--blue:#36c;--red:#c00;--yellow:#ff0
}
body{background:var(--bg);color:#fff;font-family:system-ui,-apple-system,sans-serif;
margin:0;padding:0;min-height:100vh;display:flex;flex-direction:column;align-items:center}
header{display:flex;align-items:center;gap:12px;padding:10px 16px;width:100%;
max-width:540px;border-bottom:1px solid var(--border)}
header .back{color:var(--accent);text-decoration:none;font-size:0.9em;flex:0 0 auto}
header h1{margin:0;font-size:1.25em;flex:1;text-align:center;letter-spacing:0.5px}
#state-badge{font-size:0.7em;padding:3px 10px;border-radius:10px;font-weight:600;letter-spacing:0.5px}
#sb-badge{font-size:0.65em;padding:3px 8px;border-radius:10px;font-weight:600;letter-spacing:0.3px;cursor:default}
.sb-on{background:rgba(0,170,0,0.18);color:#0f0;border:1px solid rgba(0,255,0,0.3)}
.sb-off{background:rgba(150,150,150,0.15);color:#666;border:1px solid #333}
.sb-err{background:rgba(200,0,0,0.18);color:#f44;border:1px solid rgba(255,0,0,0.3)}
.s-idle{background:#2a2a2a;color:#aaa}
.s-cal{background:var(--orange);color:#000}
.s-play{background:var(--green);color:#fff}
.s-pause{background:var(--yellow);color:#000}
main{width:100%;max-width:540px;padding:0 12px 24px;display:flex;flex-direction:column;align-items:center;gap:10px}
#score{font-size:4.5em;font-weight:800;line-height:1;color:var(--green);
text-shadow:0 0 20px rgba(0,255,80,0.4);margin:8px 0}
.cam-card{width:100%;background:var(--card);border:1px solid var(--border);
border-radius:12px;padding:8px;margin-top:10px}
.cam-wrap{position:relative;width:100%;line-height:0;border-radius:8px;overflow:hidden}
.cam-wrap img{width:100%;display:block}
.cam-wrap svg{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}
.roi-bar{display:flex;align-items:center;justify-content:space-between;
gap:10px;margin-top:8px;font-size:0.78em;color:var(--muted)}
.roi-bar .dpad{display:grid;grid-template-columns:repeat(3,32px);grid-template-rows:repeat(3,28px);gap:2px}
.roi-bar .dpad button{padding:0;background:#222;color:#fff;border:1px solid #333;
border-radius:4px;font-size:0.85em;cursor:pointer}
.roi-bar .dpad button:active{background:#333}
.roi-bar .resize{display:flex;flex-wrap:wrap;gap:4px;align-items:center;justify-content:center;font-size:0.78em}
.roi-bar .resize button{padding:4px 8px;background:#222;color:#fff;border:1px solid #333;border-radius:4px;cursor:pointer;min-width:28px}
.roi-bar .resize button:active{background:#333}
.roi-bar .info{flex:1;text-align:right;color:var(--accent);font-size:0.72em;font-family:ui-monospace,monospace}
.actions{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;width:100%;margin-top:6px}
.btn{padding:14px 22px;font-size:1em;border:none;border-radius:10px;
cursor:pointer;font-weight:700;transition:transform 0.1s,filter 0.15s}
.btn:active{transform:scale(0.96)}
.btn:disabled{filter:grayscale(0.7) brightness(0.7);cursor:default}
.btn-cal{background:var(--orange);color:#000}
.btn-tune{background:var(--blue);color:#fff}
.btn-start{background:var(--green);color:#fff;font-size:1.1em;padding:16px 28px}
.btn-pause{background:var(--yellow);color:#000}
.btn-resume{background:var(--green);color:#fff}
.btn-reset{background:#444;color:#fff}
.btn-end{background:var(--red);color:#fff}
#game-bar{display:none;width:100%;background:var(--card);border:1px solid var(--border);
border-radius:10px;padding:10px}
#game-bar .bar-row{display:flex;flex-wrap:wrap;gap:6px;justify-content:center}
#game-bar .btn{padding:10px 16px;font-size:0.9em}
#countdown{display:none;text-align:center;width:100%}
#cd-bar{width:100%;height:5px;background:#222;border-radius:3px;overflow:hidden;margin:4px 0}
#cd-fill{height:100%;background:var(--green);transition:width 0.4s linear;width:100%}
#cd-text{color:var(--green);font-size:0.82em;font-weight:600}
#cal-feedback{width:100%;font-size:0.85em;padding:10px 14px;background:var(--card);
border:1px solid var(--border);border-radius:8px;display:none;text-align:center}
.cal-ok{border-left:3px solid var(--green) !important}
.cal-fail{border-left:3px solid var(--red) !important}
#cal-snap{max-width:100%;border:2px solid var(--orange);border-radius:8px;display:none}
#cal-info{color:var(--orange);font-size:0.8em;display:none;text-align:center}
#info{color:var(--muted);font-size:0.85em;text-align:center}
details.expert,details.logs{width:100%;background:var(--card);border:1px solid var(--border);
border-radius:10px;margin-top:6px;overflow:hidden}
details>summary{cursor:pointer;padding:10px 14px;font-size:0.85em;font-weight:600;
color:var(--accent);user-select:none;list-style:none}
details>summary::-webkit-details-marker{display:none}
details>summary::before{content:'\25B8 ';display:inline-block;transition:transform 0.15s}
details[open]>summary::before{transform:rotate(90deg)}
.expert-body{padding:8px 14px 14px;display:flex;flex-direction:column;gap:14px}
.cam-sliders{display:grid;grid-template-columns:repeat(2,1fr);gap:6px 16px;font-size:0.78em;color:var(--muted)}
.cam-sliders .group-title{grid-column:1 / -1;color:var(--accent);font-weight:600;
font-size:0.82em;border-bottom:1px solid var(--border);padding-bottom:3px;margin-top:4px}
.cam-sliders label{display:flex;align-items:center;justify-content:space-between;gap:6px}
.cam-sliders input[type=range]{flex:1;min-width:60px;accent-color:var(--accent)}
.cam-sliders input[type=checkbox]{accent-color:var(--accent)}
.cam-sliders .play{cursor:pointer;color:var(--accent);padding-left:4px}
.log-box{padding:6px 12px 12px}
#gol-log{display:flex;flex-direction:column;gap:6px}
.gol-entry{background:#0e0e0e;border:1px solid var(--border);border-radius:8px;
padding:8px;display:flex;gap:8px;align-items:center;cursor:pointer}
.gol-entry:active{background:#1a1a1a}
.gol-entry img{width:90px;border-radius:4px;border:2px solid var(--green)}
.gol-entry .gol-info{flex:1;font-size:0.82em}
.gol-entry .gol-num{color:var(--green);font-size:1.2em;font-weight:bold}
.gol-entry .gol-time{color:var(--muted);font-size:0.72em}
.btn-var{background:var(--red);color:#fff;padding:5px 10px;font-size:0.75em;
border:none;border-radius:5px;cursor:pointer;font-weight:bold}
.gol-annulled{opacity:0.4}
.gol-annulled .gol-num{text-decoration:line-through;color:#f44}
#console{height:140px;background:#040404;border:1px solid var(--border);border-radius:6px;
padding:6px;font-family:ui-monospace,monospace;font-size:0.7em;color:#0f0;overflow-y:auto;line-height:1.4}
.log-reject{color:#f44}.log-dice{color:#0f0}.log-cal{color:var(--orange)}
#gol-flash{position:fixed;top:0;left:0;width:100%;height:100%;
background:rgba(0,255,0,0.3);pointer-events:none;opacity:0;transition:opacity 0.5s;z-index:50}
#gol-text{position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);
font-size:6em;font-weight:bold;color:#0f0;opacity:0;transition:opacity 0.5s;
pointer-events:none;text-shadow:0 0 30px #0f0;z-index:51}
.active{opacity:1 !important}
#lightbox{position:fixed;inset:0;background:rgba(0,0,0,0.92);
display:none;flex-direction:column;justify-content:center;align-items:center;z-index:100;padding:16px}
#lightbox img{max-width:100%;max-height:60vh;border:3px solid var(--green);border-radius:8px;display:block}
#lb-title{color:#fff;font-size:1.3em;font-weight:bold;margin:10px 0}
#lb-frames{display:flex;flex-wrap:wrap;gap:10px;justify-content:center;align-items:flex-start;width:100%;max-width:920px}
.lb-frame{display:flex;flex-direction:column;align-items:center;gap:4px;max-width:48%;flex:1 1 320px}
.lb-frame .lb-label{color:var(--muted);font-size:0.78em;letter-spacing:1px;text-transform:uppercase}
.lb-frame.trigger .lb-label{color:var(--green)}
.lb-frame.before  .lb-label{color:var(--orange)}
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
<div id='lb-frames'>
<div class='lb-frame before' id='lb-frame-prev'>
<div class='lb-label' data-i18n='var.before'>Before</div>
<img id='lb-img-prev'/>
</div>
<div class='lb-frame trigger'>
<div class='lb-label' data-i18n='var.trigger'>Trigger</div>
<img id='lb-img'/>
</div>
</div>
<div id='lb-buttons'>
<button class='btn-foi' onclick='varConfirm()' data-i18n='var.confirm'>Foi Gol</button>
<button class='btn-anula' onclick='varAnula()' data-i18n='var.annul'>Anula</button>
</div>
</div>
<header>
<a class='back' href='/' data-i18n='nav.menu'>&#8592; Menu</a>
<h1>gol-cam</h1>
<div id='state-badge' class='s-idle' data-i18n='train.idle'>IDLE</div>
<div id='sb-badge' class='sb-off' title='scoreboard'>📊 …</div>
</header>
<main>
<div id='score' style='display:none'>0</div>
<div class='cam-card'>
<div class='cam-wrap'>
<img id='cam'/>
<svg id='cam-overlay' viewBox='0 0 320 240' preserveAspectRatio='none'>
<rect id='ov-roi' x='0' y='0' width='0' height='0' fill='none' stroke='#ffff00' stroke-width='2'/>
<rect id='ov-dice' x='0' y='0' width='0' height='0' fill='none' stroke='#32cd32' stroke-width='2' style='display:none'/>
</svg>
</div>
<div class='roi-bar'>
<div class='dpad'>
<div></div><button onclick="moveRoi(0,-8)">&#9650;</button><div></div>
<button onclick="moveRoi(-8,0)">&#9664;</button>
<button onclick="moveRoi(0,0,true)" style='font-size:0.6em'>RST</button>
<button onclick="moveRoi(8,0)">&#9654;</button>
<div></div><button onclick="moveRoi(0,8)">&#9660;</button><div></div>
</div>
<div class='resize'>
<span>W</span><button onclick="resizeRoi(-8,0)">&minus;</button><button onclick="resizeRoi(8,0)">+</button>
<span>H</span><button onclick="resizeRoi(0,-8)">&minus;</button><button onclick="resizeRoi(0,8)">+</button>
</div>
<div class='info' id='roi-info'></div>
</div>
</div>
<div class='actions' id='idle-controls'>
<button class='btn btn-tune' id='btn-tune' onclick='autotune()' data-i18n='train.autotune'>&#9881; Auto-Tune</button>
<button class='btn btn-cal' id='btn-cal' onclick='calibrate()' data-i18n='train.calibrate'>&#127919; Calibrate</button>
<button class='btn btn-start' id='btn-start' onclick='startGame()' style='display:none' data-i18n='train.start'>&#9654; Start</button>
</div>
<div id='game-bar'>
<div class='bar-row'>
<button class='btn btn-pause' id='btn-pause' onclick='pauseGame()' data-i18n='train.pause'>Pause</button>
<button class='btn btn-resume' id='btn-resume' onclick='resumeGame()' style='display:none' data-i18n='train.resume'>Resume</button>
<button class='btn btn-reset' id='btn-reset' onclick='resetGame()' data-i18n='train.reset'>Reset</button>
<button class='btn btn-end' id='btn-stop' onclick='stopGame()' data-i18n='train.end'>End</button>
</div>
</div>
<div id='countdown'>
<div id='cd-bar'><div id='cd-fill'></div></div>
<div id='cd-text'></div>
</div>
<div id='cal-feedback'></div>
<img id='cal-snap'/>
<div id='cal-info'></div>
<div id='info' data-i18n='train.hint_calibrate'>Place the dadinho in view, then press Calibrate</div>
<details class='expert'>
<summary data-i18n='train.expert'>&#9881; Expert Settings</summary>
<div class='expert-body'>
<div class='cam-sliders'>
<div class='group-title' data-i18n='train.cam_section'>Camera</div>
<label>Bri<input type='range' min='-2' max='2' value='-1' onchange="camAdj('bri',this.value)"></label>
<label>Con<input type='range' min='-2' max='2' value='1' onchange="camAdj('con',this.value)"></label>
<label>Sharp<input type='range' min='-2' max='2' value='1' onchange="camAdj('sharp',this.value)"></label>
<label>Exp<input type='range' min='0' max='1200' value='150' onchange="camAdj('aec',this.value)"></label>
<label>Gain<input type='range' min='0' max='30' value='8' onchange="camAdj('gain',this.value)"></label>
<label>GCeil<input type='range' min='0' max='6' value='1' onchange="camAdj('gceil',this.value)"></label>
<label>Gamma<input type='checkbox' onclick="camAdj('gma',this.checked?1:0)"></label>
<label>Lens<input type='checkbox' onclick="camAdj('lenc',this.checked?1:0)"></label>
<div class='group-title' data-i18n='train.post_section'>Post-process</div>
<label style='grid-column:1 / -1'>B&amp;W<input type='range' min='0' max='255' value='30' onchange="fetch('/threshold?val='+this.value)"></label>
<div class='group-title' data-i18n='train.audio_section'>Audio</div>
<label>Vol<input type='range' min='0' max='100' value='70' onchange="fetch('/volume?val='+this.value)"><span class='play' onclick="fetch('/test-sound')">&#9654;</span></label>
<label>LED<input type='checkbox' onclick="fetch('/led?val='+(this.checked?1:0))"></label>
<div class='group-title' data-i18n='train.scoreboard_section'>Scoreboard</div>
<label style='grid-column:1 / -1;justify-content:flex-start;gap:8px'>
<button onclick="fetch('/test-goal')" style='padding:5px 10px;background:#36c;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:0.85em'>↗ Test Push</button>
<button onclick="resetScoreboard()" style='padding:5px 10px;background:#444;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:0.85em'>Reset Placar</button>
<span id='sb-info' style='font-family:ui-monospace,monospace;color:var(--accent);font-size:0.75em'></span>
</label>
</div>
</div>
</details>
<details class='logs'>
<summary data-i18n='train.goals_section'>Goals</summary>
<div class='log-box'><div id='gol-log'></div></div>
</details>
<details class='logs'>
<summary data-i18n='train.console_section'>Console</summary>
<div class='log-box'><div id='console'></div></div>
</details>
</main>
<script>
var I18N={
en:{
'nav.menu':'← Menu',
'goal.flash':'GOOOL!',
'var.title':'VAR - Video Review',
'var.confirm':'Foi Gol',
'var.annul':'Annul',
'var.annulled':'ANNULLED',
'var.before':'Before',
'var.trigger':'Trigger',
'train.calibrate':'\u{1F3AF} Calibrate',
'train.calibrating':'Calibrating...',
'train.analyzing':'Analyzing frame...',
'train.autotune':'⚙ Auto-Tune',
'train.autotuning':'Auto-tuning contrast...',
'train.start':'▶ Start',
'train.pause':'Pause',
'train.resume':'Resume',
'train.reset':'Reset',
'train.end':'End',
'train.idle':'IDLE',
'train.cal_badge':'CALIBRATING',
'train.playing':'PLAYING',
'train.paused':'PAUSED',
'train.hint_calibrate':'Place the dadinho in view, then press Calibrate',
'train.hint_calibrated':'Calibrated! Press Start',
'train.detecting_in':'Detecting in %ds...',
'train.disconnected':'disconnected',
'train.gol_num':'GOL #%d',
'train.expert':'⚙ Expert Settings',
'train.cam_section':'Camera',
'train.post_section':'Post-process',
'train.audio_section':'Audio','train.scoreboard_section':'Scoreboard',
'train.goals_section':'Goals',
'train.console_section':'Console'
},
pt:{
'nav.menu':'← Menu',
'goal.flash':'GOOOL!',
'var.title':'VAR - Revisão',
'var.confirm':'Foi Gol',
'var.annul':'Anula',
'var.annulled':'ANULADO',
'var.before':'Antes',
'var.trigger':'Disparo',
'train.calibrate':'\u{1F3AF} Calibrar',
'train.calibrating':'Calibrando...',
'train.analyzing':'Analisando frame...',
'train.autotune':'⚙ Auto-Ajuste',
'train.autotuning':'Ajustando contraste...',
'train.start':'▶ Iniciar',
'train.pause':'Pausar',
'train.resume':'Continuar',
'train.reset':'Reiniciar',
'train.end':'Encerrar',
'train.idle':'AGUARDANDO',
'train.cal_badge':'CALIBRANDO',
'train.playing':'JOGANDO',
'train.paused':'PAUSADO',
'train.hint_calibrate':'Coloque o dadinho na câmera e pressione Calibrar',
'train.hint_calibrated':'Calibrado! Pressione Iniciar',
'train.detecting_in':'Detectando em %ds...',
'train.disconnected':'desconectado',
'train.gol_num':'GOL #%d',
'train.expert':'⚙ Avançado',
'train.cam_section':'Câmera',
'train.post_section':'Pós-processo',
'train.audio_section':'Áudio','train.scoreboard_section':'Placar',
'train.goals_section':'Gols',
'train.console_section':'Console'
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

document.getElementById('cam').src='http://'+location.hostname+':81/stream';
const $=id=>document.getElementById(id);
let lastState=-1,lastGolSeq=0;
const con=$('console'),glog=$('gol-log');
function log(msg,cls){
const d=document.createElement('div');
if(cls)d.className=cls;
d.textContent=msg;
con.appendChild(d);
if(con.children.length>100)con.removeChild(con.firstChild);
con.scrollTop=con.scrollHeight;}
let varEntry=null,varBtn=null;
function setLbImages(triggerSrc,prevSrc){
$('lb-img').src=triggerSrc||'';
const pf=$('lb-frame-prev');
if(prevSrc){$('lb-img-prev').src=prevSrc;pf.style.display='';}
else{$('lb-img-prev').removeAttribute('src');pf.style.display='none';}}
function showLightbox(triggerSrc,prevSrc){
varEntry=null;varBtn=null;
setLbImages(triggerSrc,prevSrc);
$('lb-title').textContent='';
$('lb-buttons').style.display='none';
$('lightbox').style.display='flex';
$('lightbox').onclick=function(){this.style.display='none';};}
function showVAR(triggerSrc,prevSrc,entry,btn){
varEntry=entry;varBtn=btn;
setLbImages(triggerSrc,prevSrc);
$('lb-title').textContent=t('var.title');
$('lb-buttons').style.display='flex';
$('lightbox').style.display='flex';
$('lightbox').onclick=null;}
function varConfirm(){$('lightbox').style.display='none';}
function varAnula(){
if(!varEntry)return;
fetch('/deduct').then(()=>{
varEntry.classList.add('gol-annulled');
if(varBtn){varBtn.textContent=t('var.annulled');varBtn.disabled=true;
varBtn.style.background='#555';}});
$('lightbox').style.display='none';}

async function camAdj(param,val){await fetch('/cam?'+param+'='+val);}

let scoreboardIp='';
async function pollScoreboard(){
  if(!scoreboardIp){$('sb-badge').textContent='📊 –';$('sb-badge').className='sb-off';return;}
  try{
    const r=await fetch('http://'+scoreboardIp+'/status',{signal:AbortSignal.timeout(2500)});
    const d=await r.json();
    $('sb-badge').textContent='📊 '+d.a+'×'+d.b;
    $('sb-badge').className='sb-on';
    $('sb-badge').title='scoreboard '+scoreboardIp+' — A:'+d.a+' B:'+d.b;
    if($('sb-info'))$('sb-info').textContent=scoreboardIp+'  A:'+d.a+' B:'+d.b;
  }catch(e){
    $('sb-badge').textContent='📊 ⚠';
    $('sb-badge').className='sb-err';
    $('sb-badge').title='scoreboard unreachable: '+scoreboardIp;
  }
}
async function resetScoreboard(){
  if(!scoreboardIp)return;
  try{await fetch('http://'+scoreboardIp+'/api/reset');pollScoreboard();}catch(e){}
}
setInterval(pollScoreboard,2000);
async function moveRoi(dx,dy,reset){
if(reset)await fetch('/roi?x=0&y=0');
else await fetch('/roi?dx='+dx+'&dy='+dy);}
async function resizeRoi(dw,dh){await fetch('/roi?dw='+dw+'&dh='+dh);}

async function calibrate(){
$('btn-cal').textContent=t('train.calibrating');
$('btn-cal').disabled=true;
$('cal-feedback').style.display='block';
$('cal-feedback').textContent=t('train.analyzing');
$('cal-feedback').className='';
await fetch('/calibrate');
setTimeout(async()=>{
try{const r=await fetch('/status');const d=await r.json();
if(d.calMsg){
$('cal-feedback').textContent=d.calMsg;
$('cal-feedback').className=d.calibrated?'cal-ok':'cal-fail';}
if(d.hasSnap){
$('cal-snap').src='/cal-snapshot?t='+Date.now();
$('cal-snap').style.display='block';}
}catch(e){}
$('btn-cal').textContent=t('train.calibrate');
$('btn-cal').disabled=false;},1500);}

function setSlider(s,v){
if(v===undefined||v===null||Number.isNaN(v))return;
s.value=v;s.setAttribute('value',v);}

function syncSliders(d,useCur){
const g=useCur?d.curGain:d.autoGain, gc=useCur?d.curGceil:d.autoGceil;
const ae=useCur?d.curAec:d.autoAec, c=useCur?d.curCon:d.autoCon;
const b=useCur?d.curBri:d.autoBri, sh=useCur?d.curSharp:d.autoSharp;
const gm=useCur?d.curGma:d.autoGma, le=useCur?d.curLenc:d.autoLenc;
document.querySelectorAll('.cam-sliders input[type=range]').forEach(s=>{
const oc=s.getAttribute('onchange')||'';
if(oc.includes("'aec'"))setSlider(s,ae);
else if(oc.includes("'gain'"))setSlider(s,g);
else if(oc.includes("'gceil'"))setSlider(s,gc);
else if(oc.includes("'con'"))setSlider(s,c);
else if(oc.includes("'bri'"))setSlider(s,b);
else if(oc.includes("'sharp'"))setSlider(s,sh);
else if(!useCur&&oc.includes('/threshold'))setSlider(s,d.autoThresh);});
document.querySelectorAll('.cam-sliders input[type=checkbox]').forEach(cb=>{
const oc=cb.getAttribute('onclick')||'';
if(oc.includes("'gma'")&&gm!==undefined)cb.checked=!!gm;
else if(oc.includes("'lenc'")&&le!==undefined)cb.checked=!!le;});}

async function autotune(){
$('btn-tune').disabled=true;$('btn-cal').disabled=true;
$('cal-feedback').style.display='block';
$('cal-feedback').textContent=t('train.autotuning');
$('cal-feedback').className='';
await fetch('/autotune');
const tick=setInterval(async()=>{
try{const r=await fetch('/status');const d=await r.json();
if(d.calMsg)$('cal-feedback').textContent=d.calMsg;
if(d.autoDone===1){
clearInterval(tick);
syncSliders(d,false);
$('cal-feedback').className=d.autoScore>=100?'cal-ok':'cal-fail';
$('btn-tune').disabled=false;$('btn-cal').disabled=false;
}else if(d.autoStage>0){
syncSliders(d,true);
}
}catch(e){}},250);}

async function startGame(){await fetch('/start');
$('cal-snap').style.display='none';$('cal-feedback').style.display='none';
glog.innerHTML='';lastGolSeq=0;}
async function pauseGame(){await fetch('/pause');}
async function resumeGame(){await fetch('/resume');}
async function stopGame(){await fetch('/stop');}
async function resetGame(){await fetch('/reset');
glog.innerHTML='';lastGolSeq=0;lastState=-1;}

function updateUI(st,cal){
const playing=st===2,paused=st===3,inGame=playing||paused,idle=st===0;
$('game-bar').style.display=inGame?'block':'none';
$('btn-pause').style.display=playing?'':'none';
$('btn-resume').style.display=paused?'':'none';
$('idle-controls').style.display=idle?'flex':'none';
$('btn-start').style.display=(idle&&cal)?'':'none';
$('btn-cal').style.display=idle?'':'none';
$('btn-tune').style.display=idle?'':'none';}

setInterval(async()=>{
try{const r=await fetch('/status');const d=await r.json();
if(d.state!==lastState){
lastState=d.state;
const b=$('state-badge');
if(d.state===0){b.textContent=t('train.idle');b.className='s-idle';
$('score').style.display='none';
$('info').textContent=d.calibrated?t('train.hint_calibrated'):t('train.hint_calibrate');}
else if(d.state===1){b.textContent=t('train.cal_badge');b.className='s-cal';}
else if(d.state===2){b.textContent=t('train.playing');b.className='s-play';
$('score').style.display='';}
else if(d.state===3){b.textContent=t('train.paused');b.className='s-pause';
$('score').style.display='';}}
updateUI(d.state,d.calibrated);
if(d.cdRemain>0&&(d.state===2)){
const sec=Math.ceil(d.cdRemain/1000);
$('countdown').style.display='block';
$('cd-text').textContent=t('train.detecting_in',sec);
$('cd-fill').style.width=(d.cdRemain/100)+'%';
}else{$('countdown').style.display='none';}
if(d.calibrated){$('cal-info').style.display='';
$('cal-info').textContent='Dice: '+d.calPx+'px, '+d.calW+'x'+d.calH+
'px (contrast>='+d.calContrast+')';}
if(d.scoreboardIp&&d.scoreboardIp!==scoreboardIp){scoreboardIp=d.scoreboardIp;pollScoreboard();}
if(d.roiW!==undefined){
$('roi-info').textContent=d.roiW+'×'+d.roiH+' @'+d.roiX+','+d.roiY;
const roiX=160-d.roiW/2+(d.roiX||0), roiY=120-d.roiH/2+(d.roiY||0);
const ovR=$('ov-roi');ovR.setAttribute('x',roiX);ovR.setAttribute('y',roiY);
ovR.setAttribute('width',d.roiW);ovR.setAttribute('height',d.roiH);}
const ovD=$('ov-dice');
if(d.diceX!==undefined&&d.diceX>=0){
ovD.setAttribute('x',d.diceX);ovD.setAttribute('y',d.diceY);
ovD.setAttribute('width',d.diceW);ovD.setAttribute('height',d.diceH);
ovD.style.display='';}
else{ovD.style.display='none';}
$('score').textContent=d.goals;
if(d.state===2||d.state===3)$('info').textContent='fps:'+d.fps;
if(d.state===1)log('Calibrating...','log-cal');
if(d.calibrated&&lastState===1&&d.state===0){
log('Calibrated: contrast>='+d.calContrast+' size='+d.calPx+'px bbox='+d.calW+'x'+d.calH+'px','log-cal');
log('Limits: pixels '+Math.max(5,Math.floor(d.calPx/3))+'-'+(d.calPx*4)+
', bbox<='+Math.max(d.calW,d.calH)*2+'px','log-cal');}
if(d.state===2&&d.matchPx>0){
const cls=d.reject==='DICE'?'log-dice':'log-reject';
log(d.reject+': '+d.matchPx+'px bbox='+d.bboxW+'x'+d.bboxH+
' dens='+d.density+'% (lim:'+d.minPx+'-'+d.maxPx+' bbox<='+d.maxBbox+')',cls);}
if(d.scored&&d.goalSeq>lastGolSeq){
lastGolSeq=d.goalSeq;
$('gol-flash').classList.add('active');$('gol-text').classList.add('active');
setTimeout(()=>{$('gol-flash').classList.remove('active');
$('gol-text').classList.remove('active')},2000);
const stamp=Date.now();
const snapUrl='/goal-snapshot?t='+stamp;
const prevUrl=d.hasSnapPrev?('/goal-snapshot-prev?t='+stamp):'';
const e=document.createElement('div');e.className='gol-entry';
e.dataset.prevUrl=prevUrl;
e.onclick=function(){showLightbox(this.querySelector('img').src,this.dataset.prevUrl||'');};
const img=document.createElement('img');
img.src=snapUrl;
const info=document.createElement('div');info.className='gol-info';
const num=document.createElement('div');num.className='gol-num';
num.textContent=t('train.gol_num',d.goals);
const tm=document.createElement('div');tm.className='gol-time';
tm.textContent=new Date().toLocaleTimeString();
info.appendChild(num);info.appendChild(tm);
const vb=document.createElement('button');vb.className='btn-var';
vb.textContent='VAR';
vb.onclick=function(ev){ev.stopPropagation();
if(this.disabled)return;
showVAR(img.src,e.dataset.prevUrl||'',e,this);return false;};
e.appendChild(img);e.appendChild(info);e.appendChild(vb);
glog.insertBefore(e,glog.firstChild);}
}catch(e){$('info').textContent=t('train.disconnected');}},500);

document.addEventListener('DOMContentLoaded',function(){setLang(curLang);});
</script></body></html>
)rawliteral";
