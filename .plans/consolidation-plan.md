# Plano de Consolidação: gol-cam + Placar Eletronico + GOOOL Elatronico

**Data:** 2026-05-24
**Branch sugerido:** `consolidation`
**Decisões aprovadas:**
- Arquitetura: monorepo PlatformIO com 2 environments (dois ESP32 separados, coordenados via WiFi)
- Modelos 3D: pasta `hardware/` versionada diretamente no git
- Histórico git do Placar Eletronico: preservado via `git subtree merge`

---

## 1. Visão geral

Os três projetos formam um único produto — câmera detectora + placar físico + estrutura impressa em 3D. Após a consolidação, o repositório `gol-cam` passa a ser a fonte única de verdade para firmware, modelos mecânicos e documentação. O Placar Eletronico continua sendo um segundo board físico (ESP32 DevKit com 4× MAX7219), mas seu firmware vive ao lado do firmware da câmera, compartilhando `.env`, convenções de build e protocolo de rede.

**Estrutura final pretendida:**

```
gol-cam/
├── platformio.ini              # 2 environments: dfr1154 (câmera) + placar (display)
├── partitions.csv
├── load_env.py
├── .env                        # WIFI_SSID, WIFI_PASSWORD, BOARD_ROLE, PEER_IP
├── include/                    # Headers compartilhados (pins, frame_store, goal_detector)
│   ├── pins_dfr1154.h          # ex-pins.h
│   ├── pins_placar.h           # NOVO: pinos do Placar (DIN/CLK/CS + botões)
│   └── ...
├── src/
│   ├── main.cpp                # entry point da câmera (#ifdef BOARD_ROLE_CAMERA)
│   ├── main_placar.cpp         # NOVO: entry point do placar
│   ├── web_stream.cpp
│   ├── placar_display.cpp      # NOVO: driver MAX7219 + fonte de dígitos
│   ├── placar_buttons.cpp      # NOVO: leitura debounce dos 4 botões
│   ├── placar_web.cpp          # NOVO: web server do placar + sync com gol-cam
│   └── *_dashboard.h           # HTMLs existentes
├── hardware/                   # NOVO — modelos 3D
│   ├── README.md               # inventário e instruções de impressão
│   ├── trave/                  # Trave Eletronica V3/V4 + Boca da Trave
│   ├── caixa-camera/           # Caixa Camera Goll V2 → V5.2
│   ├── placar/                 # Placar ESP 32 Wifi + New Scene
│   ├── acessorios/             # Suporte autofalante, grade cooler
│   └── arquivos-historicos/    # Versões antigas (V2, V3.1, V3.2)
├── .about/                     # mantido
├── .plans/                     # mantido (este arquivo + sessões)
├── .reports/                   # mantido
├── CLAUDE.md                   # atualizar (novo board, novo wiring)
└── README.md                   # reescrever (escopo expandido)
```

---

## 2. Fases de execução

### Fase 0 — Preparação (10 min)

- [ ] Criar branch `consolidation` a partir de `master`
- [ ] Commitar `.claude/` que está untracked (ou adicionar ao `.gitignore` conforme política)
- [ ] Confirmar que `pio run` ainda passa com o código atual (baseline)

### Fase 1 — Importar modelos 3D do GOOOL Elatronico (15 min)

Como `GOOOL Elatronico` **não tem git**, é uma cópia simples seguida de organização.

- [ ] Criar `hardware/{trave,caixa-camera,placar,acessorios,arquivos-historicos}/`
- [ ] Copiar arquivos por categoria:
  - **`hardware/trave/`** — `Trave Eletronica+teto V4.{3mf,stl}`, `Trave Eletronica+teto V3.{3mf,stl}`, `Boca da Trave.{3mf,stl}`
  - **`hardware/caixa-camera/`** — `Caixa Camera Golll V5.2.{3mf,stl}`, `Base Caixa Camera Golll V5.2.stl`, `Caixa Camera Golll V5.1.{3mf,stl}`
  - **`hardware/placar/`** — `Placar ESP 32 Wifi.3mf` + `New Scene.3mf` (de Placar Eletronico)
  - **`hardware/acessorios/`** — `Est. Fix. Autofalante 1.3mf`, `grade_para_cooler_colmeia_com_borda_85mm.3mf`
  - **`hardware/arquivos-historicos/`** — todas as V2, V3, V3.1, V3.2, "Trave Desmontada", etc.
