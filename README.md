# gol-cam

Sistema eletrônico completo para **futebol de botão**: uma câmera que detecta gols por visão computacional, um placar de LED que mostra a contagem e um conjunto de peças impressas em 3D que une tudo. Construído para a variante **Dadinho** — o pequeno dado de 4-6 mm usado como bola.

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
                          │ Painel HTTP (navegador faz polling)
                          │
       ┌──────────────────┼──────────────────┐
       │                  │                  │
       ▼                  ▼                  ▼
┌────────────┐     ┌────────────┐     ┌────────────┐
│ Câmera A   │     │ Câmera B   │     │ Placar     │
│ DFR1154    │     │ DFR1154    │     │ ESP32 +    │
│ side=A     │     │ side=B     │     │ MAX7219    │
└──────┬─────┘     └──────┬─────┘     └─────▲──────┘
       │                  │                 │
       └──────────────────┴─────────────────┘
              POST /goal {"side":"A|B"}   na detecção
              (fallback de polling do placar @500 ms via /status)
```

Para uma configuração com apenas uma câmera, omita a Câmera B e o `PEER_IP` do placar. O placar também pode operar de forma **autônoma** — se a conexão WiFi falhar, ele volta para o modo AP `PLACAR_WIFI` (192.168.4.1), e os 4 botões físicos continuam funcionando.

## Como funciona a detecção

Um DFR1154 (ESP32-S3 + câmera grande-angular OV3660) observa a área do gol. Você calibra colocando o dadinho no campo de visão, e o sistema aprende a assinatura de bordas dele. Durante a partida, ele detecta o cubo entrando no gol, captura uma foto, toca um som de comemoração pelo alto-falante I2S, envia o evento de gol para o placar e mantém a contagem — tudo via WiFi.

Recursos:
- Stream MJPEG ao vivo da câmera
- Calibração em um clique com feedback visual
- Detecção automática de gols com cooldown de 10 segundos
- Registro de fotos dos gols com horário
- Sistema de revisão tipo VAR (árbitro de vídeo)
- Controles de jogo: pausar, retomar, reiniciar, encerrar partida
- Placar físico de LED com botões de override manual
- Operação autônoma do placar quando a câmera está offline

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

**Display A** (placar da esquerda, 2 módulos em cadeia, DOUT → DIN):

| MAX7219 | GPIO | Macro (`include/pins_placar.h`) |
|---|---|---|
| DIN | 23 | `PLACAR_DIN_A` |
| CLK | 18 | `PLACAR_CLK_A` |
| CS  | 5  | `PLACAR_CS_A`  |
| VCC | 5V (VIN) | — |
| GND | GND | — |

**Display B** (placar da direita, 2 módulos em cadeia):

| MAX7219 | GPIO | Macro |
|---|---|---|
| DIN | 19 | `PLACAR_DIN_B` |
| CLK | 21 | `PLACAR_CLK_B` |
| CS  | 22 | `PLACAR_CS_B`  |

**Botões** (cada um do GPIO ao GND, `INPUT_PULLUP` — sem resistores externos):

| Botão | GPIO | Macro | Ação |
|---|---|---|---|
| UP A | 32 | `PLACAR_BTN_UP_A` | Incrementa lado A |
| DW A | 33 | `PLACAR_BTN_DW_A` | Zera lado A |
| UP B | 25 | `PLACAR_BTN_UP_B` | Incrementa lado B |
| DW B | 26 | `PLACAR_BTN_DW_B` | Zera lado B |

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

# Opcional — IPs estáticos ajudam no modo match
WIFI_STATIC_IP=192.168.0.100
WIFI_GATEWAY=192.168.0.1
WIFI_SUBNET=255.255.255.0

# Opcional — tags de papel para o modo match
BOARD_ROLE=goal_a            # ou goal_b, single, scoreboard
PEER_IP=192.168.0.101        # IP da outra câmera (modo match)
SCOREBOARD_IP=192.168.0.110  # IP do placar (câmera envia gols para cá)
CAMERA_IP=192.168.0.100      # IP da câmera (placar faz polling como fallback)
```

`load_env.py` lê esses valores e injeta como `-D` defines de build, então são compilados no firmware. Placas diferentes precisam de valores `.env` diferentes — grave uma, altere o `.env`, grave a próxima.

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

### Fallback do placar (sem câmera ou câmera offline)

Se a câmera estiver inacessível, o placar continua funcionando:
- WiFi STA falhou → modo AP `PLACAR_WIFI` / `12345678`, abra `http://192.168.4.1`
- Botões físicos sempre funcionam (UP / RESET por lado)
- A interface web manual espelha os botões

## Estrutura do repositório

```
gol-cam/
├── platformio.ini              # Dois ambientes: dfr1154 + placar
├── partitions.csv              # Layout de 16 MB de flash para a câmera
├── load_env.py                 # .env → -D defines de build
├── .env                        # WiFi + overrides de papel/IP (ignorado pelo git)
├── include/
│   ├── camera.h, pins.h        # Câmera (DFR1154)
│   ├── frame_store.h, goal_detector.h, gol_sound*.h
│   ├── pins_placar.h           # GPIOs do placar
│   ├── placar_display.h        # Driver MAX7219
│   ├── placar_buttons.h        # Leitor de botões com debounce
│   └── placar_web.h            # Contrato do servidor HTTP do placar
├── src/
│   ├── main.cpp                # Entry da câmera + loop de detecção + push de gol
│   ├── web_stream.cpp          # Servidor HTTP da câmera (esp_http_server)
│   ├── mode_select.h, training_dashboard.h, match_dashboard.h
│   ├── main_placar.cpp         # Entry do placar + WiFi/AP + polling da câmera
│   ├── placar_display.cpp      # Renderização MAX7219
│   ├── placar_buttons.cpp      # Debounce dos botões
│   └── placar_web.cpp          # WebServer do placar + /goal /sync /status
├── hardware/                   # Caixas impressas em 3D
├── .about/                     # Proposta de valor, feature requests
├── .plans/                     # Planos de implementação, notas de sessão
└── .reports/                   # Pesquisa, auditoria de UX, screenshots
```

## Licença

MIT
