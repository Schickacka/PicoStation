# Documento C — Mapa Elétrico Pino-a-Pino, Trilhas e Pontos de Solda (`PU-8`, `PU-18` e Comparativo)

> Este documento cruza `psx-spx/pinouts`, guias oficiais `xStation` (`EARLY-PU-8`, `LATE-PU-8`, `PU-18`, `PU-7`), esquemático `paulocode/picostation_pcb.kicad_sch` (auditado no nível de coordenadas/fios S-expression), `boards/picostation_*.cmake`, `PsNee` (`PU-8`/`PU-18`), `psdevwiki` e `PicoStation-main`.

---

## 1. Resumo de Isolamento Físico: Pinos Levantados (`LIFT PIN`) & Passivos Removidos `[CONFIRMADO]`

Para o `PicoStation`/`xStation` assumir os barramentos sem **contenção elétrica push-pull** (dois drivers digitais forçando `3.3V` contra `0V` simultaneamente) e para isolar o clock/QSB:

| Revisão | CI Alvo | Pinos Levantados (`LIFT PIN`) | Passivos Removidos na PCB | Fios Extras fora do footprint QSB | Motivo Elétrico |
|---|---|---|---|---|---|
| **`PU-7`** (`1-655-322-xx`) | `IC309 CXD2516Q` (100p) | **`11`, `14`, `18`, `72`** `[CONFIRMADO]` | Top: **1 resistor pequeno + 1 filtro** próx. ao `X101 67MHz`. Bottom: **3 resistores** `[CONFIRMADO]` | **`APLL`** (violeta) + **`LIDSW`** (amarelo) `[CONFIRMADO]` | Isolar C2PO/SCOR/SQSO/SENS e linhas I2S + clock `APLL` do DSP `CXD2516Q` `[CORROBORADO]` |
| **`EARLY-PU-8`** (`1-658-467-11/-12/-13`) | `IC309 CXD2510Q` (80p) | **`44` (`DA06-C2PO`), `63` (`SCOR`), `66` (`SQSO`), `69` (`SENS`)** `[CONFIRMADO]` | Top (área roxa `X101 67MHz`): **1 resistor pequeno + 1 filtro** `[CONFIRMADO]`.<br/>Bottom: **3 resistores SMD** antes do QSB `[CONFIRMADO]` *(designadores `Rnnn` `[DESCONHECIDO]`; linhas `DA15/DA16/LRCK` `[INFERIDO]`)* | **`APLL`** (violeta) + **`IO35`** (amarelo — `DOOR` / Shell Open `[CORROBORADO]`) `[CONFIRMADO]`. Cortar pino plástico inferior na carcaça `[CONFIRMADO]` | Isolar os 4 pinos diretos sem resistor série + abrir os 3 resistores série do barramento I2S (`32/34/35 → IC305.84/85/86`) + cortar acoplamento do oscilador p/ `APLL` `[CORROBORADO]` |
| **`LATE-PU-8`** (`1-658-467-21..-42`) *(Alvo Primário)* | `IC309 CXD2510Q` (80p) | **`44` (`DA06-C2PO`), `63` (`SCOR`), `66` (`SQSO`), `69` (`SENS`)** `[CONFIRMADO]` | Top (área roxa `X101 67MHz`): **1 resistor pequeno + 1 filtro** `[CONFIRMADO]`.<br/>Bottom: **3 resistores SMD** antes do QSB `[CONFIRMADO]` (`Rnnn` `[DESCONHECIDO]`; `DA15/DA16/LRCK` `[INFERIDO]`) | **`IO16`** `[CONFIRMADO]`. Cortar pino plástico inferior na carcaça `[CONFIRMADO]` | Mesmos isolamentos do `EARLY-PU-8`; no footprint QSB Late PU-8 o pad `APLL` casa via PCB enquanto `IO16` é roteado por fio fino `[CORROBORADO]` |
| **`PU-18`** (`1-664-537-11..-82`) *(Próximo Alvo)* | `IC701 CXD2545Q` (100p) | **`45` (`LRCK`), `46` (`DA16`), `47` (`DA15`), `56` (`C2PO`), `74` (`SCOR`), `77` (`SQSO`), `78` (`SQCK`), `80` (`SENS`)** `[CONFIRMADO]` | Bottom (próx. ao **`OSC V67.737 KDS813`**): **1 resistor SMD pequeno** (área roxa) `[CONFIRMADO]` (`Rnnn` `[DESCONHECIDO]`) | **`APLL`** ligado no pad **logo acima do resistor removido** `[CONFIRMADO]`. Cortar pino plástico inferior + raspar máscara nas vias esq/dir do QSB `[CONFIRMADO]` | `IC701.68` gera **`FSOF = 4.2336 MHz`** que alimenta `IC304.14` (`OSC1`) — `IC701` NÃO pode ser desligado! Por isso levantam-se os 8 pinos (inclusive `I2S` e a entrada `78 SQCK`) `[CONFIRMADO]` |