- [ ] Escrever `hardware/README.md` com:
  - Foto/lista de cada peça
  - Versão recomendada para impressão atual (V5.2)
  - Material, slicer settings, tempo estimado se conhecido
  - Notas: peças marcadas como `arquivos-historicos/` são referência apenas
- [ ] Commit: `hardware: import 3D models from GOOOL Elatronico project`

**Tamanho estimado adicionado:** ~5–8 MB de arquivos binários. Aceitável sem LFS (decisão aprovada).

### Fase 2 — Importar firmware do Placar com histórico (20 min)

Usar `git subtree merge` para preservar os 2 commits originais (`d4b5221` initial + `ceee6dc` README).

```bash
# Dentro de gol-cam
git remote add placar "C:/_PROJETOS/Placar Eletronico"
git fetch placar
git merge -s ours --no-commit --allow-unrelated-histories placar/master
git read-tree --prefix=_import_placar/ -u placar/master
git commit -m "import: subtree merge Placar Eletronico history"
git remote remove placar
```

Resultado: pasta temporária `_import_placar/` contendo o sketch `.ino`, README, screenshots e modelos 3D do Placar, **com histórico preservado**. Os modelos 3D do `_import_placar/` serão movidos para `hardware/` na Fase 3.

- [ ] Executar subtree merge acima
- [ ] Verificar `git log --oneline --all --graph` mostra os 2 commits do Placar

### Fase 3 — Reorganizar arquivos importados (15 min)

- [ ] Mover `_import_placar/Screenshot*.png` → `.reports/screenshots/placar-*.png`
- [ ] Mover `_import_placar/Pinos ESP32.jpeg`, `Ligacao dos Pinos.png` → `.reports/screenshots/placar-wiring-*.{jpeg,png}`
- [ ] Mover `_import_placar/New Scene.3mf` → `hardware/placar/` (se ainda não copiado na Fase 1)
- [ ] Mover `_import_placar/README.md` → preservar conteúdo no novo `README.md` consolidado (Fase 7)
- [ ] Mover `_import_placar/Placar_Eletronico_Wi-Fi/Placar_Eletronico_Wi-Fi.ino` → `src/_legacy_placar.ino` (referência durante a portabilidade — removido ao fim da Fase 5)
- [ ] Apagar pasta `_import_placar/`
- [ ] Commit: `consolidation: reorganize imported placar files into hardware/, reports/, src/`

### Fase 4 — Configurar build dual-environment (30 min)

Reescrever `platformio.ini` para suportar dois targets, sem quebrar o build atual da câmera.

```ini
[platformio]
default_envs = dfr1154

[env]
framework = arduino
extra_scripts = pre:load_env.py
monitor_speed = 115200
build_flags =
    -DWIFI_SSID=\"${sysenv.WIFI_SSID}\"
    -DWIFI_PASSWORD=\"${sysenv.WIFI_PASSWORD}\"

[env:dfr1154]
platform = espressif32
board = esp32-s3-devkitc-1
board_build.flash_size = 16MB
board_build.partitions = partitions.csv
board_build.arduino.memory_type = qio_opi
build_flags =
    ${env.build_flags}
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DBOARD_ROLE_CAMERA=1
build_src_filter =
    +<*>
    -<main_placar.cpp>
    -<placar_*.cpp>
    -<_legacy_placar.ino>
upload_speed = 921600

[env:placar]
platform = espressif32
board = esp32dev          ; ESP32 DevKit V1 genérico
build_flags =
    ${env.build_flags}
    -DBOARD_ROLE_PLACAR=1
lib_deps =
    majicdesigns/MD_MAX72XX@^3.5.0
build_src_filter =
    +<main_placar.cpp>
    +<placar_*.cpp>
    -<main.cpp>
    -<web_stream.cpp>
    -<_legacy_placar.ino>
```

- [ ] Reescrever `platformio.ini` com as duas environments
- [ ] Atualizar `load_env.py` se necessário (provavelmente já é genérico — verificar)
- [ ] Adicionar `BOARD_ROLE=scoreboard` e `CAMERA_IP=<ip-do-gol-cam>` como variáveis suportadas no `.env`
- [ ] Validar `pio run -e dfr1154` ainda compila (sem regressão)
- [ ] Commit: `build: add dual-environment platformio.ini for dfr1154 + placar`

### Fase 5 — Portar o sketch do Placar para C++/PlatformIO (60–90 min)

