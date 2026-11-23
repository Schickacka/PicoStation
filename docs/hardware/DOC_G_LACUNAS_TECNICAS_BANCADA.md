# Documento G — Registro de Lacunas Técnicas (`[DESCONHECIDO]` / `[INFERIDO]`) & Guia de Inspeção em Bancada (`PU-8 OC/SCEI` & `PU-18`)

> Este documento lista **todas** as informações que não puderam ser confirmadas em nível documental primário e fornece um roteiro prático de **medição com multímetro, osciloscópio/analisador lógico e fotografia macro** antes de energizar o conjunto PS1 + PicoStation.

---

## 1. Registro Consolidado de Itens `[DESCONHECIDO]` e `[INFERIDO]`

| ID | Área / Revisão | Item Não Verificado ou Conflitante | Classificação | Impacto no PicoStation |
|---|---|---|---|---|
| **G-01** | `PU-8` (`IC309 CXD2510Q`) | `psx-spx` lista `IC309.33 = VDD 5V` e `23/73 = VDDa/VDD`; datasheet pede `VDD - AVDD` max `±0.3V`. Não está confirmado em placa física se `IC309.73` e I/Os (`XLAT, CMD, SQCK, SENS, SCOR, SQSO`) operam em `3.5V` (como o `IC304 HC05`) ou se há `5.0V` acoplado perto de `IC309`. | `[DESCONHECIDO]` / `[CORROBORADO]` | **Crítico (Segurança RP2040):** RP2040 suporta no máximo `3.80V` ligado (`IOVDD+0.5V`) e `0.5V` desligado. Medir tensão DC antes de ligar GPIO! |
| **G-02** | `PU-8 OC` (console de bancada) vs. `PU-18` | Confirmar quais osciladores foram removidos no console `PU-8 OC` (`X101 67.737MHz`, `X302 4.000MHz`, ou ambos) e onde entra `GPIO 21` (`APLL`). Lembrar que `src/` **não gera** o PWM `GPIO 3` (`3.988/4.238 MHz`) presente no `RC5`! | `[DESCONHECIDO]` na placa física | **Crítico (Boot):** Se `X302` (`PU-8`) ou `IC701.68` (`PU-18`) estiver desconectado/removido e não houver `GPIO 3` no firmware compilado, o `HC05` não dá clock e não inicializa. |
| **G-03** | `EARLY-PU-8`, `LATE-PU-8`, `PU-18` | Designadores `Rnnn` e valores exatos dos resistores removidos: (a) top-side `1 resistor + 1 filtro` próx. ao `X101` em `PU-7`/`PU-8`; (b) bottom-side `3 resistores` em `PU-7`/`PU-8`; (c) bottom-side `1 resistor` próx. ao `V67.737 KDS813` na `PU-18`; e rede passiva em `WFCK`/`APTL`. | `[DESCONHECIDO]` (valores/refs PCB); `[INFERIDO]` que os 3R bottom são `DA15/DA16/LRCK` | Necessário mapear via foto macro + multímetro para documentação 100% reproduzível (especialmente em instalações fly-wire). |
| **G-04** | `PU-8` & `PU-18` | Numeração de test points `CLnnn` na `PU-8` e `PU-18` (`psx-spx` só detalhou `CLnnn` para `PU-23`/`PM-41`, além de `CL316 = IC304.21 TEST1`). Ponto exato de chegada do fio `APLL`, `IO35` (`EARLY-PU-8`), `IO16` (`LATE-PU-8`) e `LIDSW` (`PU-7`). | `[DESCONHECIDO]` | Fotos em alta resolução permitem amarrar cada pad do QSB ao pino exato de CI na `PU-8` e `PU-18`. |
| **G-05** | `PU-8` (`EARLY`/`LATE`) & `PU-18` | Ponto exato de injeção `SCEX_DATA` (`GPIO 4`): `PU-18` cita `IC706 uPC5023GR-118` (`pin 1 DATA`, `pins 7/10 GATE`), porém na `PU-8` (`IC702 CXA1782BR` / op-amp `082B`/`2903` vs `IC304`) apenas os PNGs do `PsNee` mostram os pads sem legenda textual de designadores PCB. | `[DESCONHECIDO]` (ref. PCB PU-8); `[CORROBORADO]` (`IC706` PU-18) | Testar continuidade do pino `3` do FFC (`J2.3 = GPIO 4`) até o QSB e validar se há risco de contenção push-pull com `IC706.1`. |
| **G-06** | `EARLY-PU-8` vs `LATE-PU-8` | Lado da PCB (superior vs inferior) de montagem de `IC702 CXA1782BR`, além de pinagem por pino do `IC304 80p MC68HC05L16` (inferida via `PortX.BitY` sem esquemático Sony `PU-7`/`EARLY-PU-8`). | `[INFERIDO]` (`IC304` 80p); `[DESCONHECIDO]` lado de `IC702` | Impacta apenas consoles `EARLY-PU-8 (-11..-13)`. |
| **G-07** | Componentes parciais em `psx-spx` | Pinagens completas ausentes nas fontes: `CXD2922Q/BQ` (100p), `CXD2516Q` (100p), `CXD1817R` (144p, só pino `49 SCOR`), `BA6398FP`/`BA6397FP`/`BA6392FP`/`BA5977FP`, `CXA2550N`, `NEC 424805AL-A60`, `CXA2075M`, coeficientes `K00..K4F` do `CXD2545Q`, e sub-revisão de vídeo DAC usada na `PU-18` (`216:613` vs `216:2922`). | `[DESCONHECIDO]` | Documentado para evitar falsas afirmações; não bloqueia `PicoStation` na `PU-8`/`PU-18`. |
| **G-08** | `PU-20` (`1-668-413-xx`) | Conflito interno no `psx-spx` sobre clocks da `PU-20` (`X101+X201` vs cristal único + `IC204 CY2081 SL-500`). | `[DESCONHECIDO]` | `PU-20` já é incompatível com PicoStation (`CXD1817R`), mas merece registro em matriz. |
| **G-09** | `PicoStation-main` (`docs/TEST-LOG.md`) | Testes em console físico real após os patches da Fase 1 (`PU-8 OC/SCEI` com BIOS JP e `PU-18`) ainda aguardam execução em bancada. | Pendente de bancada | Validar boot `NORMAL BOOT`, busca ao fim do disco (*Resident Evil*), jogos multi-track, `GetSCExInfo` anti-modchip e troca de disco (`DOOR`). |

