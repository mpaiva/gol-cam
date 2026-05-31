# gol-cam

Sistema eletrônico completo para **futebol de botão**: uma câmera que detecta gols por visão computacional, um placar de LED que mostra a contagem e um conjunto de peças impressas em 3D que une tudo. A detecção atual combina três sinais — **cor (HSV), movimento (frame-differencing)** e **bordas (Sobel)** — calibrados em conjunto a partir de uma única captura da bola colocada no gol.

## Visão geral do sistema

Três componentes coordenados em um único repositório:

| Componente | Hardware | Função |
|---|---|---|
| **Câmera** | DFRobot DFR1154 (ESP32-S3 + OV3660) | Detecta gols por visão baseada em bordas; serve o painel web e o stream MJPEG |
| **Placar** | ESP32 DevKit V1 + 4× matrizes de LED MAX7219 8×8 + 4 botões | Exibe o placar em dígitos de LED físicos; recebe eventos de gol da câmera e suporta override manual |
| **Hardware** | Caixas impressas em 3D (`hardware/`) | Trave, caixa da câmera, gabinete do placar, acessórios |

**Topologia de rede:**

```
                  ┌──────────────┐
                  │  Smartphone  │
                  │  / tablet    │
                  └───────┬──────┘
                          │ Painel HTTP
                          │
       ┌──────────────────┼──────────────────┐
       │                  │                  │
       ▼                  ▼                  ▼
┌────────────┐     ┌────────────┐     ┌────────────┐
│ Câmera A   │     │ Câmera B   │     │ Placar     │
│ DFR1154    │     │ DFR1154    │     │ ESP32 +    │
│ side=A     │     │ side=B     │     │ MAX7219    │
│ .40.90     │     │ .40.91     │     │ .40.89     │
└──────┬─────┘     └──────┬─────┘     └─────▲──────┘
       │                  │                 │
       └──────────────────┴─────────────────┘
              GET /goal?side=a|b       na detecção
              GET /goal-undo?side=a|b  na anulação VAR
```

Para uma configuração com apenas uma câmera, omita a Câmera B. O placar e a câmera trabalham apenas em modo STA (sem fallback AP); se o WiFi cair, a câmera não consegue empurrar o gol, mas os 4 botões físicos no placar continuam funcionando para placar manual.

**Esquema de IP estático sugerido** (define em `.env` via `WIFI_STATIC_IP` / `SCOREBOARD_STATIC_IP`):

| Placa | IP |
|---|---|
| Placar | `192.168.40.89` |
| Câmera A (side A) | `192.168.40.90` |
| Câmera B (side B, `BOARD_ROLE=home`) | `192.168.40.91` |

## Como funciona a detecção

Um DFR1154 (ESP32-S3 + câmera grande-angular OV3660) observa a área do gol em **RGB565 QVGA (320×240)**. Você calibra colocando a bola dentro da ROI, e o sistema aprende três assinaturas ao mesmo tempo:

1. **Bordas (Sobel)** — limiar de gradiente que descreve o silhueta da bola
2. **Cor (UV / HSV)** — par de chroma `(U, V)` médio dos pixels dentro do bbox, hue-invariante a mudanças de luma
3. **Movimento (frame-differencing)** — limiar de "piso de ruído" amostrado durante a calibração

Durante a partida, cada frame passa por três disparadores em paralelo, todos compartilhando o cooldown de 10 s:

- **Bordas** — dispara quando o silhueta aparece e desaparece, formato/tamanho dentro da bbox calibrada
- **Movimento** — conta pixels da ROI cuja luma mudou > limiar vs. o frame anterior, *após subtração da média* (DC removal) para ignorar variações uniformes de luz, e *após filtro de bbox compacto* (≤ 30 % da ROI) para rejeitar variações de luz não-uniformes ou sombras de mão
- **Cor** — conta pixels da ROI cuja chroma `(u, v)` está dentro da tolerância em torno do alvo calibrado; mesmo filtro de bbox compacto