O `.ino` original é Arduino IDE-style. Portar para layout PlatformIO (`.cpp` + headers), mantendo a lógica intacta na primeira passada.

**Arquivos novos a criar:**

- [ ] **`include/pins_placar.h`** — `#define`s de DIN1/CLK1/CS1, DIN2/CLK2/CS2, UP1/DW1/UP2/DW2 (cópia direta do `.ino`)
- [ ] **`include/placar_display.h`** + **`src/placar_display.cpp`** — classe `PlacarDisplay` encapsulando os 2 `MD_MAX72XX`, função `mostrar(int valor)`, fonte de dígitos `digitos[10][8]`, rotação 90°
- [ ] **`include/placar_buttons.h`** + **`src/placar_buttons.cpp`** — leitura debounce de UP/DW por lado, callbacks `onUpA/onDownA/onUpB/onDownB`
- [ ] **`src/placar_web.cpp`** — `WebServer` na porta 80, endpoints `/`, `/a+`, `/b+`, `/az`, `/bz`, `/reset`, **e novos endpoints para integração com gol-cam:** `/sync` (POST com `{a, b}`), `/status` (GET retorna placar atual + role)
- [ ] **`src/main_placar.cpp`** — `setup()` + `loop()` cooperativo: inicializa display, botões, WiFi (modo STA conectando ao mesmo SSID do `.env`, com fallback AP), web server, e **task de polling para o gol-cam** que escuta eventos de gol

**Lógica nova (integração com gol-cam):**

A câmera (`gol-cam`) já publica `/status` com contagem de gols e `lastGoalTimeMs`. O placar fará polling a cada 500 ms desse endpoint. Quando detectar incremento na contagem do lado correspondente, **anima o display** (pisca antes de atualizar o número). Botões físicos continuam funcionando como override manual.

Protocolo proposto:

```
GET http://<placar-ip>/status  →  {"role":"scoreboard","a":3,"b":1,"peer":"<camera-ip>"}
POST http://<placar-ip>/sync   →  body: {"a":3,"b":1}  (idempotente)
GET http://<camera-ip>/status  →  já existe; placar lê goalCount e identifica side via BOARD_ROLE
```

- [ ] Implementar `PlacarDisplay`
- [ ] Implementar `PlacarButtons`
- [ ] Implementar `placar_web` com endpoints REST
- [ ] Implementar polling-loop em `main_placar.cpp` (cliente HTTP simples para `/status` da câmera)
- [ ] Adicionar suporte fallback: se câmera offline, placar opera standalone
- [ ] Compilar `pio run -e placar`, corrigir erros
- [ ] Deletar `src/_legacy_placar.ino`
- [ ] Commit: `placar: port Arduino sketch to PlatformIO C++ with gol-cam sync`

### Fase 6 — Atualizar gol-cam para "publicar" gols (20 min)

A câmera precisa expor o lado (A ou B) para que o placar saiba qual contador incrementar. Já existe `BOARD_ROLE` e `PEER_IP` no schema do `.env`.

- [ ] Adicionar campo `"side":"A"` ou `"side":"B"` ao JSON de `/status` em `web_stream.cpp`, derivado de `BOARD_ROLE` (env: `BOARD_ROLE=goal_a` ou `goal_b`)
- [ ] **Opcional (melhor que polling):** Adicionar push HTTP do gol-cam para o placar no momento da detecção de gol — endpoint `POST /sync` no placar. Requer `SCOREBOARD_IP` no `.env` da câmera. Mantém polling como fallback.
- [ ] Atualizar `match_dashboard.h` para descobrir o IP do placar via `PEER_IP` e mostrar status de conexão do placar físico
- [ ] Commit: `gol-cam: publish side + push goals to scoreboard board`

### Fase 7 — Documentação consolidada (30 min)

- [ ] Reescrever **`README.md`** consolidando:
  - Visão geral do sistema (3 componentes: câmera, placar, estrutura 3D)
  - BOM atualizado: 1× DFR1154 + 1× ESP32 DevKit + 4× MAX7219 + 4× botões + autofalante + peças impressas
  - Diagrama de rede: câmera ↔ placar ↔ celular dashboard
  - Build: `pio run -e dfr1154` vs `pio run -e placar`
  - Wiring do placar (importar tabelas do README antigo do Placar Eletronico)
  - Link para `hardware/README.md` para impressão 3D
