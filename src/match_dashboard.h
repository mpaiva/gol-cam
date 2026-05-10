#pragma once

static const char MATCH_DASHBOARD_HTML[] = R"rawliteral(
<!DOCTYPE html><html><head>
<title>gol-cam MATCH</title>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#111;color:#fff;font-family:system-ui,sans-serif;
display:flex;flex-direction:column;align-items:center;padding:10px}
h1{font-size:1.6em;margin:5px 0}
a{color:#888;text-decoration:none;font-size:0.8em}
a:hover{color:#fff}

/* Config panel */
#config{background:#1a1a1a;border:1px solid #333;border-radius:12px;
padding:20px;margin:10px 0;width:100%;max-width:600px;text-align:center}
#config h2{font-size:1.2em;margin-bottom:12px}
#config label{color:#888;font-size:0.85em}
#config input{background:#222;color:#fff;border:1px solid #444;border-radius:6px;
padding:8px 12px;font-size:1em;width:200px;margin:4px 8px}
.cfg-row{display:flex;align-items:center;justify-content:center;gap:8px;margin:8px 0;flex-wrap:wrap}
.btn{padding:10px 20px;font-size:1em;border:none;border-radius:8px;
cursor:pointer;font-weight:bold;transition:all 0.2s}
.btn:active{transform:scale(0.95)}
.btn-go{background:#0f0;color:#000}
.btn-go:disabled{background:#333;color:#666;cursor:default}

/* Scoreboard */
#scoreboard{display:none;margin:10px 0;text-align:center}
#score-line{font-size:4em;font-weight:bold;margin:5px 0}
.team-label{font-size:1.2em;color:#888}
#score-line .x{color:#555;margin:0 15px}

/* Camera feeds */
#feeds{display:none;width:100%;max-width:800px;gap:10px;margin:10px 0;
justify-content:center;flex-wrap:wrap}
.feed-box{flex:1;min-width:280px;max-width:390px;text-align:center}
.feed-box img{width:100%;border:2px solid #333;border-radius:8px;display:block}
.cam-wrap{position:relative;line-height:0}
.cam-wrap svg{position:absolute;inset:2px;width:calc(100% - 4px);height:calc(100% - 4px);pointer-events:none}
.feed-label{font-size:0.85em;color:#888;margin:4px 0}
.feed-status{font-size:0.75em;padding:2px 8px;border-radius:8px;display:inline-block;margin:2px 0}
.st-online{background:#0a0;color:#fff}
.st-offline{background:#c00;color:#fff}
.st-idle{background:#333;color:#888}

/* Controls */
#controls{display:none;width:100%;max-width:600px;margin:8px 0;text-align:center}
.ctrl-row{display:flex;gap:8px;justify-content:center;flex-wrap:wrap;margin:6px 0}
.btn-cal{background:#f90;color:#000}
.btn-tune{background:#36c;color:#fff}
.btn-start{background:#0a0;color:#fff}
.btn-pause{background:#ff0;color:#000}
.btn-resume{background:#0a0;color:#fff}
.btn-reset{background:#555;color:#fff}
.btn-end{background:#c00;color:#fff}

/* Gol flash */
#gol-flash{position:fixed;top:0;left:0;width:100%;height:100%;
background:rgba(0,255,0,0.3);pointer-events:none;opacity:0;transition:opacity 0.5s}
#gol-text{position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);
font-size:6em;font-weight:bold;color:#0f0;opacity:0;transition:opacity 0.5s;
pointer-events:none;text-shadow:0 0 30px #0f0}
.active{opacity:1!important}

/* Goal log */
#gol-log{width:100%;max-width:600px;margin:10px 0}
.gol-entry{background:#1a1a1a;border:1px solid #333;border-radius:8px;
margin:8px 0;padding:10px;display:flex;gap:10px;align-items:center;cursor:pointer}
.gol-entry:active{background:#222}
.gol-entry img{width:100px;border-radius:4px;border:2px solid #0f0}
.gol-entry .gol-info{flex:1;font-size:0.85em}
.gol-entry .gol-num{color:#0f0;font-size:1.2em;font-weight:bold}
.gol-entry .gol-side{font-size:0.8em;color:#888}
.gol-entry .gol-time{color:#888;font-size:0.75em}
.gol-annulled{opacity:0.4}
.gol-annulled .gol-num{text-decoration:line-through;color:#f44}
.btn-var{background:#c00;color:#fff;padding:6px 12px;font-size:0.8em;
border:none;border-radius:6px;cursor:pointer;font-weight:bold}

/* Lightbox */
#lightbox{position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.9);
display:none;flex-direction:column;justify-content:center;align-items:center;z-index:100}
#lightbox img{max-width:95%;max-height:70%;border:3px solid #0f0;border-radius:8px}
#lb-title{color:#fff;font-size:1.5em;font-weight:bold;margin:10px 0}
#lb-buttons{display:none;gap:20px;margin:15px 0}
.btn-foi{background:#0a0;color:#fff;padding:14px 30px;font-size:1.2em;
border:none;border-radius:10px;cursor:pointer;font-weight:bold}
.btn-anula{background:#c00;color:#fff;padding:14px 30px;font-size:1.2em;
border:none;border-radius:10px;cursor:pointer;font-weight:bold}
.cam-sliders{display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin:4px 0;font-size:0.7em;color:#888}
.cam-sliders label{display:flex;align-items:center;gap:3px}
.cam-sliders input[type=range]{width:60px;accent-color:#0ff}
.roi-controls{display:flex;gap:4px;align-items:center;justify-content:center;margin:4px 0;flex-wrap:wrap}
.roi-controls .btn{padding:6px 10px;font-size:0.8em;background:#333;color:#fff;min-width:34px}
.roi-controls .btn:active{background:#555}
.roi-controls .btn-rst{background:#555;font-size:0.7em}
.roi-info{color:#0ff;font-size:0.7em;text-align:center;margin:2px 0}
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
<button class='btn-foi' id='btn-lb-foi' data-i18n='var.confirm'>Foi Gol</button>
<button class='btn-anula' id='btn-lb-anula' data-i18n='var.annul'>Anula</button>
</div>
</div>

<h1 data-i18n='match.title'>gol-cam MATCH</h1>
<a href='/' data-i18n='nav.menu'>&#8592; Menu</a>

<!-- Config panel -->
<div id='config'>
<h2 data-i18n='match.board_ips'>Board IPs</h2>
<div class='cfg-row'>
<label data-i18n='match.home'>Home:</label><input id='ip-home' placeholder='192.168.x.x'>
</div>
<div class='cfg-row'>
<label data-i18n='match.away'>Away:</label><input id='ip-away' placeholder='192.168.x.x'>
</div>
<div class='cfg-row'>
<button class='btn btn-go' id='btn-connect' onclick='connect()' data-i18n='match.connect'>Connect</button>
</div>
<div id='cfg-msg' style='color:#888;font-size:0.85em;margin-top:8px'></div>
</div>

<!-- Scoreboard -->
<div id='scoreboard'>
<div class='team-label' id='lbl-home' data-i18n='match.home_label'>HOME</div>
<div id='score-line'><span id='score-home'>0</span><span class='x'>x</span><span id='score-away'>0</span></div>
<div class='team-label' id='lbl-away' data-i18n='match.away_label'>AWAY</div>
</div>

<!-- Camera feeds -->
<div id='feeds' style='display:none'>
<div class='feed-box'>
<div class='feed-label'><span data-i18n='match.home_goal'>Home Goal</span> <span id='st-home' class='feed-status st-idle'>...</span></div>
<div class='cam-wrap'>
<img id='cam-home'/>
<svg id='ov-home' viewBox='0 0 320 240' preserveAspectRatio='none'>
<rect class='ov-roi' fill='none' stroke='#ffff00' stroke-width='2'/>
<rect class='ov-dice' fill='none' stroke='#32cd32' stroke-width='2' style='display:none'/>
</svg>
</div>
<div class='cam-sliders'>
<label>Bri<input type='range' min='-2' max='2' value='-1' onchange="camAdj('home','bri',this.value)"></label>
<label>Con<input type='range' min='-2' max='2' value='1' onchange="camAdj('home','con',this.value)"></label>
<label>Sharp<input type='range' min='-2' max='2' value='1' onchange="camAdj('home','sharp',this.value)"></label>
<label>Exp<input type='range' min='0' max='1200' value='150' onchange="camAdj('home','aec',this.value)"></label>
<label>Gain<input type='range' min='0' max='30' value='8' onchange="camAdj('home','gain',this.value)"></label>
<label>GCeil<input type='range' min='0' max='6' value='1' onchange="camAdj('home','gceil',this.value)"></label>
<label>Gamma<input type='checkbox' onclick="camAdj('home','gma',this.checked?1:0)"></label>
<label>Lens<input type='checkbox' onclick="camAdj('home','lenc',this.checked?1:0)"></label>
<label>B&amp;W<input type='range' min='0' max='255' value='30' onchange="setThreshold('home',this.value)"></label>
<label>Vol<input type='range' min='0' max='100' value='70' onchange="setVolume('home',this.value)"><span style='cursor:pointer' onclick="testSound('home')">&#9654;</span></label>
<label>LED<input type='checkbox' onclick="setLed('home',this.checked?1:0)"></label>
</div>
<div class='roi-controls'>
<button class='btn' onclick="moveRoi('home',0,-8)">&#9650;</button>
<button class='btn' onclick="moveRoi('home',-8,0)">&#9664;</button>
<button class='btn btn-rst' onclick="resetRoi('home')">RST</button>
<button class='btn' onclick="moveRoi('home',8,0)">&#9654;</button>
<button class='btn' onclick="moveRoi('home',0,8)">&#9660;</button>
<span style='color:#666'>|</span>
W<button class='btn' onclick="resizeRoi('home',-8,0)">&#8722;</button>
<button class='btn' onclick="resizeRoi('home',8,0)">&#43;</button>
H<button class='btn' onclick="resizeRoi('home',0,-8)">&#8722;</button>
<button class='btn' onclick="resizeRoi('home',0,8)">&#43;</button>
</div>
<div class='roi-info' id='roi-home'></div>
<div class='ctrl-row'>
<button class='btn btn-tune' onclick='autotune("home")' data-i18n='match.tune_home'>&#9881; Auto-Tune</button>
<button class='btn btn-cal' onclick='calibrate("home")' data-i18n='match.cal_home'>Calibrate Home</button>
</div>
</div>
<div class='feed-box'>
<div class='feed-label'><span data-i18n='match.away_goal'>Away Goal</span> <span id='st-away' class='feed-status st-idle'>...</span></div>
<div class='cam-wrap'>
<img id='cam-away'/>
<svg id='ov-away' viewBox='0 0 320 240' preserveAspectRatio='none'>
<rect class='ov-roi' fill='none' stroke='#ffff00' stroke-width='2'/>
<rect class='ov-dice' fill='none' stroke='#32cd32' stroke-width='2' style='display:none'/>
</svg>
</div>
<div class='cam-sliders'>
<label>Bri<input type='range' min='-2' max='2' value='-1' onchange="camAdj('away','bri',this.value)"></label>
<label>Con<input type='range' min='-2' max='2' value='1' onchange="camAdj('away','con',this.value)"></label>
<label>Sharp<input type='range' min='-2' max='2' value='1' onchange="camAdj('away','sharp',this.value)"></label>
<label>Exp<input type='range' min='0' max='1200' value='150' onchange="camAdj('away','aec',this.value)"></label>
<label>Gain<input type='range' min='0' max='30' value='8' onchange="camAdj('away','gain',this.value)"></label>
<label>GCeil<input type='range' min='0' max='6' value='1' onchange="camAdj('away','gceil',this.value)"></label>
<label>Gamma<input type='checkbox' onclick="camAdj('away','gma',this.checked?1:0)"></label>
<label>Lens<input type='checkbox' onclick="camAdj('away','lenc',this.checked?1:0)"></label>
<label>B&amp;W<input type='range' min='0' max='255' value='30' onchange="setThreshold('away',this.value)"></label>
<label>Vol<input type='range' min='0' max='100' value='70' onchange="setVolume('away',this.value)"><span style='cursor:pointer' onclick="testSound('away')">&#9654;</span></label>
<label>LED<input type='checkbox' onclick="setLed('away',this.checked?1:0)"></label>
</div>
<div class='roi-controls'>
<button class='btn' onclick="moveRoi('away',0,-8)">&#9650;</button>
<button class='btn' onclick="moveRoi('away',-8,0)">&#9664;</button>
<button class='btn btn-rst' onclick="resetRoi('away')">RST</button>
<button class='btn' onclick="moveRoi('away',8,0)">&#9654;</button>
<button class='btn' onclick="moveRoi('away',0,8)">&#9660;</button>
<span style='color:#666'>|</span>
W<button class='btn' onclick="resizeRoi('away',-8,0)">&#8722;</button>
<button class='btn' onclick="resizeRoi('away',8,0)">&#43;</button>
H<button class='btn' onclick="resizeRoi('away',0,-8)">&#8722;</button>
<button class='btn' onclick="resizeRoi('away',0,8)">&#43;</button>
</div>
<div class='roi-info' id='roi-away'></div>
<div class='ctrl-row'>
<button class='btn btn-tune' onclick='autotune("away")' data-i18n='match.tune_away'>&#9881; Auto-Tune</button>
<button class='btn btn-cal' onclick='calibrate("away")' data-i18n='match.cal_away'>Calibrate Away</button>
</div>
</div>
</div>

<!-- Match controls -->
<div id='controls'>
<div class='ctrl-row'>
<button class='btn btn-start' id='btn-start' onclick='matchCmd("start")' data-i18n='match.start'>Start Match</button>
<button class='btn btn-pause' id='btn-pause' onclick='matchCmd("pause")' style='display:none' data-i18n='match.pause'>Pause</button>
<button class='btn btn-resume' id='btn-resume' onclick='matchCmd("resume")' style='display:none' data-i18n='match.resume'>Resume</button>
<button class='btn btn-reset' id='btn-reset' onclick='matchReset()' data-i18n='match.reset'>Reset</button>
<button class='btn btn-end' id='btn-stop' onclick='matchCmd("stop")' style='display:none' data-i18n='match.end'>End Match</button>
</div>
</div>

<!-- Goal log -->
<div id='gol-log'></div>

<script>
var I18N={
en:{
'nav.menu':'\u2190 Menu',
'goal.flash':'GOOOL!',
'var.title':'VAR - Video Review',
'var.confirm':'Foi Gol',
'var.annul':'Annul',
'var.annulled':'ANNULLED',
'match.title':'gol-cam MATCH',
'match.board_ips':'Board IPs',
'match.home':'Home:',
'match.away':'Away:',
'match.connect':'Connect',
'match.enter_both':'Enter both IPs',
'match.home_label':'HOME',
'match.away_label':'AWAY',
'match.home_goal':'Home Goal',
'match.away_goal':'Away Goal',
'match.cal_home':'Calibrate Home',
'match.cal_away':'Calibrate Away',
'match.tune_home':'⚙ Auto-Tune',
'match.tune_away':'⚙ Auto-Tune',
'match.start':'Start Match',
'match.pause':'Pause',
'match.resume':'Resume',
'match.reset':'Reset',
'match.end':'End Match',
'match.offline':'OFFLINE',
'match.ready':'READY',
'match.idle':'IDLE',
'match.cal':'CAL...',
'match.playing':'PLAYING',
'match.paused':'PAUSED',
'match.goal_num':'GOL #%d (%s)',
'match.scorer_home':'HOME',
'match.scorer_away':'AWAY'
},
pt:{
'nav.menu':'\u2190 Menu',
'goal.flash':'GOOOL!',
'var.title':'VAR - Revis\u00e3o',
'var.confirm':'Foi Gol',
'var.annul':'Anula',
'var.annulled':'ANULADO',
'match.title':'gol-cam JOGO',
'match.board_ips':'IPs das Placas',
'match.home':'Casa:',
'match.away':'Fora:',
'match.connect':'Conectar',
'match.enter_both':'Digite ambos os IPs',
'match.home_label':'CASA',
'match.away_label':'FORA',
'match.home_goal':'Gol Casa',
'match.away_goal':'Gol Fora',
'match.cal_home':'Calibrar Casa',
'match.cal_away':'Calibrar Fora',
'match.tune_home':'⚙ Auto-Ajuste',
'match.tune_away':'⚙ Auto-Ajuste',
'match.start':'Iniciar Partida',
'match.pause':'Pausar',
'match.resume':'Continuar',
'match.reset':'Reiniciar',
'match.end':'Encerrar',
'match.offline':'OFFLINE',
'match.ready':'PRONTO',
'match.idle':'AGUARDANDO',
'match.cal':'CAL...',
'match.playing':'JOGANDO',
'match.paused':'PAUSADO',
'match.goal_num':'GOL #%d (%s)',
'match.scorer_home':'CASA',
'match.scorer_away':'FORA'
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
            away:{ip:'',online:false,goals:0,goalSeq:0,state:-1,calibrated:false}};
let polling=null;
let varEntry=null,varBtn=null,varSide=null;

// Restore from localStorage
try{
  const saved=JSON.parse(localStorage.getItem('gol-match'));
  if(saved){$('ip-home').value=saved.home||'';$('ip-away').value=saved.away||'';}
}catch(e){}

// Auto-detect: if accessed from a board, try to get peer info
(async function(){
  try{
    const r=await fetch('/status');const d=await r.json();
    const myIp=location.hostname;
    if(d.role==='home'){
      $('ip-home').value=myIp;
      if(d.peer)$('ip-away').value=d.peer;
    }else if(d.role==='away'){
      $('ip-away').value=myIp;
      if(d.peer)$('ip-home').value=d.peer;
    }else{
      if(!$('ip-home').value)$('ip-home').value=myIp;
    }
  }catch(e){}
})();

function connect(){
  const h=$('ip-home').value.trim(),a=$('ip-away').value.trim();
  if(!h||!a){$('cfg-msg').textContent=t('match.enter_both');return;}
  boards.home.ip=h;boards.away.ip=a;
  localStorage.setItem('gol-match',JSON.stringify({home:h,away:a}));
  $('config').style.display='none';
  $('scoreboard').style.display='block';
  $('feeds').style.display='flex';
  $('controls').style.display='block';
  // Start streams
  $('cam-home').src='http://'+h+':81/stream';
  $('cam-away').src='http://'+a+':81/stream';
  // Start polling
  if(polling)clearInterval(polling);
  polling=setInterval(poll,500);
  poll();
}

async function poll(){
  await Promise.allSettled([pollBoard('home'),pollBoard('away')]);
  updateControls();
}

async function pollBoard(side){
  const b=boards[side];
  try{
    const r=await fetch('http://'+b.ip+'/status',{signal:AbortSignal.timeout(2000)});
    const d=await r.json();
    b.online=true;
    b.state=d.state;
    b.calibrated=d.calibrated;
    // Detect new goals
    const prevGoals=b.goals;
    b.goals=d.goals;
    if(d.goalSeq>b.goalSeq&&d.scored){
      b.goalSeq=d.goalSeq;
      flashGoal(side,b.goals);
      addGoalEntry(side,b.goals,b.ip);
    }else if(d.goalSeq>b.goalSeq){
      b.goalSeq=d.goalSeq;
    }
    // Update status indicator
    const el=$('st-'+side);
    if(d.state===2){el.textContent=t('match.playing');el.className='feed-status st-online';}
    else if(d.state===3){el.textContent=t('match.paused');el.className='feed-status st-online';}
    else if(d.state===0){el.textContent=d.calibrated?t('match.ready'):t('match.idle');el.className='feed-status st-idle';}
    else{el.textContent=t('match.cal');el.className='feed-status st-idle';}
    // Update ROI info + SVG overlays
    var ri=$(('roi-'+side));
    if(ri&&d.roiW!==undefined)ri.textContent='ROI: '+d.roiW+'x'+d.roiH+' offset:'+d.roiX+','+d.roiY;
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
  // Score
  // Home camera watches home goal — goals scored there count for AWAY (opponent scored)
  // Away camera watches away goal — goals scored there count for HOME
  $('score-home').textContent=boards.away.goals;
  $('score-away').textContent=boards.home.goals;

  const anyPlaying=boards.home.state===2||boards.away.state===2;
  const anyPaused=boards.home.state===3||boards.away.state===3;
  const inGame=anyPlaying||anyPaused;
  const bothIdle=boards.home.state===0&&boards.away.state===0;
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
  // Which team scored? Camera at home goal detects away team scoring, and vice versa
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
  sd.textContent=totalHome+' x '+totalAway;
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

async function setThreshold(side,val){
  const ip=boards[side].ip;if(!ip)return;
  await fetch('http://'+ip+'/threshold?val='+val);}

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
  const ip=boards[side].ip;
  if(!ip)return;
  await fetch('http://'+ip+'/calibrate');
}

function setMatchSlider(s,v){
  if(v===undefined||v===null||Number.isNaN(v))return;
  s.value=v;s.setAttribute('value',v);
  s.dispatchEvent(new Event('input',{bubbles:true}));
}

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
    else if(oc.includes("'sharp'"))setMatchSlider(s,sh);
    else if(!useCur&&oc.includes('setThreshold'))setMatchSlider(s,d.autoThresh);});
  panel.querySelectorAll('input[type=checkbox]').forEach(cb=>{
    const oc=cb.getAttribute('onclick')||'';
    if(oc.includes("'gma'")&&gm!==undefined)cb.checked=!!gm;
    else if(oc.includes("'lenc'")&&le!==undefined)cb.checked=!!le;});
}

async function autotune(side){
  const ip=boards[side].ip;if(!ip)return;
  await fetch('http://'+ip+'/autotune');
  const panel=document.querySelector('#cam-'+side).closest('.feed-box');
  const tick=setInterval(async()=>{
    try{const r=await fetch('http://'+ip+'/status');const d=await r.json();
      if(d.autoDone===1){
        clearInterval(tick);
        syncMatchSliders(panel,d,false);
        setTimeout(async()=>{try{const r2=await fetch('http://'+ip+'/status');
          const d2=await r2.json();if(d2.autoDone===1)syncMatchSliders(panel,d2,false);
        }catch(e){}},400);
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
  boards.home.goals=0;boards.home.goalSeq=0;boards.home.state=-1;
  boards.away.goals=0;boards.away.goalSeq=0;boards.away.state=-1;
  $('score-home').textContent='0';$('score-away').textContent='0';
  $('gol-log').innerHTML='';
}

document.addEventListener('DOMContentLoaded',function(){setLang(curLang);});
</script>
</body></html>
)rawliteral";
