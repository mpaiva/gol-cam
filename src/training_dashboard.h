#pragma once

static const char TRAINING_DASHBOARD_HTML[] = R"rawliteral(
<!DOCTYPE html><html><head>
<title>gol-cam</title>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
*{box-sizing:border-box}
body{background:#111;color:#fff;font-family:system-ui,sans-serif;
display:flex;flex-direction:column;align-items:center;margin:0;padding:15px}
h1{margin:5px 0;font-size:1.8em}
img{max-width:100%;border:2px solid #333;border-radius:8px;margin:10px 0}
#score{font-size:5em;font-weight:bold;margin:5px 0;transition:all 0.3s}
#gol-flash{position:fixed;top:0;left:0;width:100%;height:100%;
background:rgba(0,255,0,0.3);pointer-events:none;opacity:0;transition:opacity 0.5s}
#gol-text{position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);
font-size:6em;font-weight:bold;color:#0f0;opacity:0;transition:opacity 0.5s;
pointer-events:none;text-shadow:0 0 30px #0f0}
.active{opacity:1 !important}
.btn{padding:10px 20px;font-size:1em;border:none;border-radius:8px;
cursor:pointer;font-weight:bold;transition:all 0.2s}
.btn:active{transform:scale(0.95)}
.btn-cal{background:#f90;color:#000}
.btn-start{background:#0a0;color:#fff}
#info{color:#888;font-size:0.85em;margin:5px;text-align:center}
#cal-info{color:#f90;font-size:0.9em;margin:5px;display:none}
#cal-snap{max-width:100%;border:2px solid #f90;border-radius:8px;margin:8px 0;display:none}
#cal-feedback{color:#fff;font-size:0.85em;margin:5px;padding:8px 12px;
background:#222;border-radius:6px;display:none;max-width:400px;text-align:center}
.cal-ok{border-left:3px solid #0f0}.cal-fail{border-left:3px solid #f44}
#game-bar{display:none;width:100%;max-width:400px;background:#1a1a1a;
border:1px solid #333;border-radius:8px;padding:8px;margin:4px 0}
#game-bar .bar-row{display:flex;align-items:center;justify-content:center;gap:8px}
#game-bar .btn{padding:8px 16px;font-size:0.9em}
.btn-pause{background:#ff0;color:#000}
.btn-resume{background:#0a0;color:#fff}
.btn-reset{background:#555;color:#fff}
.btn-end{background:#c00;color:#fff}
#countdown{display:none;text-align:center;margin:4px 0}
#cd-bar{width:100%;max-width:400px;height:6px;background:#333;border-radius:3px;
overflow:hidden;margin:4px 0}
#cd-fill{height:100%;background:#0f0;transition:width 0.4s linear;width:100%}
#cd-text{color:#0f0;font-size:0.85em;font-weight:bold}
#gol-log{width:100%;max-width:400px;margin:10px 0}
.gol-entry{background:#1a1a1a;border:1px solid #333;border-radius:8px;
margin:8px 0;padding:10px;display:flex;gap:10px;align-items:center;cursor:pointer}
.gol-entry:active{background:#222}
.gol-entry img{width:120px;border-radius:4px;border:2px solid #0f0}
.gol-entry .gol-info{flex:1;font-size:0.85em}
.gol-entry .gol-num{color:#0f0;font-size:1.4em;font-weight:bold}
.gol-entry .gol-time{color:#888;font-size:0.75em}
.btn-var{background:#c00;color:#fff;padding:6px 12px;font-size:0.8em;
border:none;border-radius:6px;cursor:pointer;font-weight:bold}
.btn-var:active{transform:scale(0.95)}
.gol-annulled{opacity:0.4}
.gol-annulled .gol-num{text-decoration:line-through;color:#f44}
#lightbox{position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.9);
display:none;flex-direction:column;justify-content:center;align-items:center;z-index:100}
#lightbox img{max-width:95%;max-height:70%;border:3px solid #0f0;border-radius:8px}
#lb-title{color:#fff;font-size:1.5em;font-weight:bold;margin:10px 0}
#lb-buttons{display:none;gap:20px;margin:15px 0}
.btn-foi{background:#0a0;color:#fff;padding:14px 30px;font-size:1.2em;
border:none;border-radius:10px;cursor:pointer;font-weight:bold}
.btn-anula{background:#c00;color:#fff;padding:14px 30px;font-size:1.2em;
border:none;border-radius:10px;cursor:pointer;font-weight:bold}
#console{width:100%;max-width:400px;height:180px;background:#0a0a0a;
border:1px solid #333;border-radius:6px;margin:10px 0;padding:8px;
font-family:monospace;font-size:0.7em;color:#0f0;overflow-y:auto;
line-height:1.4}
.log-reject{color:#f44}.log-dice{color:#0f0}.log-cal{color:#f90}
.idle-controls{display:flex;gap:10px;flex-wrap:wrap;justify-content:center;margin:8px 0}
#state-badge{font-size:0.8em;padding:4px 12px;border-radius:12px;margin:5px}
.s-idle{background:#333;color:#888}
.s-cal{background:#f90;color:#000}
.s-play{background:#0a0;color:#fff}
.s-pause{background:#ff0;color:#000}
.lang-picker{position:fixed;top:8px;right:8px;display:flex;gap:4px;z-index:200}
.lang-btn{padding:4px 8px;font-size:0.75em;border:1px solid #555;border-radius:4px;
background:#222;color:#aaa;cursor:pointer;font-weight:bold}
.lang-btn.active{background:#0f0;color:#000;border-color:#0f0}
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
<button class='btn-foi' onclick='varConfirm()' data-i18n='var.confirm'>Foi Gol</button>
<button class='btn-anula' onclick='varAnula()' data-i18n='var.annul'>Anula</button>
</div>
</div>
<h1>gol-cam</h1>
<a href='/' data-i18n='nav.menu'>&#8592; Menu</a>
<div id='state-badge' class='s-idle' data-i18n='train.idle'>IDLE</div>
<div id='score' style='display:none'>0</div>
<img id='cam'/>
<div id='game-bar'>
<div class='bar-row'>
<button class='btn btn-pause' id='btn-pause' onclick='pauseGame()' data-i18n='train.pause'>Pause</button>
<button class='btn btn-resume' id='btn-resume' onclick='resumeGame()' style='display:none' data-i18n='train.resume'>Resume</button>
<button class='btn btn-reset' id='btn-reset' onclick='resetGame()' data-i18n='train.reset'>Reset</button>
<button class='btn btn-end' id='btn-stop' onclick='stopGame()' data-i18n='train.end'>End Game</button>
</div>
</div>
<div id='countdown'>
<div id='cd-bar'><div id='cd-fill'></div></div>
<div id='cd-text'></div>
</div>
<div class='idle-controls' id='idle-controls'>
<button class='btn btn-cal' id='btn-cal' onclick='calibrate()' data-i18n='train.calibrate'>Calibrate Dice</button>
<button class='btn btn-start' id='btn-start' onclick='startGame()' style='display:none' data-i18n='train.start'>Start Game</button>
</div>
<div id='cal-feedback'></div>
<img id='cal-snap'/>
<div id='cal-info'></div>
<div id='info' data-i18n='train.hint_calibrate'>Place the dadinho in view, then press Calibrate</div>
<div id='gol-log'></div>
<div id='console'></div>
<script>
var I18N={
en:{
'nav.menu':'\u2190 Menu',
'goal.flash':'GOOOL!',
'var.title':'VAR - Video Review',
'var.confirm':'Foi Gol',
'var.annul':'Annul',
'var.annulled':'ANNULLED',
'train.calibrate':'Calibrate Dice',
'train.calibrating':'Calibrating...',
'train.analyzing':'Analyzing frame...',
'train.start':'Start Game',
'train.pause':'Pause',
'train.resume':'Resume',
'train.reset':'Reset',
'train.end':'End Game',
'train.idle':'IDLE',
'train.cal_badge':'CALIBRATING...',
'train.playing':'PLAYING',
'train.paused':'PAUSED',
'train.hint_calibrate':'Place the dadinho in view, then press Calibrate',
'train.hint_calibrated':'Calibrated! Press Start Game',
'train.detecting_in':'Detecting in %ds...',
'train.disconnected':'disconnected',
'train.gol_num':'GOL #%d'
},
pt:{
'nav.menu':'\u2190 Menu',
'goal.flash':'GOOOL!',
'var.title':'VAR - Revis\u00e3o',
'var.confirm':'Foi Gol',
'var.annul':'Anula',
'var.annulled':'ANULADO',
'train.calibrate':'Calibrar Dadinho',
'train.calibrating':'Calibrando...',
'train.analyzing':'Analisando frame...',
'train.start':'Iniciar Jogo',
'train.pause':'Pausar',
'train.resume':'Continuar',
'train.reset':'Reiniciar',
'train.end':'Encerrar Jogo',
'train.idle':'AGUARDANDO',
'train.cal_badge':'CALIBRANDO...',
'train.playing':'JOGANDO',
'train.paused':'PAUSADO',
'train.hint_calibrate':'Coloque o dadinho na c\u00e2mera e pressione Calibrar',
'train.hint_calibrated':'Calibrado! Pressione Iniciar Jogo',
'train.detecting_in':'Detectando em %ds...',
'train.disconnected':'desconectado',
'train.gol_num':'GOL #%d'
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
function showLightbox(src){
varEntry=null;varBtn=null;
$('lb-img').src=src;$('lb-title').textContent='';
$('lb-buttons').style.display='none';
$('lightbox').style.display='flex';
$('lightbox').onclick=function(){this.style.display='none';};}
function showVAR(src,entry,btn){
varEntry=entry;varBtn=btn;
$('lb-img').src=src;$('lb-title').textContent=t('var.title');
$('lb-buttons').style.display='flex';
$('lightbox').style.display='flex';
$('lightbox').onclick=null;}
function varConfirm(){
$('lightbox').style.display='none';}
function varAnula(){
if(!varEntry)return;
fetch('/deduct').then(()=>{
varEntry.classList.add('gol-annulled');
if(varBtn){varBtn.textContent=t('var.annulled');varBtn.disabled=true;
varBtn.style.background='#555';}});
$('lightbox').style.display='none';}

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
$('idle-controls').style.display=idle?'':'none';
$('btn-start').style.display=(idle&&cal)?'':'none';
$('btn-cal').style.display=idle?'':'none';}

setInterval(async()=>{
try{const r=await fetch('/status');const d=await r.json();
if(d.state!==lastState){
lastState=d.state;
const b=$('state-badge');
if(d.state===0){b.textContent=t('train.idle');b.className='s-idle';
$('score').style.display='none';
$('info').textContent=d.calibrated?t('train.hint_calibrated')
:t('train.hint_calibrate');}
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
'px (RGB565: '+d.calR+','+d.calG+','+d.calB+')';}
$('score').textContent=d.goals;
if(d.state===2||d.state===3)$('info').textContent='fps:'+d.fps;
if(d.state===1)log('Calibrating...','log-cal');
if(d.calibrated&&lastState===1&&d.state===0){
log('Calibrated: color=RGB565('+d.calR+','+d.calG+','+d.calB+
') size='+d.calPx+'px bbox='+d.calW+'x'+d.calH+'px','log-cal');
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
const snapUrl='/goal-snapshot?t='+Date.now();
const e=document.createElement('div');e.className='gol-entry';
e.onclick=function(){showLightbox(this.querySelector('img').src);};
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
showVAR(img.src,e,this);return false;};
e.appendChild(img);e.appendChild(info);e.appendChild(vb);
glog.insertBefore(e,glog.firstChild);}
}catch(e){$('info').textContent=t('train.disconnected');}},500);

document.addEventListener('DOMContentLoaded',function(){setLang(curLang);});
</script></body></html>
)rawliteral";
