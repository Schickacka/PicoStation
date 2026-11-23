# Documento F — Arquitetura Completa Hardware/Firmware do PicoStation & Análise Elétrica RP2040

> Documentação baseada na inspeção direta de `src/*.cpp`, `src/*.c`, `include/*.h`, `pio/main.pio`, `CMakeLists.txt`, `boards/*.cmake`, `picostation_pico1.elf` / `.elf.map`, `HANDOFF.md`, `releases/RC5/PATCHES.md`, análise binária/disassembly de `releases/RC5/flash/*.bin`, esquema `paulocode/picostation_pcb.kicad_sch` e **Datasheet oficial do RP2040 (Seção 5.2.3)**.

---

## 1. Síntese de Clocks, PLL, PWM, PIO e Memória no RP2040 `[CONFIRMADO]`

### 1.1 PLL do Sistema (`271.200 MHz`) e Clocks PWM (`src/main.cpp`, `src/picostation.cpp`)
- **Configuração PLL (`vcocalc` / `set_sys_clock_khz(271200, true)`):**
  - Regulador interno ajustado para **`1.15 V`** (`VREG_VOLTAGE_1_15`) + pausa de `100 ms`.
  - A partir do cristal `12.000 MHz` do Pico: `REFDIV=1, FBDIV=113 → VCO = 1356.0 MHz`, `PD1=5, PD2=1 → clk_sys = clk_peri = 271.2000 MHz` `[CONFIRMADO]`.
- **Saídas PWM em `src/picostation.cpp:58-83, 389-419` (todos com divisor inteiro exato — zero jitter fracionário PWM):**

| Sinal (`Pin`) | GPIO (`pico1`/`plus`) | `wrap` | `clkdiv` | `level` | `invert` | Frequência Gerada (Nominal PS1) | Erro Estático | Função |
|---|---|---|---|---|---|---|---|---|
| **`CLK`** (`pwmMainClock`) | `21` / `21` | `1` (período 2) | `2` | `1` (50%) | `false` | **`67.800000 MHz`** (`67.7376 MHz`) | **`+0.09212%`** (`+921.2 ppm`) | Mestre (`APLL` / CD Decoder / CPU se `X101` removido) |
| **`DA15` (`BCLK`) 1x** | `15` / `15` | `31` (período 32) | `4` | `16` (50%) | **`true`** | **`2,118,750 Hz`** (`2,116,800 Hz`) | `+0.0921%` | Bit clock I²S (`48 × LRCK`) |
| **`DA15` (`BCLK`) 2x** | `15` / `15` | `31` (período 32) | `2` | `16` (50%) | **`true`** | **`4,237,500 Hz`** (`4,233,600 Hz`) | `+0.0921%` | Bit clock I²S em `DSPB=1` (`2x`) |
| **`LRCK` 1x** | `17` / `17` | `1535` (período 1536) | `4` | `768` (50%) | `false` | **`44,140.625 Hz`** (`44,100 Hz`) | `+0.0921%` | Word Select (`75.069 setores/s`, `13,321.06 µs/setor`) |
| **`LRCK` 2x** | `17` / `17` | `1535` (período 1536) | `2` | `768` (50%) | `false` | **`88,281.25 Hz`** (`88,200 Hz`) | `+0.0921%` | Word Select (`150.138 setores/s`, `6,660.53 µs/setor`) |
| **`GPIO 3` *(somente RC5)*** | `3` (ausente em `src/`) | `67` (`PU8`) / `63` (`PU18`) | `1` | `34` / `32` | `false` | `3.9882 MHz` (`PU8`) / `4.2375 MHz` (`PU18`) | `-0.294%` / `+0.092%` | **Clock `OSC1` do `MC68HC05`** (`X302` / `CXD2545Q.68`) |
| **`Si5351` DFO (`I2C0`)** | `12,13` / `8,9` | — | — | — | `8 mA` | `53.203425 MHz` (CLK1) / `53.693175 MHz` (CLK2) | Cristal próprio | Clock de GPU PAL/NTSC (evita perda de cor por desvio `+921 ppm`) |

