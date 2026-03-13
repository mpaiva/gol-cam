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
.feed-box img{width:100%;border:2px solid #333;border-radius:8px}
.feed-label{font-size:0.85em;color:#888;margin:4px 0}
.feed-status{font-size:0.75em;padding:2px 8px;border-radius:8px;display:inline-block;margin:2px 0}
.st-online{background:#0a0;color:#fff}
.st-offline{background:#c00;color:#fff}
.st-idle{background:#333;color:#888}

/* Controls */
#controls{display:none;width:100%;max-width:600px;margin:8px 0;text-align:center}
.ctrl-row{display:flex;gap:8px;justify-content:center;flex-wrap:wrap;margin:6px 0}
.btn-cal{background:#f90;color:#000}
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
</style></head><body>
<div id='gol-flash'></div>
<div id='gol-text'>GOOOL!</div>
<div id='lightbox'>
<div id='lb-title'></div>
<img id='lb-img'/>
<div id='lb-buttons'>
<button class='btn-foi' id='btn-lb-foi'>Foi Gol</button>
<button class='btn-anula' id='btn-lb-anula'>Anula</button>
</div>
</div>

<h1>gol-cam MATCH</h1>
<a href='/'>&#8592; Menu</a>

<!-- Config panel -->
<div id='config'>
<h2>Board IPs</h2>
<div class='cfg-row'>
<label>Home:</label><input id='ip-home' placeholder='192.168.x.x'>
</div>
<div class='cfg-row'>
<label>Away:</label><input id='ip-away' placeholder='192.168.x.x'>
</div>
<div class='cfg-row'>
<button class='btn btn-go' id='btn-connect' onclick='connect()'>Connect</button>
</div>
<div id='cfg-msg' style='color:#888;font-size:0.85em;margin-top:8px'></div>
</div>

<!-- Scoreboard -->
<div id='scoreboard'>
<div class='team-label'>HOME</div>
<div id='score-line'><span id='score-home'>0</span><span class='x'>x</span><span id='score-away'>0</span></div>
<div class='team-label'>AWAY</div>
</div>

<!-- Camera feeds -->
<div id='feeds' style='display:none'>
<div class='feed-box'>
<div class='feed-label'>Home Goal <span id='st-home' class='feed-status st-idle'>...</span></div>
<img id='cam-home'/>
<div class='ctrl-row'><button class='btn btn-cal' onclick='calibrate("home")'>Calibrate Home</button></div>
</div>
<div class='feed-box'>
<div class='feed-label'>Away Goal <span id='st-away' class='feed-status st-idle'>...</span></div>
<img id='cam-away'/>
<div class='ctrl-row'><button class='btn btn-cal' onclick='calibrate("away")'>Calibrate Away</button></div>
</div>
</div>

<!-- Match controls -->
<div id='controls'>
<div class='ctrl-row'>
<button class='btn btn-start' id='btn-start' onclick='matchCmd("start")'>Start Match</button>
<button class='btn btn-pause' id='btn-pause' onclick='matchCmd("pause")' style='display:none'>Pause</button>
<button class='btn btn-resume' id='btn-resume' onclick='matchCmd("resume")' style='display:none'>Resume</button>
<button class='btn btn-reset' id='btn-reset' onclick='matchReset()'>Reset</button>
<button class='btn btn-end' id='btn-stop' onclick='matchCmd("stop")' style='display:none'>End Match</button>
</div>
</div>

<!-- Goal log -->
<div id='gol-log'></div>

<script>
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
  if(!h||!a){$('cfg-msg').textContent='Enter both IPs';return;}
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
    if(d.state===2){el.textContent='PLAYING';el.className='feed-status st-online';}
    else if(d.state===3){el.textContent='PAUSED';el.className='feed-status st-online';}
    else if(d.state===0){el.textContent=d.calibrated?'READY':'IDLE';el.className='feed-status st-idle';}
    else{el.textContent='CAL...';el.className='feed-status st-idle';}
  }catch(e){
    b.online=false;
    $('st-'+side).textContent='OFFLINE';
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
  const scorer=side==='home'?'AWAY':'HOME';
  const totalHome=boards.away.goals,totalAway=boards.home.goals;
  const snapUrl='http://'+ip+'/goal-snapshot?t='+Date.now();

  const e=document.createElement('div');e.className='gol-entry';
  e.onclick=function(){showLightbox(this.querySelector('img').src);};
  const img=document.createElement('img');img.src=snapUrl;
  const info=document.createElement('div');info.className='gol-info';
  const num=document.createElement('div');num.className='gol-num';
  num.textContent='GOL #'+goalNum+' ('+scorer+')';
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
  $('lb-img').src=src;$('lb-title').textContent='VAR - Video Review';
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
    if(varBtn){varBtn.textContent='ANULADO';varBtn.disabled=true;
    varBtn.style.background='#555';}
  });
  $('lightbox').style.display='none';
};

async function calibrate(side){
  const ip=boards[side].ip;
  if(!ip)return;
  await fetch('http://'+ip+'/calibrate');
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
</script>
</body></html>
)rawliteral";