> **Nota crítica de bancada sobre `PU-8` Overclock (`oscilador removido` em `HANDOFF.md`):**  
> Como o passo 5 oficial do `xStation` em `EARLY/LATE-PU-8` já exige remover o resistor e o filtro adjacentes ao `X101` (e em consoles com overclock `X101` foi removido e a CPU recebeu oscilador próprio ex. `80 MHz` em `IC103.4` levantado, ou o `PicoStation GPIO 21` injeta `67.800 MHz` em `APLL`), **o console alvo do projeto já possui alteração direta nessa malha de clock**. O impacto sobre `PU8_OC` vs `PU18_OC` e os 2 bytes de calibração é detalhado no `DOC_F` e o procedimento de verificação em placa no `DOC_G`.

---

## 2. Esquema Elétrico da Placa Interface `picostation_pcb` & Conector FFC 20 Vias `[CONFIRMADO]`

Extraído diretamente por análise geométrica de fios e rótulos de [`picostation_pcb.kicad_sch`](https://github.com/paulocode/picostation_pcb/blob/main/picostation_pcb.kicad_sch) combinado com `boards/picostation_pico1.cmake`:

- **Regulação & Proteção (`U1`, `D1`, `C1`, `C2`, `R1`, `D2`):**
  - Entrada `J2.2` (`+7.5V..+7.92V` protegido pelo fusível de placa PS1 `CCP2E20` = `800 mA`) → capacitor `C1 (10 µF)` → `U1 (LM78M05 TO-252)` → `+5.0V` → capacitor `C2 (10 µF)` + LED indicador (`R1 2.2 kΩ` no `.kicad_sch` / BOM lista `R2 4.7 kΩ` → `D2 LED` → `GND`) + **diodo Schottky `D1 (1N5819)`** (ânodo em `U1.VO`, cátodo em `J3.39 VSYS`) `[CONFIRMADO]`.
  - **Motivo do `1N5819`:** Queda direta `~0.35–0.45 V` leva `VSYS` a `~4.55–4.65 V` (dentro da faixa `1.8V–5.5V` do regulador buck-boost `RT6150` do Pico) e **impede retorno de corrente (`backfeed`) do `VBUS +5V` USB do PC para o barramento de +7.5V da fonte do PlayStation** durante programação/depuração via USB com o cabo FFC plugado `[CORROBORADO]`.
- **Conector SD (`J1`):** `1:+3V3 (J3.36)`, `2:GP9 (SD_CS)`, `3:GP11 (SD_MOSI)`, `4:GP10 (SD_SCK)`, `5:GP8 (SD_MISO)`, `6:GND` `[CONFIRMADO]`.
- **Conector FFC 20 Vias (`J2`: `TE_2-84952-0`, passo `1.0 mm`, contato inferior; usa cabo FFC 20p 200mm invertido "lado azul p/ cima no QSB", compatível com QSB do xStation) `[CONFIRMADO]`:**

| Pino `J2` (`FFC`) | Net KiCad (`picostation_pcb`) | GPIO `pico1` | Sinal PS1 / PicoStation | Equiv. `xStation` (ESP32 / Cabo 20p) | Direção (ODE) | Nível |
|---|---|---|---|---|---|---|
| **`1`** | `GND` | `GND` | Terra de referência | `GND` | Power/Ref | `0 V` |
| **`2`** | `J2_Pin2` → `U1.VI` (`78M05`) | `VSYS` (via `78M05`+`1N5819`) | `+7.5V / ~7.9V` (via `PS601`/`PSxxx`) | `8V / LDO IN` | Power IN | `+7.69–7.92 V` |
| **`3`** | `pico_GP4` | `GPIO 4` | **`SCEX_DATA`** (`SCEA`/`SCEE`/`SCEI` 250 bps) | `PSNee / GP27` | OUT | `3.3V / Hi-Z` |
| **`4`** | `pico_GP16` | `GPIO 16` | **`DA16`** (`SDTA48` → `IC305.85`) | `DATA_OUT / GP26` | OUT (PIO0 SM0) | `3.3V CMOS` |
| **`5`** | `pico_GP19` | `GPIO 19` | **`SQSO`** (`SUBQ` → `IC304.24`) | `Subq / GP25` | OUT (PIO0 SM2/3) | `3.3V CMOS` |
| **`6`** | `pico_GP27A1` | `GPIO 27` | **`CMD_CK`** (`CLOK` de `IC304.45`) | `CLOK / GP33` | IN (PIO0 SM1 `jmp_pin`) | `3.5V CMOS` |
| **`7`** | `pico_GP26A0` | `GPIO 26` | **`CMD_DATA`** (`DATA` de `IC304.43`) | `Data / GP32` | IN (PIO0 SM1 `in_pins`) | `3.5V CMOS` |
| **`8`** | `pico_GP6` | `GPIO 6` (`plus: 20`) | **`DOOR`** (`SHELL_OPEN` → `IC304.19`) | `LID Switch / GP35` | IN (IRQ 50ms deb.) | `3.5V CMOS` |
| **`9`** | `pico_GP0` | `GPIO 0` | **`XLAT`** (de `IC304.44`) | `XLAT / GP34` | IN (IRQ fall) | `3.5V CMOS` |
| **`10`** | *(Não conectado em `J2/J4`)* | — | Não usado (`NC`) | `N/C / GP39` | — | — |
| **`11`** | `GND` | `GND` | Terra de referência | `GND` | Power/Ref | `0 V` |
| **`12`** | `pico_GP21` | `GPIO 21` | **`CLK`** (`67.800 MHz` PWM) | `APLL_CLK_OUT / GP16` | OUT (PWM) | `3.3V` |
| **`13`** | `pico_GP1` | `GPIO 1` | **`SQCK`** (de `IC304.26`) | `SQCK / GP36` | IN (PIO0 SM2/3) | `3.5V CMOS` |
| **`14`** | `pico_GP15` | `GPIO 15` | **`DA15`** (`SCLK48` → `IC305.86`) | `BCK_OUT / GP4` | OUT (PWM) + IN PIO | `3.3V CMOS` |
| **`15`** | `pico_GP17` | `GPIO 17` | **`LRCK`** (→ `IC305.84`) | `WS_OUT / GP5` | OUT (PWM) + IN Poll | `3.3V CMOS` |
| **`16`** | `pico_GP14` | `GPIO 14` | **`SENS`** (→ `IC304.23`) | `SENS / GP19` | OUT | `3.3V CMOS` |
| **`17`** | `pico_GP7` | `GPIO 7` (`plus: 22`) | **`RESET`** (`/RES`) | `RST / GP21` | BIDIR (Open-Drain) | `3.5V CMOS` |
| **`18`** | `GND` | `GND` | Terra de referência | `GND` | Power/Ref | `0 V` |
| **`19`** | `pico_GP18` | `GPIO 18` | **`SCOR`** (→ `IC304.31`) | `SC* / GP22` | OUT | `3.3V CMOS` |
| **`20`** | `pico_GP2` | `GPIO 2` | **`LMTSW`** (`/POS0` → `IC304.18`) | `Limit Switch / GP23` | OUT | `3.3V CMOS` |

*(Nota: Em `picostation_pcb.kicad_sch`, os pinos `DA06-C2PO` e `WFCK` **não passam no cabo FFC até o Pico**: eles são aterrados a `GND` no lado do QSB / placa-mãe; além disso `J2` e `J4` estão em paralelo 1:1 para debug/breakout de 20 pinos `[CONFIRMADO]`.)*

---

## 3. Mapa Elétrico Rede a Rede (Pino de Origem, Destino, Passivos, Aterramento e Contenção)

> **Convenção para `IC304` 80p (`EARLY-PU-8` / `PU-7`):** A correspondência de pinos por porta lógica (`PA0..7`, `PB0..7`, `PC0..7`, `PD1..7`) entre o `MC68HC05L16` (80p) e o `MC68HC05G6` (52p) está marcada como `[INFERIDO]` por equivalência direta de portas Motorola `[CONFIRMADO]` em `steps/216`.

| Rede (Net) | `GPIO pico1` / `plus` | `FFC J2` | Origem Stock (`PU-8` / `PU-18` / `PM-41`) | Destino Stock / ODE | `IC304` `80p` (`EARLY-PU-8`) `[INF]` | `IC304` `52p` (`LATE-PU-8`/`PU-18`) `[CONF]` | Passivos Intermediários Documentados | Lift / Isolamento (`PU-8` vs `PU-18`) | Comportamento / Risco de Conflito |
|---|---|---|---|---|---|---|---|---|---|
| **`XLAT`** | `0` / `0` | `J2.9` | `IC304` (`PortD.2`) | `PU-8`: `IC309.72` + `IC702.20` (`XLT`)<br/>`PU-18`: `IC701.87`<br/>`PM-41`: `732.75` / `.94` | `49` (`BP2/PD2`) | `44` (`XLAT`) | **`22 Ω`** em série (`216:836,1422`) `[CORR.]` | Sem lift `[CONF.]` | IN (borda descida dispara `MechCommand::processLatchedCommand()`). **Colide com `PICO_DEFAULT_UART_TX_PIN=0` se UART debug ativo.** `[CONF.]` |
| **`CMD_DATA`** | `26` / `26` | `J2.7` | `IC304` (`PortD.1`) | `PU-8`: `IC309.71` + `IC702.21`<br/>`PU-18`: `IC701.86`<br/>`PM-41`: `732.74` / `.93` | `50` (`BP1/PD1`) | `43` (`DATA`) | **`22 Ω`** em série (`216:836,1421`) `[CORR.]` | Sem lift `[CONF.]` | IN (PIO0 SM1 `in_pins` base; requer `GPIO_CMD_CK == GPIO_CMD_DATA + 1`). `[CONF.]` |
| **`CMD_CK`** | `27` / `27` | `J2.6` | `IC304` (`PortD.3`) | `PU-8`: `IC309.74` + `IC702.19`<br/>`PU-18`: `IC701.88`<br/>`PM-41`: `732.76` / `.95` | `48` (`BP3/PD3`) | `45` (`CLOK`) | **`22 Ω`** em série (`216:836,1423`) `[CORR.]` | Sem lift `[CONF.]` | IN (PIO0 SM1 `jmp_pin`; monitorado idle HIGH `30ms` no boot). `[CONF.]` |
| **`SENS`** | `14` / `14` | `J2.16` | `PU-8`: `IC309.69`<br/>`PU-18`: `IC701.80`<br/>`PM-41`: `732.82` / `.92` | `IC304` (`PortB.7`) | `38` (`PB7/KWI7`) | `23` (`SENSE`) | Nenhum doc. `[DESC]` | **`PU-8`: LIFT `IC309.69`**<br/>**`PU-18`: LIFT `IC701.80`** `[CONF.]` | OUT (mux 1-bit `m_sensData[]`). **Contenção CRÍTICA sem lift.** `[CONF.]` |
| **`SCOR`** | `18` / `18` | `J2.19` | `PU-8`: `IC309.63`<br/>`PU-18`: `IC701.74`<br/>`PU-20`: `IC720.49` | `IC304` (`PortC.7` IRQ1) | `46` (`PC7/IRQ1`) | `31` (`SCOR`) | Nenhum doc. `[DESC]` | **`PU-8`: LIFT `IC309.63`**<br/>**`PU-18`: LIFT `IC701.74`** `[CONF.]` | OUT (pulso ativo-HIGH `75/150 Hz` quando `GFS=1`). **Contenção CRÍTICA sem lift.** `[CONF.]` |
| **`SQSO`** | `19` / `19` | `J2.5` | `PU-8`: `IC309.66`<br/>`PU-18`: `IC701.77`<br/>`PM-41`: `732.81` / `.91` | `IC304` (`PortC.0` SDI) | `39` (`PC0/SDI`) | `24` (`SUBQ`) | Nenhum doc. `[DESC]` | **`PU-8`: LIFT `IC309.66`**<br/>**`PU-18`: LIFT `IC701.77`** `[CONF.]` | OUT (PIO0 SM3 `subq` 96b / SM2 `soct` 17b). **Contenção CRÍTICA sem lift.** `[CONF.]` |
| **`SQCK`** | `1` / `1` | `J2.13` | `IC304` (`PortC.2` SCK) | `PU-8`: `IC309.67`<br/>`PU-18`: `IC701.78` | `41` (`PC2/SCK`) | `26` (`SQCK`) | Nenhum doc. `[DESC]` | **`PU-8`: Sem lift**<br/>**`PU-18`: LIFT `IC701.78`** `[CONF.]` | IN (clock síncrono do HC05). **Colide com `PICO_DEFAULT_UART_RX_PIN=1` se UART debug ativo.** `[CONF.]` |
| **`DA15`** (`BCLK`) | `15` / `15` | `J2.14` | `PU-8`: `IC309.35`<br/>`PU-18`: `IC701.47` | `IC305.86` (`BCLK`) | — | — | `LATE/EARLY PU-8`: resistor em série bottom removido (`[INF.]` 1 dos 3R) | **`PU-8`: Remover resistor série (`3R` bottom)**<br/>**`PU-18`: LIFT `IC701.47`** `[CONF.]` | OUT PWM (`2.11875/4.2375 MHz`) + IN PIO0 SM0 sync (`DA16`). **Contenção CRÍTICA sem isolamento.** `[CONF.]` |
| **`DA16`** (`DATA`) | `16` / `16` | `J2.4` | `PU-8`: `IC309.34`<br/>`PU-18`: `IC701.46` | `IC305.85` (`DATA`) | — | — | `EARLY/LATE PU-8`: 1 dos 3R bottom removido `[INF.]` | **`PU-8`: Remover resistor série (`3R` bottom)**<br/>**`PU-18`: LIFT `IC701.46`** `[CONF.]` | OUT PIO0 SM0 (DMA 1176 dwords = 2352 B/setor). **Contenção CRÍTICA sem isolamento.** `[CONF.]` |
| **`LRCK`** | `17` / `17` | `J2.15` | `PU-8`: `IC309.32`<br/>`PU-18`: `IC701.45` | `IC305.84` (`LRCK`) | — | — | `EARLY/LATE PU-8`: 1 dos 3R bottom removido `[INF.]` | **`PU-8`: Remover resistor série (`3R` bottom)**<br/>**`PU-18`: LIFT `IC701.45`** `[CONF.]` | OUT PWM (`44.14/88.28 kHz`) + polling para armar DMA. **Contenção CRÍTICA sem isolamento.** `[CONF.]` |
| **`DA06-C2PO`** | `GND` | QSB→GND | `PU-8`: `IC309.44`<br/>`PU-18`: `IC701.56` | `IC305.87` (`C2PO`) | — | — | Aterrado no QSB (`values.h` L45) `[CONF.]` | **`PU-8`: LIFT `IC309.44`**<br/>**`PU-18`: LIFT `IC701.56`** `[CONF.]` | Mantido em `0V` (sem erros C2). **Curto direto para GND sem lift.** `[CONF.]` |
| **`WFCK`** | `GND` | QSB→GND | `PU-8`: `IC309.62`<br/>`PU-18`: `IC701.73` | — | — | — | `values.h` (`always GND`); `PM-41`: `SPU.5` sync modchip `[CONF.]` | Sem lift (`xStation`) `[CONF.]` | Rede passiva de `WFCK` em `PU-8`/`PU-18` sem esquemático: `[DESC]` (`DOC_G`). |
| **`LMTSW`** | `2` / `2` | `J2.20` | Chave sled (`CN702.12`) | `IC304` (`PortB.2`) | `33` (`PB2/KWI2`) | `18` (`LMTSW`) | Pull-up board `[INF.]` (`[DESC]` valor) | Drive físico **desconectado** | OUT (`0` se `sector <= 3000`, `1` se `> 3000`). Curto se drive óptico ficar plugado com chave fechada a GND! `[CONF.]` |
| **`DOOR`** | `6` / `20` | `J2.8` | Chave tampa (`SHELL_OPEN`) | `IC304` (`PortB.3`) | `34` (`PB3/KWI3`) | `19` (`DOOR`) | Pull-up/RC `[DESC]` | Sem lift; **desligar/soldar em paralelo** (`IO35`/`IO16`) | IN (`HIGH = aberta`, `LOW = fechada`). **Atenção:** `tzmwx` alerta testar tampa fechada para não travar boot. `[CONF.]` |
| **`/RES`** | `7` / `22` | `J2.17` | `CN602.7` (`PU-8`), `CN602.5` (`PU-18`), `IC002/003` (`PM-41`) | `IC103.76` & `IC203` (`330 Ω`); `IC304`, `IC308.38`, `IC722.20` (`5.6 kΩ`); `IC305.89`, `IC309.70`, `IC701.81`, `IC702.22`, `CN103.2` `[CONF.]` | `20` (`/RESET`) | `13` (`/RES`) | **`330 Ω`** (CPU/GPU) e **`5.6 kΩ`** (HC05/SPU/Driver) `[CONF.]` | Sem lift `[CONF.]` | BIDIR **Open-Drain emulado**: segura `LOW 300ms` no boot, depois vira `GPIO_IN` (IRQ `50-999ms` / `>=1000ms`). **NUNCA drivado HIGH.** `[CONF.]` |
| **`SCEX_DATA`** | `4` / `4` | `J2.3` | Wobble RF / Modchip | `PM-41`: `IC732.42 CEI` (via cap←`41 CEO` e R/D↔`IC723.10 VC`).<br/>`PU-8`/`PU-18`: pino exato de injeção (`DATA`/`GATE`/`SCIN`) `[DESC]` em texto (apenas PNGs em `PsNee`) | `[DESC]` | `17` (`PB1`: `CG` em 5500 `[CORR.]`? N/C em PM-41) | Em `PM-41`: cap + resistor/diodo `[CONF.]` | Sem lift `[CONF.]` | OUT 250 bps 8N2 invertido (`"SCEA"/"SCEE"/"SCEI"`). **Verificar via continuidade do ponto no QSB / diagrama `PsNee` (`DOC_G`).** `[CORR.]` |
| **`CLK` (`APLL`)** | `21` / `21` | `J2.12` | `X101 67.737MHz` (`IC103.4 CRYSTALP`; `IC309.51 APTL`; `IC305.80..83` `[CORR.]`) | Destino exato da rede `APLL` na PCB `PU-8` (`EARLY`/`LATE`) vs `PU-18` (acima do R removido junto ao `X101`) `[DESC]` nos textos sem esquemático | — | — | **`PU-8`**: 1 R + 1 filtro top removidos; **`PU-18`**: 1 R pequeno bottom removido `[CONF.]` | Remoção do(s) passivo(s) junto ao `X101` `[CONF.]` | OUT PWM `67.800 MHz` (`271.2/4`). **No PU-8 OC (oscilador removido), checar na bancada onde entra o clock da CPU vs CD (`DOC_G`).** |
| **`Si5351 I2C0`** | `SDA:12`/`8`, `SCL:13`/`9` *(pico2: `31`!)* | — | Expansão placa `PicoStation` | `Si5351A` addr `0x60` → `CLK1=53.203425M`, `CLK2=53.693175M` (`8mA`) → substituto de `X201` (DFO PAL/NTSC) `[CONF.]` | — | — | Pull-ups I²C na placa DFO `[INF.]` | Opcional / placa `plus` | **`initHW()` chama I2C0 mesmo se `GPIO=31` em `picostation_pico2`!** `[CONF.]` |

---

## 4. Rede de Alimentação e Fusíveis em `PU-8` e `PU-18` `[CONFIRMADO]`

- **Trilho `+3.5V` (`~3.48 V` em `PU-8`/`PU-9`; `~3.53 V` em `PU-18`/`PU-23`):**
  - Entra por `CN602.3` (Fio Laranja) + `CN602.5` (`STAND-BY 3.54V` sempre ativo em `PU-8`/`PU-9`).
  - Protegido por `PS604` (`CCP2E50`, `2.3 A`) para lógica principal (`IC103`, `IC203`, `IC304`, `IC305`, `IC303`, `IC309.23/73`, `IC701.21/40/41/90`, `IC102`) e `PS605` (`CCP2E15`, `600 mA`) para portas de controle/Memory Card `[CORROBORADO]`.
- **Trilho `+5.0V`:**
  - **Não sai diretamente do `CN602`** nas Fat (`PU-8`, `PU-18`) — é gerado na placa-mãe a partir do `+7.5V` (protegido por `PS603 CCP2E20`, `800 mA`), alimentando **Multi-Out pino 3**, `IC405` (via bobina `L401`), `IC501`/`IC502`, e **atenção crítica: `IC309.33` (`CXD2510Q VDD 5V` em `PU-8`)** `[CONFIRMADO]`, além de `IC308.79` (`CXD2925Q 5V`) `[CONFIRMADO]` e `IC310` (`SPU-RAM`) nas `PU-22`/`PM-41`.
- **Trilho `+7.5V..+8.0V` (`~7.69 V` em `PU-8`/`PU-9`; `~7.92 V` em `PU-18`/`PU-23`):**
  - Entra por `CN602.1` (Fio Marrom). Protegido por `PS601` (`CCP2E20`, `800 mA`, marcado `"20"`) para o driver BTL (`BA6397FP` pinos `21,22`; `BA5947FP` pinos `10,19,28`) e **`J2.2` (`FFC Pino 2`) do QSB xStation/PicoStation** `[CORROBORADO]`; e `PS602` (`CCP2E20`, `800 mA`) para motores DualShock + PIO `CN103.18/52` `[CONFIRMADO]`.

---

## 5. Pontos de Modchips Clássicos (`PSNee v7/v8`, `MM3`, `OneChip`, `Old Crow`) vs. ODE `[CONFIRMADO]`

- **Modchips clássicos** não interceptam `DA15/DA16/LRCK/C2PO` nem levantam pinos; apenas ligam `VCC 3.5V`, `GND`, `DATA` (`SCEx` para `CEI`/`RF`/amplificador de erro de tracking), `GATE` (somente `PU-7`/`PU-8` — mantido `LOW` p/ silenciar wobble original em discos licenciados; em `PU-22+`/`PM-41` substituído por **`WFCK` / `SYNC`** de `7.35/14.7 kHz`), `SQCK` + `SUBQ` (em `PSNee` / `PicoStation` p/ stealth 100% em `PU-7`/`PU-8` — em `PSNee v7/v8` todos os GPIOs ficam **Hi-Z/float** quando ociosos para evitar ruído no pickup da `PM-41(2)` `[CONFIRMADO]`), e linhas **`BIOS Ax / Dx` + `RESET`** em `SCPH-xxx0` (`NTSC-J`) e `SCPH-102` (`PAL PSone`) para patch "on-the-fly" da trava de região na `BIOS ROM` (`40p` ou `32p`) `[CORROBORADO]`.
- **No `PicoStation`**, como não há disco físico girando, o próprio firmware lê `currentSector`/`GFS`/`SOCT` internamente no RP2040 e só precisa de **`GPIO 4` (`SCEX_DATA`)** — sem necessidade de `GATE`, `WFCK` ou captura analógica!

---

## 6. Conectores Externos e Internos da Placa-Mãe (`[CONFIRMADO]`)

### 6.1 Portas de Controle (`SIO0`) & Memory Card (9 pinos; MemCard omite pino `8` e Shield)
`1: DAT/MISO (CPU.84)`, `2: CMD/MOSI (CPU.83)`, `3: +7.5V (rumble; NC MemCard)`, `4: GND`, `5: +3.5V`, `6: /CS1 (CPU.80) ou /CS2 (CPU.77)`, `7: SCK (CPU.81)`, `8: /IRQ10 (CPU.89, só Ctrl)`, `9: /ACK (CPU.82)`.  
- **`PU-23` Daughterboard `CN102` (10p):** `1:/IRQ10, 2:/ACK(/IRQ7), 3:/JOY2, 4:7.5V(7.92V), 5:/JOY1, 6:DAT, 7:GND, 8:CMD, 9:3.5V, 10:CLK`.

### 6.2 `CN103` Porta Paralela / Expansão (`PIO`, 68p, `PU-7..PU-22`)
- `1,16,19,34,35,50,53,68: GND` (Nota PSIO: `5` repurp. `SBEN`, `39` repurp. `/CS2`) | `17,51: +3.5V` | `18,52: +7.5V`
- `2:/RESET (CPU.76)` | `3:DREQ5 (86)` | `4:/CS0 (98)` | `30:/RD (101)` | `31:NC (/IRQ2 PSIO)` | `32:SYSCK (33.868M)` | `33:BCLK` | `36:DACK5 (85)` | `37:/IRQ10 (89)` | `38:/WR1 (99)` | `64:/WR0 (100)` | `65:NC (/CS5 PSIO)` | `66:LRCK` | `67:SDIN (DATO)`
- Lado par/inferior `6..13` = `D0,D2..D14`; `40..47` = `D1,D3..D15` (`CPU.107..124`); `14..15,20..29` = `A0,A2..A22`; `48..49,54..63` = `A1,A3..A23` (`CPU.125..152`).

### 6.3 Porta Serial (`SIO1`, 8p + Shield, todas Fat; 6 pads na `PM-41`)
Interface `0V / 3.5V` **invertida por transistores `Q105`/`Q106` e protegida por `FB112..FB116` + `22 Ω` + `1 kΩ`/`470 Ω` pull-ups + diodos** (ex. `PU-23`): `1: RXD (CPU.75)`, `2: +3.5V (via L102)`, `3: DSR (Q106→CPU.73)`, `4: TXD (CPU.74)`, `5: CTS (Q106→CPU.71)`, `6: DTR (CPU.72→Q105)`, `7: GND (+Shield)`, `8: RTS (CPU.70→Q105)`.

### 6.4 `AV Multi-Out` (12p + Shield), `CN602` (PSU) e `CN701`/`CN702` (Drive Óptico)
- **`AV Multi-Out`:** `1:G`, `2:R`, `3:+5.0V (única saída 5V externa!)`, `4:B`, `5:Supply GND`, `6:S-Video C (220µF+75Ω)`, `7:Composite (220µF+75Ω)`, `8:S-Video Y (cap+75Ω)`, `9:Audio L (100Ω)`, `10:Audio L GND`, `11:Audio R (100Ω)`, `12:Audio R GND`, `Shield:Video GND`.
- **`CN602` (`PU-8`/`PU-9` 7p):** `1:Marrom +7.5V(7.69V), 2:Verm GND, 3:Lar +3.5V(3.48V), 4:Amar GND, 5:Brc STAND-BY(3.54V ON), 6:Azul GND, 7:Mag /RES`.
- **`CN602` (`PU-18`/`PU-23` 5p):** `1:Marrom +7.5V(7.92V), 2:Verm GND, 3:Lar +3.5V(3.53V), 4:Amar GND, 5:Brc /RES`.
- **`CN701` (Motor 4p, `PU-8/18/23/PM-41`):** `1:SL-, 2:SL+, 3:SP+, 4:SP-`.
- **`CN702` (Pick-up 16p, `PU-23`/`PM-41`):** `1:LD(Q701), 2:VCC, 3:VC(CL710), 4:F-(33K), 5:NC(CL776), 6:PD2(33K*), 7:PD1(33K*), 8:E-(33K), 9:M1(PD), 10:VR(91Ω->GND), 11:GND, 12:LS(/POS0), 13..16: Bobinas Focus/Tracking (±)`.
- **`CLnnn` verificados (`PU-23`/`PM-41`):** `CL316 (IC304.21)`, `CL616 (+7.5V pré-chave PM-41)`, `CL617 (GND PM-41)`, `CL704 (IC723.13 RF0)`, `CL706 (GND)`, `CL708 (IC723.15 FE)`, `CL709 (IC723.16 TE)`, `CL710 (IC723.10 VC)`, `CL711 (IC723.15 via 1K)`, `CL776 (CN702.5)`. Numeração `CLnnn` de `PU-8`/`PU-18` não documentada nos arquivos → `[DESCONHECIDO]` (`DOC_G`).