> **Observação de transição 1x↔2x:** `updatePlaybackSpeed()` (`src/picostation.cpp:411-416`) zera `pwm_set_mask_enabled(0)` brevemente ao reconfigurar `DA15`/`LRCK`, paralisando por dezenas de nanossegundos todos os slices no mesmo ciclo antes de religá-los sincronizados `[CONFIRMADO]`.

### 1.2 As 4 State Machines do `pio0` (`30/32` instruções) + `spi1` @ 30 MHz `[CONFIRMADO]`
*(Correção factual: o projeto usa **4 State Machines** em `pio0`, **não 5**; o cartão SD utiliza o periférico hardware **`spi1`**, e não PIO.)*

1. **`pio0 SM 0` (`i2s_data`, 3 instr.):** `in = DA15 (15)`, `out = DA16 (16)`, **`input_sync_bypass` habilitado em `DA15`** (obrigatório para 4.24 MHz sem atraso de 2 ciclos do sincronizador), `shift_right=false` (MSB-first), `autopull=true (24 bits)`, TX FIFO joined (`8 words`).
2. **`pio0 SM 1` (`mechacon`, 7 instr.):** `in = CMD_DATA (26)`, `jmp_pin = CMD_CK (27)`, RX FIFO joined (`8 words`), `shift_right=true` (**LSB-first**), dispara **`IRQ 0`** (`PIO0_IRQ_0`) a cada 8 bits (`rxf >> 24`). Requer `GPIO_CMD_CK == GPIO_CMD_DATA + 1`.
3. **`pio0 SM 2` (`soct`, 8 instr.):** `out = SQSO (19)`, `in = SQCK (1)`. Ao carregar `0xFFFFFFF`, emite **`x = 16` (17 bits `1`)** sincronizados com `SQCK` e faz `push block` para notificar Core 0.
4. **`pio0 SM 3` (`subq`, 12 instr.):** `out/set = SQSO (19)`, `in = SQCK (1)`. Puxa 3 dwords (`96 bits`), coloca `SQSO=1` no início, desloca **LSB-first** na borda de `SQCK`, depois força `SQSO=0` e entra em loop infinito (`hang`) até próximo setor.
5. **DMA + Cache SRAM (`src/i2s.cpp`):** 1 canal DMA (`DREQ_PIO0_TX0`, `1176 × 32-bit = 4704 B/setor`) servido por cache circular de **`32 setores` (`pioSamples[32][1176] = 150,528 B`)**. Core 1 carrega setores da SD (`spi1 @ 30 MHz`, `12 mA`, `SLEW_FAST`), calcula ECC/EDC + `scramble_data()` e aguarda borda `LRCK 1→0→1` antes de armar cada setor `[CONFIRMADO]`.

### 1.3 Separação Dual-Core & Orçamento Real de Memória (`picostation_pico1.elf`) `[CONFIRMADO]`
- **Core 0:** IRQs de GPIO (`RESET` open-drain 300ms + Reset curto/longo, `DOOR` debounce 50ms, `XLAT` borda de descida), `PIO0_IRQ_0` (`mechacon`), `moveSled` (tick `29 µs`), `send_subq` via alarme (`<= 3333 µs`), `resetXBUSY` via alarme (`15 ms`), `LMTSW`, troca 1x/2x PWM e drenagem de `SOCT`.
- **Core 1:** Leitura SD/FatFs via `spi1`, geração de setores, menu (`DirectoryListing`), loop DMA I²S (`6.66 ms/setor` de margem máxima em 2x!) e **`ModChip::sendLicenseString`** (250 bps non-blocking).
- **Ocupação verificada (`arm-none-eabi-size` / `.elf.map` em `tools/build-src/picostation_pico1.elf`):**
  - Flash: **`320,908 B`** (`~313.4 KB`).
  - SRAM principal (`256 KB` em `0x20000000`): **`213,596 B` (`81.48%`)** — sendo `.ram_vector_table = 192 B`, `.data = 42,436 B` (**`41,008 B` / 126 funções `__time_critical_func` copiadas para SRAM** para evitar stalls do cache XIP durante DMA/IRQ!), `.bss = 170,964 B` (inclui `150,528 B` de `pioSamples`) e `.heap = 2,048 B`. Restam **`48,548 B` (~47.4 KB)** livres, além dos 4 KB dedicados `scratch_x`/`scratch_y` (stacks dos Cores 1 e 0).