---

## 2. Protocolo Prático de Medição em Bancada (Antes de Plug-In / Energia)

> **REGRA DE OURO NA `PU-8`:** A fonte de 7 pinos mantém **`+3.5V EVER` (`3.54 V`) em `CN602.5` sempre energizado enquanto o cabo AC estiver na tomada** `[CONFIRMADO]`. **Desconecte o cabo AC** antes de qualquer solda, inspeção de continuidade ou conexão USB no Pico!

### Fase A — Inspeção Offline (Multímetro em Continuidade `Beep` e Ohms `Ω`, Cabo AC Desconectado)
1. **Identificar a Revisão Exata:** Ler `1-658-467-XX` (`PU-8`: `-11..-13` = Early 80p HC05 + 2×VRAM; `-21..-42` = Late 52p HC05 + SGRAM) ou `1-664-537-XX` (`PU-18`: `-11,-21,-52,-62` 4×RAM vs `-31,-41,-72,-82` 1×RAM). Verificar se `X101` e `X302` (`4.000 MHz` na `PU-8`) estão presentes ou removidos.
2. **Validar Isolamento de Pinos Levantados (`OL` / Circuito Aberto para o Pad e Adjacentes):**
   - `PU-8` (`IC309 CXD2510Q`): pinos **`44, 63, 66, 69`** — resistência `> MΩ` entre cada pino levantado e seu respectivo pad na PCB, e sem curto entre pinos vizinhos. Confirmar também circuito aberto onde os **3 resistores bottom** (`DA15, DA16, LRCK`) e o **resistor+filtro top** (`X101/APLL`) foram removidos.
   - `PU-18` (`IC701 CXD2545Q`): pinos **`45, 46, 47, 56, 74, 77, 78, 80`** isolados dos pads. Confirmar se pino **`68 (FSOF)`** está soldado (se usar clock próprio em `src/`/`xStation`) ou levantado (exclusivo `RC5 GPIO 3`!).
3. **Continuidade Ponta-a-Ponta (`< 2 Ω` até o conector FFC 20 vias `J2` / Pico):**
   - Conferir todos os 20 pinos do cabo FFC (`DOC_C` Seção 2): principalmente `J2.1/11/18 = GND`, `J2.2` não em curto com `GND`, `J2.17 = RESET` sem curto a `3.5V`, `J2.3 = SCEX` rastreando até qual CI chega na placa (op-amp/HC05 na `PU-8`; `IC706.1` na `PU-18`), e `J2.12 = APLL` rastreando até `IC103.4` / `IC305.80` / pad `X101`.
4. **Checagem de Diodo `D1 (1N5819)` na `picostation_pcb`:** Testar com multímetro em modo diodo da saída `U1 (7805 pin 3)` para `J3.39 (VSYS)`: deve conduzir (~`0.2–0.35 V`) apenas de `7805 -> VSYS`, bloqueando (`OL`) de `VSYS -> 7805`. **Jamais ligue USB com console energizado sem ter verificado `D1`.**

