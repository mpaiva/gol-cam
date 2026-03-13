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
</style></head><body>
<h1>gol-cam</h1>
<div class='cards'>
<a class='card card-treino' href='/training'>
<div class='icon'>&#127919;</div>
<h2>Treino</h2>
<p>1 camera<br>Calibration &amp; practice</p>
</a>
<a class='card card-jogo' href='/match'>
<div class='icon'>&#9917;</div>
<h2>Jogo</h2>
<p>2 cameras<br>Match scoreboard</p>
</a>
</div>
</body></html>
)rawliteral";