---

## 2. Mistério Resolvido: Os 2 Bytes `PU-8` vs `PU-18` e as Variantes `_OC` do RC5 `[CONFIRMADO]`

A auditoria por disassembly e mapa de memória nos binários oficiais em `releases/RC5/flash/` esclareceu integralmente os bytes reportados em `releases/RC5/PATCHES.md`:

1. **Diferença exata de 2 bytes entre `picostation_PU8.bin` e `picostation_PU18.bin` (`0x3CB04..0x3CB23`):**
   - Trata-se da struct `PWMSettings` para **`GPIO 3`** (implementada no RC5 oficial fechado, mas **inexistente no snapshot `src/`**, onde `GPIO 3` é livre em `pico1` ou `EXP_BUTTON0` em `plus_pico1`):
     - **`PU-8` (`0x3CB18 = 0x43 [67]`, `0x3CB22 = 0x22 [34]`):** $271.2\text{ MHz} / 68 = \mathbf{3.988235\text{ MHz}}$ (duty 50%), substituindo o cristal `X302 = 4.000 MHz` que alimenta o **`MC68HC05` (`IC304`)** (`-0.294%`).
     - **`PU-18` (`0x3CB18 = 0x3F [63]`, `0x3CB22 = 0x20 [32]`):** $271.2\text{ MHz} / 64 = \mathbf{4.237500\text{ MHz}}$ (duty 50%), substituindo o sinal `FSOF = 4.2336 MHz` do pino **`IC701.68` levantado** (`+0.0921%`) que alimenta `IC304.14` (`OSC1`).
   - **Aviso CRÍTICO para compilação do código-fonte `src/`:** Como **`src/` NÃO configura PWM no `GPIO 3`**, se a placa-mãe física (`PU-8` ou `PU-18`) estiver instalada para o padrão `RC5` (com `X302` removido na `PU-8` ou `IC701.68` levantado na `PU-18`), **o Mechacon `IC304` ficará sem clock ao gravar um UF2 compilado de `src/`!** Em instalações `xStation` clássicas ou `src/` padrão, `X302` e `IC701.68` devem permanecer intactos (a menos que adicionemos suporte ao `GPIO 3` no `src/`).

2. **Decodificação da Rotina `_OC` (`PU8_OC.bin` / `PU18_OC.bin`, `0x35CF8..0x35D94`):**
   - É uma função de **overclock dinâmico do Mechacon `IC304` + ajuste proporcional do `UART1` de `SCEx`** (para acelerar respostas/seek do HC05 em tempo de execução!):
     - **`PU8_OC`:** normal → `wrap=65` (`4.109 MHz`), `uart1=257 bps` (saturado pelo PL011 em `258.64 bps`), `c_MaxTrackMoveTime=29`; modo OC → `wrap=41` (**`271.2/42 = 6.4571 MHz` = `+61.4%`** no HC05!), `level=21`, **`uart1=404 bps`** (`0xCA << 1`, proporcional a `6.457/4.000 × 250 = 403.6 bps`, sem saturar `IBRD`!), `c_MaxTrackMoveTime=15`.
     - **`PU18_OC`:** normal → `wrap=63` (`4.2375 MHz`), `uart1=250 bps` (`258.64 bps`), `c_MaxTrackMoveTime=29`; modo OC → `wrap=35` (**`271.2/36 = 7.5333 MHz` = `+77.9%`** no HC05!), `level=18`, **`uart1=444 bps`** (`0xDE << 1`, proporcional a `7.533/4.234 × 250 = 444.9 bps`), `c_MaxTrackMoveTime=15`.
   - *(Comparação com `HANDOFF.md`:* `HANDOFF.md` descreveu o hardware de bancada como `"PU-8, overclock (oscilador removido) e BIOS japonesa (SCEI)"` referindo-se provavelmente ao overclock de CPU/removido `X101`/`X302` do console de teste; hoje temos provado por disassembly que os binários `*_OC` do RC5 fazem overclock especificamente do **clock do Mechacon no `GPIO 3`** + escala `UART1` + `c_MaxTrackMoveTime=15` `[CONFIRMADO]`.)

