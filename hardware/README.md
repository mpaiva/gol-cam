# Hardware — Modelos 3D imprimíveis

Peças mecânicas do sistema gol-cam, importadas do projeto auxiliar `GOOOL Elatronico` em 2026-05-24. Os modelos do placar virão do `Placar Eletronico` na Fase 2 do plano de consolidação (ver `.plans/consolidation-plan.md`).

Todos os arquivos `.3mf` são projetos completos do Bambu Studio / PrusaSlicer (incluem orientação, suportes e settings recomendados). Os `.stl` são as malhas cruas — use-os se preferir reimportar em outro slicer.

---

## Layout

```
hardware/
├── trave/                   # Estrutura da trave eletrônica (atual: V4)
├── caixa-camera/            # Encaixe da câmera DFR1154 (atual: V5.2)
├── placar/                  # Caixa do placar ESP32 + MAX7219 (vem na Fase 2)
├── acessorios/              # Fixação do autofalante, grade do cooler
└── arquivos-historicos/     # Versões antigas — referência apenas
```

---

## `trave/` — Trave eletrônica

A trave é a estrutura principal: gol de futebol de botão de 15 cm × 4 cm que abriga a câmera no teto e a "boca" abre a região do gol.

| Arquivo | Função | Versão |
|---|---|---|
| `Trave Eletronica+teto V4.3mf` | Trave + teto integrados, projeto Bambu | **Atual** |
| `Trave Eletronica+teto V4.stl` | Mesma malha em STL | **Atual** |
| `Boca da Trave.3mf` | Anteparo frontal (boca do gol) | **Atual** |
| `Boca da Trave.stl` | Mesma malha em STL | **Atual** |

**Recomendação de impressão:** PLA, camada 0.2 mm, infill 20–30%, com suportes na parte de baixo do teto.

---

## `caixa-camera/` — Encaixe da câmera DFR1154

Caixa que envolve a placa DFR1154 (42 × 42 mm, ESP32-S3 + OV3660) e fixa dentro do teto da trave. A versão atual é a **V5.2**, com base separada para facilitar o acesso ao conector USB-C.

| Arquivo | Função | Versão |
|---|---|---|
| `Caixa Camera Golll V5.2.3mf` | Corpo da caixa (V5.2) | **Atual** |
| `Base Caixa Camera Golll V5.2.stl` | Base/tampa de fundo (V5.2) | **Atual** |
| `Caixa Camera Golll V5.1.3mf` | Corpo (V5.1, fallback) | Ativa |
| `Caixa Camera Golll V5.1.stl` | Malha STL (V5.1) | Ativa |

**Recomendação de impressão:** PLA, camada 0.16 mm para detalhe na abertura da lente, sem suportes (geometria foi planejada para imprimir orientada).

---

## `placar/` — Caixa do placar LED

Caixa do placar físico (ESP32 DevKit V1 + 4× matrizes LED MAX7219 8×8 + 4 botões push). Modelos serão importados do projeto `Placar Eletronico` durante o subtree merge (Fase 2 do plano).

> Esperar até a Fase 3 concluir — após isso esta pasta conterá `Placar ESP 32 Wifi.3mf` e `New Scene.3mf`.

---

## `acessorios/` — Peças auxiliares

| Arquivo | Função |
|---|---|
| `Est. Fix. Autofalante 1.3mf` | Suporte de fixação do autofalante (saída de áudio I2S/MAX98357 do gol-cam) |
| `grade_para_cooler_colmeia_com_borda_85mm.3mf` | Grade tipo colmeia 85 mm, ventilação interna |

---

## `arquivos-historicos/` — Referência apenas

Versões anteriores das caixas de câmera e traves. **Não imprima a partir destes arquivos** sem antes confirmar que substituem alguma peça nova. Mantidos para rastrear evolução do design.

- Caixas V1, V2, V3, V3.1, V3.2, V3.3 (substituídas pela V5.x)
- Trave V3 e versões `Trave Desmontada`, `Trave Eletronica+teto` (sem versão), `Trave Eletronica` (sem teto) — substituídas pela V4

---

## Origem

- Pasta `trave/`, `caixa-camera/`, `acessorios/`, `arquivos-historicos/`: projeto `C:\_PROJETOS\GOOOL Elatronico` (sem git, importado por cópia em 2026-05-24)
- Pasta `placar/`: projeto `C:\_PROJETOS\Placar Eletronico` (importado via `git subtree merge`, preservando histórico)