### Fase B — Checagem de Tensões DC (Pico DESCONECTADO da Placa / Cabo FFC Removido)
1. Energizar apenas a placa PS1 (sem Pico plugado) e medir com precisão de centésimos de volt em relação ao `GND`:
   - `CN602`: Pin 1 (`+7.69V` PU-8 / `+7.92V` PU-18), Pin 3 (`+3.48V` PU-8 / `+3.53V` PU-18), `AV Multi-Out Pin 3` (`+5.00V`).
   - **Crítico (`G-01`):** Medir tensão DC **nos pads** de `XLAT`, `CMD_DATA`, `CMD_CK`, `SQCK`, `DOOR`, `RESET`, `SCEX` (pad QSB ligado ao `J2.3`) e **`IC309.73` (`PU-8`)** em repouso. Se qualquer linha exceder **`3.60 V`** (ex. pull-up ligado a `5.0 V`), **NÃO conectar diretamente ao RP2040** — adicionar divisor resistivo ou level-shifter `74LVC`!
   - Medir queda na saída do regulador `U1 (7805)` e `VSYS` no `picostation_pcb` em vazio (`~4.6 V` em `VSYS`, `3.30 V` em `3V3`).

### Fase C — Capturas com Osciloscópio / Analisador Lógico (`Saleae` / `sigrok`)
1. **Clocks (`X101`/`APLL`/`OSC1 HC05`):**
   - Medir frequência exata e Vpp no pino `14` (`OSC1`, 52p) / pino `21` (80p) do **`IC304 MC68HC05`** (`4.000 MHz` em PU-8 stock vs `4.2336 MHz` em PU-18 stock vs `GPIO 3 RC5`) e no **`IC103.4 CRYSTALP`** (`67.7376 MHz`, `67.800 MHz` Pico, ou frequência do mod overclock!) + `IC103.201 SYSCK0`.
2. **Protocolo Mechacon ↔ PicoStation:**
   - Capturar simultaneamente 8 canais (`>= 10 MS/s`): `XLAT (GP0)`, `CMD_CK (GP27)`, `CMD_DATA (GP26)`, `SENS (GP14)`, `SCOR (GP18)`, `SQCK (GP1)`, `SQSO (GP19)`, `RESET (GP7)`.
   - Verificar: (a) pulso `RESET` LOW de `300 ms` no arranque; (b) `XLAT` descendo após cada grupo 8/16/24 bits de `CMD_CK`/`CMD_DATA`; (c) se HC05 consulta `SENS($4X XBUSY)` e recebe transição após `ASEQ FOCUS_ON ($47)`; (d) se `SCOR` pulsa a `75 Hz` (`13.32 ms`) / `150 Hz` (`6.66 ms`) seguido de `96` clocks `SQCK` + `12` bytes `SubQ`; (e) verificar em `SCEX (GP4)` quadros a `250.0 bps` (`4.000 ms/bit`, `8N2` invertido `"SCEI"`) apenas com `GFS=1` na área de lead-in.
3. **Barramento I²S & SD Card:**
   - `DA15` (`2.11875/4.2375 MHz`) e `LRCK` (`44.14/88.28 kHz`) com **razão exata 48:1**, verificando *overshoot/ringing* `< 3.8 V` nos fios.

---

## 3. Checklist de Fotografias Macro Recomendadas (`PU-8` e `PU-18`)

Para fechar permanentemente os pontos `[DESCONHECIDO]` deste levantamento em futuras revisões dos docs:
1. Foto **panorâmica top e bottom** bem iluminada (identificando silkscreen `1-658-467-XX` / `1-664-537-XX` e rótulo dos fusíveis `PS601..605`).
2. Macro **`IC309` (`CXD2510Q`, `PU-8`)** e **`IC701` (`CXD2545Q`, `PU-18`)** mostrando claramente os pinos levantados e o ângulo lateral (garantia zero ponte com os pads).
3. Macro da **região dos osciladores (`X101`, `X201`, `X302`) + alteração de overclock** (top e bottom), identificando exatamente qual cristal/resistor/filtro foi removido e onde está soldado o fio do mod overclock / `APLL`.
4. Macro da **face inferior (`QSB` ou fly-wires)** mostrando os pads raspados, os 3 resistores removidos (`PU-8`) / 1 resistor removido (`PU-18`) e a região serigrafada (`Rnnn`, `CLnnn`) ao redor.
5. Macro de **`IC304` (`MC68HC05`)** e **`IC102` (BIOS `40p`/`32p`)** para registrar o código exato de máscara (`4246xx` / `SC4309xx` / `SONY 20xx`) e do amplificador/comparador `SCEx` (`IC702` / `IC706 uPC5023GR-118`).