---

## 3. Auditoria Elétrica: RP2040 (`3.3 V`) vs. Barramentos do PS1 (`3.48–3.54 V`, `5.0 V`, `7.92 V`) `[CORROBORADO]`

### 3.1 Compatibilidade de Níveis e Limites Absolutos (RP2040 Datasheet Seção 5.2.3)
- `IOVDD` nominal `3.30 V` (máximo absoluto **`3.63 V`**). `VSYS` (via `RT6150`) aceita `1.8 V..5.5 V`.
- **`V_PIN` máximo absoluto nos pinos digitais:** **`IOVDD + 0.5 V`** (**`3.80 V`** quando `IOVDD = 3.30 V`; **apenas `0.50 V` quando `IOVDD = 0 V`!**).
- **Atenção:** Os pads GPIO do RP2040 **NÃO são tolerantes a 5 V**. Os diodos de proteção ESD internos conduzem diretamente para `IOVDD` acima de `IOVDD + 0.5 V` (`FT` apenas protege correntes pequenas ou até `3.63 V` sob condições restritas de power-off).
- Limiares a `3.3 V`: `VIH >= 2.0 V`, `VIL <= 0.8 V`, `VOH >= 2.62 V`, `VOL <= 0.5 V` ↔ no CMOS `3.5 V` do PS1 (`VIH ~ 2.45 V`, `VIL ~ 1.05 V`) há margem lógica confortável (~`0.85 V` em alto em circuito aberto).

### 3.2 Três Riscos Elétricos Reais na Bancada (`PU-8` & `PU-18`)
1. **`+3.5V EVER` na `PU-8` (`CN602.5 = 3.54 V` sempre ligado) & *Back-Powering* [`ALTO RISCO`]:**
   - Enquanto o cabo AC da `PU-8` estiver na tomada (mesmo com o botão Power desligado), `CN602.5` permanece em `3.54 V`, e na partida há transitório entre o `+3.5V` do PS1 e o `7805 → 1N5819 → RT6150 (3.3V)` do Pico.
   - Da mesma forma, plugar o **USB no Pico** sem o diodo **`D1 (1N5819)`** ou com `PS1` parcialmente alimentado pode alimentar trilhos do PS1 via pinos GPIO (`RESET`, `DOOR`, `XLAT`, `SQCK`)! **Sempre desconecte o cabo AC antes de mexer ou plugar USB na `PU-8`.**
2. **Pino `33` do `CXD2510Q` (`VDD 5V` na `PU-8`) [`ALTO RISCO` se houver curto ou pino não-levantado]:**
   - `psx-spx` documenta explicitamente `IC309.33 = VDD 5V` na `PU-8` (`CXD2510Q`). Se um sinal não for devidamente isolado ou se um oscilador TTL/CMOS `5 V` for usado próximo a `X101`/`APLL`, aplicar `5.0 V` a um pino do RP2040 destrói o GPIO/IOVDD. Verifique na bancada com multímetro a tensão DC máxima em `XLAT`, `CMD_DATA`, `CMD_CK`, `SQCK`, `DOOR`, `RESET` e no ponto `APLL` (`DOC_G`).