O OR dos três triggers dispara o gol. O log serial `[detect]` mostra os três contadores (`e=`, `m=`, `c=`) e qual dispara, e o `GOAL #N` registra a fonte (`src=emc`, `src=mc`, etc.).

**Efeitos colaterais a cada gol:**
- Beep de 1,5 s @ 1 kHz pelo alto-falante I2S (placeholder — será substituído)
- LED IR pisca
- VAR captura *dois* snapshots: o frame de disparo (`/goal-snapshot`) e o frame anterior (`/goal-snapshot-prev`) — útil quando o trigger é por movimento e a bola já saiu do quadro no frame de disparo
- Push HTTP para o placar: `GET /goal?side=a|b`

**VAR (revisão de vídeo):** abrindo qualquer entrada no log de gols, o lightbox mostra os dois frames lado a lado ("Antes" / "Disparo"). Os botões "Foi Gol" mantém; "Anula" decrementa o `goalCount` da câmera **e** dispara `GET /goal-undo?side=a|b` no placar, mantendo o display em sincronia.

Recursos:
- Stream MJPEG ao vivo (a cores)
- Calibração em um clique aprende bordas + cor + ruído de movimento
- Detecção automática de gols com cooldown de 10 s
- Lightbox VAR de dois frames com sincronização do placar na anulação
- Endpoints REST para ajuste em tempo real: `/motion-delta`, `/motion-threshold`, `/motion-bbox-max`, `/color-tol`, `/color-threshold`, `/stream-skip`
- Controles de jogo: iniciar, pausar, retomar, encerrar, reiniciar
- Placar físico de LED com botões de override manual
- `/test-fire` simula um gol completo (snapshot VAR + áudio + push) para testar o pipeline sem a bola

## Futebol de Botão e o Dadinho no Rio de Janeiro

O **futebol de botão** é um esporte de mesa nascido no Brasil na década de 1930, no qual jogadores impulsionam discos circulares com o dedo para movimentar uma peça menor — a bola — por um campo em miniatura. O jogo surgiu da criatividade da garotada brasileira, que transformava botões, tampinhas de garrafa e moedas em times, jogando em mesas de jantar e tabuleiros de madeira pelo país.

### Origens

As raízes do esporte remontam às décadas de 1930 e 1940, quando Geraldo Decourt, de São Paulo, publicou as primeiras regras formais em 1930. O que começou como passatempo infantil rapidamente cresceu para uma atividade competitiva organizada. Em meados do século XX, clubes e federações já haviam se formado, e variações regionais de regras floresceram.

### Rio de Janeiro e a Regra Carioca

O Rio de Janeiro se tornou um dos centros mais importantes do esporte, desenvolvendo seu próprio estilo distinto conhecido como **Regra Carioca** ou **3 Toques**. Por essas regras, o jogador tem até três toques por jogada para avançar a bola, criando um jogo mais rápido e mais tático em comparação com outras variantes regionais.

### O Dadinho

A variante **Dadinho** representa um ramo distinto do futebol de botão. Em vez dos tradicionais discos redondos, os jogadores usam pequenos dados — tipicamente cubos amarelos de 6 mm — como peças. O nome vem de "dado" com o sufixo diminutivo "-inho".

O Dadinho foi oficialmente reconhecido pela Confederação Brasileira de Futebol de Mesa (CBFM) em 2010, dando-lhe a mesma posição institucional das modalidades tradicionais baseadas em disco. A variante tem seu próprio circuito competitivo dedicado, com o Campeonato Brasileiro de Dadinho chegando à 16ª edição em 2025.

## Hardware

### Lista de materiais (BOM)

**Placa da câmera (×1 modo single ou ×2 modo match):**
- 1× DFRobot DFR1154 (ESP32-S3 + OV3660, 16 MB flash, 8 MB PSRAM)
- 1× amplificador I2S MAX98357 + alto-falante pequeno (já inclusos na placa DFR1154)
- Cabo USB-C para alimentação e gravação

