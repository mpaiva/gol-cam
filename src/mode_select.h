#pragma once

static const char MODE_SELECT_HTML[] = R"rawliteral(
<!DOCTYPE html><html><head>
<title>gol-cam</title>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
--bg:#0a0a0a;--card:#161616;--border:#262626;--muted:#888;
--accent:#0ff;--green:#0a0;--orange:#f90
}
body{background:var(--bg);color:#fff;font-family:system-ui,-apple-system,sans-serif;
display:flex;flex-direction:column;align-items:center;justify-content:center;
min-height:100vh;padding:24px;background-image:radial-gradient(circle at 50% 0%,#1a1a1a 0%,var(--bg) 60%)}
h1{font-size:2.4em;margin-bottom:6px;letter-spacing:1px;font-weight:800}
.tag{color:var(--muted);font-size:0.85em;letter-spacing:0.5px;margin-bottom:36px}
.cards{display:flex;gap:18px;flex-wrap:wrap;justify-content:center;max-width:580px}
.card{background:var(--card);border:1px solid var(--border);border-radius:14px;
padding:28px 22px;text-align:center;cursor:pointer;transition:border-color 0.15s,transform 0.1s;
text-decoration:none;color:#fff;width:230px;display:flex;flex-direction:column;align-items:center;gap:10px}
.card:active{transform:scale(0.97)}
.card-treino{border-color:rgba(255,153,0,0.3)}
.card-treino:hover{border-color:var(--orange);box-shadow:0 0 20px rgba(255,153,0,0.15)}
.card-jogo{border-color:rgba(0,170,0,0.3)}
.card-jogo:hover{border-color:var(--green);box-shadow:0 0 20px rgba(0,170,0,0.15)}
.card .icon{font-size:2.6em;line-height:1}
.card-treino .icon{color:var(--orange)}
.card-jogo .icon{color:var(--green)}
.card h2{font-size:1.3em;font-weight:700;margin:0}
.card p{color:var(--muted);font-size:0.82em;line-height:1.4}
.lang-picker{position:fixed;top:8px;right:8px;display:flex;gap:4px;z-index:200}
.lang-btn{padding:3px 8px;font-size:0.7em;border:1px solid #333;border-radius:4px;
background:#0f0f0f;color:var(--muted);cursor:pointer;font-weight:bold}
.lang-btn.active{background:var(--accent);color:#000;border-color:var(--accent)}
</style></head><body>
<div class='lang-picker'>
<button class='lang-btn' onclick="setLang('en')">EN</button>
<button class='lang-btn' onclick="setLang('pt')">PT</button>
</div>
<h1>gol-cam</h1>
<div class='tag' data-i18n='mode.tag'>Button-soccer goal detection</div>
<div class='cards'>
<a class='card card-treino' href='/training'>
<div class='icon'>🎯</div>
<h2 data-i18n='mode.training'>Training</h2>
<p data-i18n='mode.training_desc'>1 camera<br>Calibration &amp; practice</p>
</a>
<a class='card card-jogo' href='/match'>
<div class='icon'>⚽</div>
<h2 data-i18n='mode.match'>Match</h2>
<p data-i18n='mode.match_desc'>2 cameras<br>Live scoreboard</p>
</a>
</div>
<script>
var I18N={
en:{
'mode.tag':'Button-soccer goal detection',
'mode.training':'Training',
'mode.training_desc':'1 camera\nCalibration & practice',
'mode.match':'Match',
'mode.match_desc':'2 cameras\nLive scoreboard'
},
pt:{
'mode.tag':'Detecção de gol no futebol de botão',
'mode.training':'Treino',
'mode.training_desc':'1 câmera\nCalibração e prática',
'mode.match':'Jogo',
'mode.match_desc':'2 câmeras\nPlacar ao vivo'
}
};
var curLang='en';
function t(key){
var s=(I18N[curLang]&&I18N[curLang][key])||I18N.en[key]||key;
return s;
}
function setLang(lang){
curLang=lang;localStorage.setItem('gol-lang',lang);
document.querySelectorAll('[data-i18n]').forEach(function(el){
var txt=t(el.getAttribute('data-i18n'));
if(txt.indexOf('\n')>=0){el.innerHTML=txt.replace(/\n/g,'<br>');}
else{el.textContent=txt;}
});
document.querySelectorAll('.lang-btn').forEach(function(b){
b.classList.toggle('active',b.textContent.toLowerCase()===lang);});
}
(function(){
var saved=localStorage.getItem('gol-lang');
if(saved){curLang=saved;}
else{var nav=navigator.language||'';curLang=nav.startsWith('pt')?'pt':'en';}
setLang(curLang);
})();
</script>
</body></html>
)rawliteral";