3. **`SCEX_DATA` (`GPIO 4`) em Push-Pull vs. Comparador Original (`IC702`/`IC703`/`IC706 uPC5023GR-118`) [`RISCO MÉDIO`]:**
   - Atualmente `src/modchip.cpp:70` configura `GPIO 4` como saída **push-pull**. Se o ponto `DATA`/`SCEx` da placa estiver ativo em `LOW` pelo comparador (`IC706` na `PU-18` ou op-amp na `PU-8`), ocorre colisão push-pull quando `GPIO 4 = 1`. O padrão seguro (usado em `PSNee`/`Old Crow`) é **open-drain (`GPIO_OUT=0` p/ LOW e `GPIO_IN` Hi-Z p/ soltar)** com pull-up/resistor série (`>= 470 Ω..1 kΩ`).
4. **Integridade de Sinal em Fiação Solta (`67.8 MHz CLK`, `30 MHz SD_SCK`):**
   - Fios longos (> 10–15 cm) sem plano de retorno GND geram *ringing* indutivo (`> 3.8 V` no Pico ou erro de leitura `CRC` no SD a `30 MHz`). Recomenda-se resistores série de amortecimento (`22–47 Ω` em `CLK`/`DA15`/`DA16`/`LRCK`/`SD_SCK`, e `100–470 Ω` nas entradas de `3.5 V` do PS1).

---

## 4. Correções já Aplicadas na Fase 1 & Roadmap Progressivo para o `PicoStation-main` `[CONFIRMADO]`

- **Correções auditadas e compiladas com 0 warnings na Fase 1 (`HANDOFF.md` / `docs/source-changes.patch`):**
  1. Cálculo de zona pós-clamp (`cur_zone = 15` correto em `c_sectorMax = 333000`, corrigindo seek reverso off-by-one em jogos como *Resident Evil*).
  2. Flag `-DPICOSTATION_SUBQ_CRC=ON` (CRC-16-CCITT real nos 12 bytes SubQ em vez de `0xBEEF`).
  3. Bit-bang exato de `SCEx` a **`250.0 bps` `8N2` invertido** (`-DPICOSTATION_SCEX_REGION=2` p/ `SCEI`, histerese `17`), eliminando erro `+3.456%` da UART1 no RC5.
  4. Proteção contra colisão de UART0 em `CMakeLists.txt` (lembrar que `PICO_DEFAULT_UART_TX/RX = 0/1` colide com `XLAT=0`/`SQCK=1` se `DEBUG_LOGGING_ENABLED=1`).
- **Roadmap técnico recomendado para este workspace:**
  1. **Imediato (Pré-teste físico na `PU-8 OC/SCEI`):** Medir tensões de bancada do `DOC_G` (conferir se `X302` existe ou se exige porta do `GPIO 3` PWM de `4.000 MHz` do RC5 no `src/`, e conferir `APLL`/`X101` e `IC309` a 3.5V vs 5V); converter `SCEX_DATA` para modo open-drain opcional; corrigir `initHW()` em `picostation.cpp` para checar `!= NOT_USED (31)` antes de inicializar `I2C0`.
  2. **Médio Prazo (Port & Estabilidade `PU-18`):** Auditar polaridade de `XBUSY` (`$4X` invertido em `cmd.h`), tratar eventuais `$39xx` se acessados por `vC2` em diagnósticos, testar `Si5351` DFO para jogos PAL em `SCPH-5500 SCEI`, e considerar redução de `SD_BAUD_RATE` de `30 MHz` para `20–24 MHz` com resistor série se surgirem erros CRC em cabos longos.
  3. **Longo Prazo:** Implementar suporte `GameID` (`0x81 / 0x21`) em `SIO0` para integração com `MemCard Pro`/`PicoMemcard+`.