**Placa do placar (×1, opcional):**
- 1× ESP32 DevKit V1 (qualquer ESP32 com ≥8 GPIOs livres)
- 4× módulos de matriz de LED MAX7219 8×8 (cadeia FC-16), 2 por lado
- 4× push buttons (momentâneos, normalmente abertos)
- Jumpers e fonte de 5 V (USB no DevKit funciona)

**Mecânica:**
- Peças impressas em 3D a partir de `hardware/` (PLA, veja `hardware/README.md` para configurações do slicer)

### Fiação do placar (ESP32 DevKit V1)

Os macros estão inline no topo de `src_scoreboard/scoreboard.cpp`.

**Display A** (placar da esquerda, 2 módulos em cadeia, DOUT → DIN):

| MAX7219 | GPIO | Macro |
|---|---|---|
| DIN | 23 | `DIN1` |
| CLK | 18 | `CLK1` |
| CS  | 5  | `CS1`  |
| VCC | 5V (VIN) | — |
| GND | GND | — |

**Display B** (placar da direita, 2 módulos em cadeia):

| MAX7219 | GPIO | Macro |
|---|---|---|
| DIN | 19 | `DIN2` |
| CLK | 21 | `CLK2` |
| CS  | 22 | `CS2`  |

**Botões** (cada um do GPIO ao GND, `INPUT_PULLUP` — sem resistores externos):

| Botão | GPIO | Macro | Ação |
|---|---|---|---|
| UP A | 32 | `UP1` | Incrementa lado A |
| DW A | 33 | `DW1` | Zera lado A |
| UP B | 25 | `UP2` | Incrementa lado B |
| DW B | 26 | `DW2` | Zera lado B |

### Pinagem da câmera

Veja `include/pins.h` — pré-configurada para a placa DFR1154. Não precisa fazer fiação.

### Peças impressas em 3D

Veja `hardware/README.md` para o catálogo de peças (trave V4, caixa da câmera V5.2, gabinete do placar, suporte de alto-falante, grade de cooler) com recomendações de fatiamento.

## Primeiros passos

### Pré-requisitos