- [ ] Atualizar **`CLAUDE.md`**:
  - Seção "Hardware" → cobrir os dois boards
  - Seção "Build & Deploy" → comandos `-e dfr1154` e `-e placar`
  - Seção "Architecture" → adicionar protocolo de sync câmera→placar
  - Seção "Key modules" → adicionar `placar_*.cpp` e `pins_placar.h`
- [ ] Adicionar `.about/feature-requests.md` com novas features:
  - **F6: Placar físico LED sincronizado** (estado: Implemented)
  - **F7: Operação standalone do placar** (estado: Implemented — fallback se câmera offline)
- [ ] Criar `.plans/session-2026-05-24.md` resumindo a consolidação
- [ ] Commit: `docs: consolidate README, CLAUDE.md, and feature-requests`

### Fase 8 — Validação (depende de hardware)

- [ ] Compilar ambas as environments sem warnings
- [ ] Flashar `dfr1154` no DFR1154 — verificar dashboard treino + match ainda funcionam
- [ ] Flashar `placar` no ESP32 DevKit — verificar:
  - [ ] Displays MAX7219 mostram 0×0 ao boot
  - [ ] Botões físicos incrementam corretamente
  - [ ] Conecta ao mesmo WiFi do `.env`
  - [ ] `GET /status` no placar responde
- [ ] Teste integrado: gol detectado na câmera → placar físico incrementa lado correto em <1s
- [ ] Teste fallback: desligar câmera, confirmar que botões físicos do placar continuam operando
- [ ] Commit final: tag `v1.0-consolidated`

### Fase 9 — Limpeza dos repositórios antigos (decisão do usuário)

Depois que tudo estiver funcionando e commitado em `gol-cam`:

- [ ] Confirmar com o usuário que pode arquivar `C:\_PROJETOS\Placar Eletronico\` (mover para `_arquivos/` ou apagar)
- [ ] Confirmar com o usuário que pode arquivar `C:\_PROJETOS\GOOOL Elatronico\`
- [ ] Não fazer essas remoções sem aprovação explícita — o repo do Placar tem `.git` próprio que será perdido permanentemente

---

## 3. Riscos e mitigações

| Risco | Mitigação |
|---|---|
| Pinos do DFR1154 conflitarem se um dia quisermos fundir tudo num só board | Manter como duas environments isoladas; não há dependência cruzada de pinos |
| `MD_MAX72XX` lib do PlatformIO ter API diferente do Arduino IDE | `lib_deps` fixa versão `^3.5.0`; testar compilação cedo na Fase 5 |
| Polling de 500 ms saturar WiFi com várias câmeras (modo match com 2 câmeras + placar) | Usar push do gol-cam → placar (Fase 6 opcional); polling fica como fallback |
| Histórico do subtree merge poluir `git log` da câmera | Aceitável — apenas 2 commits adicionais com prefixo `_import_placar/`; sumiço da pasta deixa rastro mas histórico limpo |
| Modelos 3D inflarem o repo (~8 MB) | Aceitável conforme decisão; reavaliar com Git LFS se passar de 50 MB |
| `.ino` original usa `delay()` e padrões Arduino — pode quebrar cooperação com web server no placar | Refatorar para loop não-bloqueante na Fase 5; já é o estilo do `gol-cam/main.cpp` |

---

## 4. Não-objetivos desta consolidação

- **Não** reescrever a detecção de gol nem mexer no `goal_detector.h`
- **Não** modernizar a UI do placar (mantém botões grandes do `.ino`, só adapta para sincronia)
- **Não** unir os dois boards num único ESP32 (decisão explícita)
- **Não** migrar para FreeRTOS multitarefa — manter loop cooperativo em ambos os boards
- **Não** adicionar BLE, OTA, MQTT — fora do escopo

---

## 5. Estimativa de tempo

| Fase | Tempo |
|---|---|
| 0. Preparação | 10 min |
| 1. Importar modelos 3D | 15 min |
| 2. Subtree merge do Placar | 20 min |
| 3. Reorganizar arquivos | 15 min |
| 4. Dual-environment build | 30 min |
| 5. Portar firmware Placar | 60–90 min |
| 6. Gol-cam publica gols | 20 min |
| 7. Documentação | 30 min |
| 8. Validação em hardware | depende |
| **Total (sem Fase 8)** | **≈ 3h30–4h** |

Fases 0–4 podem ser feitas numa sessão sem necessidade do hardware do Placar conectado.
Fases 5–8 idealmente com o ESP32 DevKit + MAX7219 montados para teste iterativo.
