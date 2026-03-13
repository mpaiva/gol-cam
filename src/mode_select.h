#pragma once

static const char MODE_SELECT_HTML[] = R"rawliteral(
<!DOCTYPE html><html><head>
<title>gol-cam</title>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#111;color:#fff;font-family:system-ui,sans-serif;
display:flex;flex-direction:column;align-items:center;justify-content:center;
min-height:100vh;padding:20px}
h1{font-size:2.2em;margin-bottom:30px}
.cards{display:flex;gap:20px;flex-wrap:wrap;justify-content:center}
.card{background:#1a1a1a;border:2px solid #333;border-radius:16px;
padding:30px 40px;text-align:center;cursor:pointer;transition:all 0.2s;
text-decoration:none;color:#fff;width:200px}
.card:hover{border-color:#0f0;transform:translateY(-4px)}
.card:active{transform:scale(0.97)}
.card h2{font-size:1.6em;margin-bottom:8px}
.card p{color:#888;font-size:0.9em}
.card .icon{font-size:3em;margin-bottom:12px}
.card-treino .icon{color:#f90}
.card-jogo .icon{color:#0f0}
.lang-picker{position:fixed;top:8px;right:8px;display:flex;gap:4px;z-index:200}
.lang-btn{padding:4px 8px;font-size:0.75em;border:1px solid #555;border-radius:4px;
background:#222;color:#aaa;cursor:pointer;font-weight:bold}
.lang-btn.active{background:#0f0;color:#000;border-color:#0f0}
</style></head><body>
<div class='lang-picker'>
<button class='lang-btn' onclick="setLang('en')">EN</button>
<button class='lang-btn' onclick="setLang('pt')">PT</button>
</div>
<h1>gol-cam</h1>
<div class='cards'>
<a class='card card-treino' href='/training'>
<div class='icon'>&#127919;</div>
<h2 data-i18n='mode.training'>Training</h2>
<p data-i18n='mode.training_desc'>1 camera<br>Calibration &amp; practice</p>
</a>
<a class='card card-jogo' href='/match'>
<div class='icon'>&#9917;</div>
<h2 data-i18n='mode.match'>Match</h2>
<p data-i18n='mode.match_desc'>2 cameras<br>Match scoreboard</p>
</a>
</div>
<script>
var I18N={
en:{
'mode.training':'Training',
'mode.training_desc':'1 camera\nCalibration & practice',
'mode.match':'Match',
'mode.match_desc':'2 cameras\nMatch scoreboard'
},
pt:{
'mode.training':'Treino',
'mode.training_desc':'1 c\u00e2mera\nCalibra\u00e7\u00e3o e pr\u00e1tica',
'mode.match':'Jogo',
'mode.match_desc':'2 c\u00e2meras\nPlacar da partida'
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