- [Git](https://git-scm.com/downloads)
- [Python 3](https://www.python.org/downloads/) (marque "Add to PATH" no instalador do Windows)
- Cabo USB-C para o DFR1154; cabo USB para o ESP32 DevKit
- **Apenas Windows:** [driver USB CP210x](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) (pode ser necessário para comunicação serial)

### Passo 1 — Clonar o repositório

```bash
git clone https://github.com/mpaiva/gol-cam.git
cd gol-cam
```

### Passo 2 — Criar um ambiente virtual Python

**Mac (Terminal):**
```bash
python3 -m venv .venv
source .venv/bin/activate
pip install platformio
```

**Windows (PowerShell):**
```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install platformio
```

> Se o PowerShell bloquear o script, rode `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` antes.

### Passo 3 — Configurar `.env`

Crie um arquivo `.env` na raiz do projeto (ignorado pelo git):

```ini
# Obrigatório — mesma WiFi para câmera e placar
WIFI_SSID=nome-da-sua-rede
WIFI_PASSWORD=sua-senha

# IP do placar — todas as câmeras empurram gols para cá (recomendado: estático)
SCOREBOARD_IP=192.168.40.89
SCOREBOARD_STATIC_IP=192.168.40.89
SCOREBOARD_GATEWAY=192.168.40.1
SCOREBOARD_SUBNET=255.255.255.0

# Per-board (definir antes de gravar cada câmera; valores comentados são exemplos)
# Câmera A (side A — padrão):  WIFI_STATIC_IP=192.168.40.90, BOARD_ROLE não definido
# Câmera B (side B):            WIFI_STATIC_IP=192.168.40.91, BOARD_ROLE=home
WIFI_GATEWAY=192.168.40.1
WIFI_SUBNET=255.255.255.0
# WIFI_STATIC_IP=192.168.40.90
# BOARD_ROLE=home              # home → side B; ausente → side A
# SCOREBOARD_SIDE=b            # override explícito (a|b), tem prioridade sobre BOARD_ROLE
```

`load_env.py` lê esses valores e injeta como `-D` defines de build, então são compilados no firmware. Como `WIFI_STATIC_IP` e `BOARD_ROLE` precisam ser diferentes em cada câmera, ajuste-os antes de cada gravação.

### Passo 4 — Compilar e gravar

Dois ambientes PlatformIO convivem no mesmo projeto:

```bash
# Câmera (padrão — igual a `pio run`)
pio run -e dfr1154 -t upload

# Placar
pio run -e placar -t upload
```

O ambiente padrão é `dfr1154`, então um `pio run` sem parâmetros compila a câmera. Grave uma placa por vez, ajustando o `.env` entre gravações se quiser valores diferentes de `BOARD_ROLE` ou IP estático.

### Passo 5 — Descobrir o IP e abrir o painel

```bash
pio device monitor   # baud 115200, procure "Dashboard: http://..." na câmera
```

Abra o IP da câmera no navegador. Você verá o seletor de modo:
- **Training (Treino)** — uma câmera, calibração e prática
- **Match (Partida)** — duas câmeras + placar opcional; configure os IPs na primeira carga (salvos no `localStorage`)

O placar serve sua própria interface simples no IP dele (incremento/reset manual, mais o status atual da conexão).

## Operação

### Fluxo padrão de partida

1. **Calibre cada câmera** — coloque o dadinho na frente do gol, clique em "Calibrate"
2. **Inicie o jogo** — o painel de partida mostra os feeds ao vivo e o placar unificado
3. **Jogue** — a câmera detecta gols, toca som de comemoração, envia o evento para o placar e incrementa o placar de LED
4. **Revisão VAR** — clique em qualquer entrada de gol para confirmar ou anular (gols anulados subtraem do lado certo)
5. **Pausar / retomar / reiniciar / encerrar** — controles atingem todas as placas simultaneamente

### Placar autônomo (sem câmera)

Mesmo com as câmeras offline ou desligadas, o placar continua funcionando:
- Botões físicos sempre funcionam (UP A/B, RESET A/B)
- A interface web em `http://192.168.40.89/` espelha os botões + adiciona `/api/reset`
- Endpoints REST disponíveis para o placar: `/goal?side=a|b` (+1), `/goal-undo?side=a|b` (−1, com piso em 0), `/a+`, `/b+`, `/az`, `/bz`, `/reset`, `/api/reset`, `/status`

## Estrutura do repositório

```
gol-cam/
├── platformio.ini              # Dois ambientes: dfr1154 + placar
├── partitions.csv              # Layout de 16 MB de flash para a câmera
├── load_env.py                 # .env → -D defines de build
├── .env                        # WiFi + IP estático + role (ignorado pelo git)
├── include/
│   ├── camera.h, pins.h        # Câmera (DFR1154)
│   ├── frame_store.h           # Buffer JPEG thread-safe para o stream MJPEG
│   └── goal_detector.h         # Struct GoalDetector (contadores, FPS)
├── src/
│   ├── main.cpp                # Câmera: loop de detecção (bordas + movimento + cor)
│   ├── web_stream.cpp          # Servidor HTTP da câmera (esp_http_server)
│   ├── mode_select.h           # HTML do seletor Training / Match
│   ├── training_dashboard.h    # HTML/JS do painel de treino (uma câmera)
│   └── match_dashboard.h       # HTML/JS do painel de partida (duas câmeras)
├── src_scoreboard/
│   └── scoreboard.cpp          # Placar: firmware monolítico (WiFi + MAX7219 + HTTP + botões)
├── hardware/                   # Caixas impressas em 3D
├── .about/                     # Proposta de valor, feature requests
├── .plans/                     # Planos de implementação, notas de sessão
└── .reports/                   # Pesquisa, auditoria de UX, screenshots
```

## Licença

MIT
