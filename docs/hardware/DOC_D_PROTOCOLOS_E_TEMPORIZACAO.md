# Documento D — Arquitetura Lógica, Barramentos Internos e Protocolos de Hardware (PS1 / PSone)

> Todas as definições de endereços, registradores, temporizações em ciclos e formatos de quadro abaixo foram extraídas de `psx-spx.consoledev.net`, `psdevwiki.com/ps1/*`, datasheets `CXD2510Q`/`CXD2545Q`/`CXD1815Q` e inspeção direta do código-fonte `PicoStation-main`.

---

## 1. CPU (`MIPS R3000A` / `CW33300`), Mapa de Memória, BIU e DMA `[CONFIRMADO]`

### 1.1 Mapa Físico, Espaços Virtuais, i-Cache, Scratchpad e Write Queue
- **Núcleo:** MIPS R3051 (`CW33300`, 33.8688 MHz nominal; 33.9000 MHz com `GPIO_CLK = 67.800 MHz` no PicoStation), 32-bit, little-endian, pipeline com *load delay slot* de 1 instrução e *branch delay slot* de 1 instrução `[CONFIRMADO]`.
- **Segmentos virtuais:**
  - `KUSEG` (`0x00000000..0x7FFFFFFF`): cacheável (`i-Cache` + `write-queue`), sem TLB (mapeamento direto 1:1 para físico `[28:0]`).
  - `KSEG0` (`0x80000000..0x9FFFFFFF`): espelho cacheável de `0x00000000..0x1FFFFFFF`.
  - `KSEG1` (`0xA0000000..0xBFFFFFFF`): **não-cacheável**, ignora `i-Cache` e `write-queue` (obrigatório para I/O `0xBF80xxxx` ou leitura segura de memória alterada por DMA).
- **Memórias acopladas ao núcleo & BIU (`FFFE0130h`):**
  - **`i-Cache` de 4 KB:** 256 linhas × 4 words (`16 B`/linha), `tag = phys_addr[31:12] | valid[3:0]`. Controlado por `BCC (0xFFFE0130)`: `[0] LOCK`, `[1] INV`, `[2] TAG` (flush combinando com `COP0_SR.IsC=1`), `[3] RAM` (habilita Scratchpad), `[9:8] IBLKSZ` (`0=2w, 1=4w`), `[11] IS1`, `[13] RDPRI`.
  - **Scratchpad (`0x1F800000..0x1F8003FF`, 1 KB):** reutiliza a SRAM de d-Cache como RAM rápida 0-waitstate (acessível em `KUSEG`/`KSEG0`, ausente em `KSEG1`). **Não-executável** (tentativa de PC saltar para lá dispara Bus Error) `[CONFIRMADO]`.
  - **Write Queue de 4 words:** escritas da CPU enfileiram na write-queue e liberam o pipeline enquanto o barramento escreve na RAM/I/O (até encher os 4 slots).
- **Controlador de Memória (`0x1F801000..1020`, `1F801060`) `[CONFIRMADO]`:**
  - `0x1F801060 DRAM_CTRL`: padrão BIOS **`0x0B80`** em `PU-7` / `EARLY-PU-8` (`4× 512Kx8`, pino `43 = /CAS`, pinos `46..49 = /WE3..0`) e **`0x0B88`** (bit 3 = 1) a partir de `LATE-PU-8` (remapeia pino `43 = /WE` e `46..49 = /CAS3..0`). **Bit 7** adiciona waitstate em acessos consecutivos e **causa travamento/hang de carregamento CD-ROM na PU-8 se mal configurado** `[CONFIRMADO]`.
  - Temporizações de `SBUS`: `DEV0` PIO (`0x1F801008 = 0x0013243F`, base `0x1F000000`), `DEV1` (`0x1F80100C = 0x00003022`), `DEV2` BIOS 512KB (`0x1F801010 = 0x0013243F`, `0x1FC00000`), `DEV4` SPU (`0x1F801014 = 0x200931E1` escrita / `0x220931E1` leitura, 16-bit), **`DEV5` CD-ROM (`0x1F801018 = 0x00020843` / `0x00020943`, 8-bit, `bit 13 = 0` Auto-Inc OFF para 4 leituras consecutivas no mesmo byte `RDDATA` em DMA3)**, `DEV8` (`0x1F80101C = 0x00070777`, `0x1F802000`), `COM_DELAY` (`0x1F801020`: recovery/hold/floating/pre-strobe = `0x00031125` / `0x0000132C`).

### 1.2 Controlador de DMA (7 Canais, `0x1F801080..10F4`) e Arbitragem `[CONFIRMADO]`
- **Canais (`D#_MADR`, `D#_BCR`, `D#_CHCR`):**
  - `DMA0` MDECin (`1 clk/w`, `CHCR=0x01000201`); `DMA1` MDECout (`1 clk/w`, `0x01000200`).
  - `DMA2` GPU (`1 clk/w`): SyncMode 1 bloco VRAM (`BS <= 10h` em GPU v0; até `8C0h` em GPU v2); SyncMode 2 **Linked-List** (`0x01000401`, header 32-bit `NPTR[23:0] | WordCount[31:24]`, fim em `0xFFFFFF`).
  - **`DMA3` CD-ROM** (`0x1F8010Bx`): SyncMode 0 burst (`0x11000000`) ou chopped (`0x11400100`); velocidade real **`24 clks/word`** na BIOS ou **`40 clks/word`** na maioria dos jogos `[CONFIRMADO]`.
  - `DMA4` SPU (`0x1F8010Cx`, `BS <= 10h`, `4 clks/w` teórico, `0x01000201` write / `0x01000200` read).
  - `DMA5` PIO Expansion (`20 clks/w`); `DMA6` Clear Ordering Table / OTC (`1 clk/w`, `0x11000002`, decremento `-4`).
- **Prioridade e IRQs:** `DPCR (0x1F8010F0)` inicia em `0x07654321` (menor dígito = maior prioridade); `DICR (0x1F8010F4)` controla máscaras `[22:16]`, enable global `[23]`, flags W1C `[30:24]` e **IRQ3** na transição `0->1` de `[31]`. DMA acessa RAM em **DRAM Hyper Page Mode** (~17 ciclos por 16 words vs ~7 ciclos/word na CPU).

### 1.3 Interrupções (`I_STAT`/`I_MASK`) e Timers (`T0..T2`) `[CONFIRMADO]`
- `0x1F801070 I_STAT` (ack escrevendo `0` no bit correspondente) / `0x1F801074 I_MASK`: `IRQ0 VBLANK`, `IRQ1 GPU`, **`IRQ2 CDROM`**, `IRQ3 DMA`, `IRQ4..6 TMR0..2`, **`IRQ7 Controller/MemCard`**, `IRQ8 SIO`, `IRQ9 SPU`, `IRQ10 Lightgun/PIO`. Todos os IRQs são **edge-triggered** no controlador de CPU: o software deve reconhecer `I_STAT` antes de esperar novo disparo.
- Timers `0x1F801100..112F`: `T0` usa `SYSCLK` ou `DOTCLK` (`IC103.160`); `T1` usa `SYSCLK` ou `HBLANK` (`IC103.159`); `T2` usa `SYSCLK` ou `SYSCLK/8`.

---

## 2. GPU, SPU e MDEC (Comunicação e Fluxo Interno) `[CONFIRMADO]`

### 2.1 Interface CPU ↔ GPU ↔ VRAM
- Endereços `0x1F801810` (**GP0**: comandos de desenho, envio de textura, transferência VRAM↔CPU, blitting, DMA2) e `0x1F801814` (**GP1 / GPUSTAT**: reset, FIFO, vídeo NTSC/PAL, DMA dir, `GPUREAD` retorna resposta de info/VRAM read).
- **VRAM 1 MB** (`1024×512` halfwords): **não é visível diretamente** ao barramento de dados da CPU. **Diferença crítica de hardware:**
  - `v0` (`CXD8514Q`, `PU-7` / `EARLY-PU-8`): Dual-Port VRAM, truncamento 5:5:5 em textura Gouraud, `GP0(02h)` falha em `xpos[3:0]==0Fh`, `DMA2` limitado a `BS <= 10h` (`FIFO=64 B = 16 words`).
  - `v2` (`CXD8561xQ`/`CXD9500Q`, `LATE-PU-8+`): SGRAM normal 32-bit, dithering completo, `X/Y flip` (`GP0(E1h).12-13`), `DMA2` estável até `8C0h`.

### 2.2 Interface CPU / CD-ROM ↔ SPU ↔ Áudio Analógico
- Mapeada em `0x1F801C00..1DFF` (barramento 16-bit `SBUS`). Suporta **24 vozes ADPCM (blocos BRR 16 B)** a `44.1 kHz` e Reverb digital **Schroeder** a `22.05 kHz`.
- **SPU-RAM (512 KB):** `0x00000..0x003FF` buffer de captura `CD Audio L`; `0x00400..0x007FF` captura `CD Audio R`; `0x00800..0x00FFF` vozes 1 e 3; `0x01000+` amostras e buffer de reverb `[CONFIRMADO]`.
- Áudio CD chega digitalmente pelo barramento I²S do `CXD1815Q` (`BCKO`, `LRCO`, `DATO` → `IC308 CXD2925Q` pinos `91..93 BCIA/LRIA/DTIA`, ou já interno em `CXD2938Q`/`CXD2941R`). Escritas CPU na SPU devem ser **16-bit pares** (8-bit ímpar é ignorada; 32-bit é instável).

---

## 3. Subsistema CD-ROM: CPU ↔ `CXD1815Q` ↔ `HC05` ↔ DSP (`CXD2510Q`/`CXD2545Q`) `[CONFIRMADO]`

### 3.1 Registradores Host (`0x1F801800..1803`, 4 Bancos) e Anomalias Reais
- `0x1F801800` (`HSTS` leitura / `INDEX[1:0]` escrita): `[7] BUSYSTS`, `[6] DRQSTS` (FIFO de dados pronta), `[5] RSLRRDY` (FIFO resposta pronta), `[4] PRMWRDY`, `[3] PRMEMPT`, `[2] ADPBUSY`.
- Banco 0: `1801W=COMMAND`, `1802W=PARAMETER` (**FIFO real de 16 bytes**), `1803W=HCHPCTL` (`[7] BFRD` libera leitura `RDDATA`, `[6] BFWR`, `[5] SMEN`), `1801R=RESULT` (16B), `1802R=RDDATA` (8/16b; último bloco repete último byte/padding se lido após `0x800`/`0x924`), `1803R=HINTMSK`.
- Banco 1: `1802W=HINTMSK`, `1803W=HCLRCTL` (`[2:0] CLRINT`, `[4:3] CLRBF`, `[6] CLRPRM`, `[7] CHPRST`), `1803R=HINTSTS` (`INT1`=setor pronto, `INT2`=fim/completo, `INT3`=ack imediato, `INT4`=data end/pause, `INT5`=erro).
- **Bugs / Peculiaridades críticos para emulação ODE:**
  1. **Bug de Glitch no `HINTSTS` (Placas antigas como `LATE-PU-8`):** bits `0..2` não são sincronizados com o clock da CPU e podem ler transições falsas `0→1→3` (~0.1%) ou `0→4→5` (~1%). Jogos/BIOS precisam ler duas vezes em polling `[CONFIRMADO]`.
  2. **SRAM de Buffer (`IC303`, 32 KB = 8 slots de setor):** apenas **2 slots completos (+1 em recepção)** são expostos efetivamente ao host (`INT1`) porque o HC05 mantém só bitflags simples sem fila FIFO. Sem drenar com `BFRD`, salta para o último setor e no 9º/17º setor sobrescreve sem flag de overrun `[CONFIRMADO]`.
  3. **Comandos inválidos (`1Dh`, `1Fh`, `5xh`)** retornam `INT5` sem limpar a `PARAMETER` FIFO (`CLRPRM` obrigatório).

### 3.2 Comandos CD-ROM (`01h..1Fh`, `19h,xxh`, Unlock `50h..57h`) & Tempos em Ciclos 33 MHz
- **Comandos principais:** `01h Nop`, `02h Setloc(m,s,f BCD)`, `03h Play`, `04h/05h Forward/Backward`, `06h ReadN`, `07h MotorOn`, `08h Stop`, `09h Pause`, `0Ah Init`, `0Bh/0Ch Mute/Demute`, `0Dh Setfilter`, `0Eh Setmode` (`[7] Speed 2x`, `[6] XA-ADPCM`, `[5] 2340B sector`, `[4] Ignore`, `[3] XA Filter`, `[2] Report`, `[1] AutoPause`, `[0] CDDA`), `0Fh Getparam`, `10h GetlocL`, `11h GetlocP`, `12h Setsession`, `13h GetTN`, `14h GetTD`, `15h/16h SeekL/SeekP`, `19h Test`, `1Ah GetID`, `1Bh ReadS`, `1Ch Reset`, `1Dh GetQ` (`vC1+`), `1Eh ReadTOC` (`vC1+`), `1Fh VideoCD` (`PU-16`).
- **Subcomandos `19h,xxh` chave:** `04h/05h` reset/read contadores SCEx (usados pelo anti-modchip!); `20h` data/versão HC05; `21h` estado `POS0/DOOR`; `22h..25h` strings de região/chipset; `50h` envia comando raw `CX(...)` ao DSP (com parâmetros opcionais); `51h` (`vC2+ PU-18`) executa `CX(39xx)` c/ leitura; `60h` lê RAM/IO do HC05 (`0x000..0x03F` I/O, `0x040..0x23F` RAM); `71h..76h` lê registradores/SRAM do `CXD1815Q`.
- **Unlock Backdoor (`50h..56h`):** Aceita strings `"Licensed by" "Sony" "Computer" "Entertainment" <região>` apenas em `vC1+` não-japonesas (USA/EUR); retorna sempre `INT5(11h,40h)`; **`58h..5Fh` travam/crasham o HC05**.
- **Tempos reais medidos (ciclos de 33.8688 MHz, PSone PAL):** `Nop` avg `0x0C4E1` (parado `0x05CF4`); `Init` avg `0x13CCE`; `GetID` 2ª resp `0x04A00`; `Pause` 1x `0x21181C` / 2x `0x10BD93` / pausado `0x01DF2`; **`Read INT1` 1x** = `451,584` ciclos (`0x70800`, 75 Hz = `13,333.3 µs`); **`Read INT1` 2x** = `225,792` ciclos (150 Hz = `6,666.7 µs`).

---

## 4. Protocolo Interno HC05 ↔ DSP/Servo (`DATA`, `CLOK`, `XLAT`, `SENS`, `SCOR`, `SQCK`, `SQSO`)

### 4.1 Diagrama de Sequência do Boot & Leitura entre CPU, HC05 e PicoStation

```mermaid
sequenceDiagram
    participant CPU as CPU IC103 / CXD1815Q
    participant HC05 as Mechacon IC304 (HC05)
    participant ODE as PicoStation (Emula DSP + SubQ + Drive)

    Note over ODE: Core 0 garante 300ms /RES=0, aguarda 30ms CMD_CK=1
    HC05->>ODE: CMD_CK/DATA LSB-first (EX: $8x ModeSpec, $9x DCLV/2x, $Ex CLV)
    HC05->>ODE: XLAT falling edge (latch cmd; nibble alto seleciona SENS mux)
    ODE-->>HC05: SENS pin atualizado (XBUSY/FOK/GFS/COUT)
    Note over HC05,ODE: AutoSeq Focus On ($47) -> PicoStation aguarda 15ms -> XBUSY reset, FOK=1
    HC05->>ODE: Seek Sled / Track Jump ($2x/$Bx/$Cx/$7xxxx)
    ODE-->>HC05: SENS ($CX) alterna COUT a cada 256 trilhas (B01000)
    Note over ODE: Em wobble (0 < setor < 4500), PicoStation envia SCEx 250 bps 8N2 via GPIO4
    loop Cada Setor (75 Hz 1x / 150 Hz 2x)
        ODE-->>CPU: PWM DA15 (2.11875/4.2375 MHz) + LRCK (44.14/88.28 kHz)
        ODE-->>CPU: DMA -> PIO0 SM0 -> DA16 (2352 B/setor 24b right-justified), C2PO=GND
        ODE-->>HC05: Pulso SCOR=1 em GPIO18 (SQSO sobe no inicio)
        HC05->>ODE: 96 pulsos SQCK (GPIO1)
        ODE-->>HC05: 96 bits SubQ LSB-first em SQSO (10B + CRC16)
        HC05->>CPU: DEC_WR/CS para CXD1815Q -> DMA3/IRQ2 (INT1)
    end
```

### 4.2 Serial Síncrona de Comandos (`DATA`, `CLOK`, `XLAT`, `$0x..$Ex`) `[CORROBORADO]`
- Transmitida **LSB-first**, até **24 bits** por palavra (ou 8b/16b, deslocados para o MSB `[23:20]` no shift register do DSP). Os 4 últimos bits recebidos selecionam instantaneamente o multiplexador `SENS` antes mesmo de `XLAT` descer.
- **Resumo de famílias `$0x..$Ex`:**
  - `$0x`: Focus control; `$1x`: Tracking/Brake/Sled kick; `$2x`: Tracking/Sled mode (`[19:18] Tracking`, `[17:16] Sled 0=Off,1=On,2=F-Fast,3=R-Fast`);
  - **`$3x` (diferença chave `PU-8` vs `PU-18`!):** na `PU-8` (`CXA1782BR`), `$30..$3F` ajusta Balance/Gain `0..7`; na `PU-18` (`CXD2545Q`), `$34` escreve Coeff RAM `K00..K4F`, `$35..$38` executam Auto-Focus/Tracking Gain & Offset (`AGF/AGT`), `$39xx` lê registradores/ADC via `SENS` (`19h,51h`);
  - `$4x`: Auto Sequence (`$40` Cancel, `$44/$45` Fine Search, `$47` Focus-On, `$48/$49` 1-Trk Jump, `$4A/$4B` 10-Trk Jump, `$4C/$4D` 2N-Trk Jump, `$4E/$4F` 1N Move no `CXD2545Q`);
  - `$5x`/`$6x`: Timers de Blind/Brake/SledKick; `$7xxxx`: 16-bit Track Jump Count (`N` trilhas);
  - `$8x`: Mode Spec (`[19] CDROM`, `[18:17] DOUT Mute`, `[15] VCOSEL`, `[14] ASHS`, **`[13] SOCT`**);
  - `$9x`: Function Spec (`[19] DCLV`, **`[18] DSPB` 0=1x / 1=2x**, `[17] ASEQ`, `[16] DPLL`);
  - `$Ax`: Áudio Digital DSP; `$Bxxxx`: Traverse Monitor (`B01000` → pulsa `COUT` a cada 256 trilhas); `$Cx`: Spindle coeff; `$Dx/$Ex`: Spindle/CLV (`E0 Stop`, `E6 CLVA`, `E8 Kick`, `EA Brake`, `EC CLVH`, `EE CLVS`, `EF CLVP`).

### 4.3 Tabela `SENS` Hardware Sony vs. Firmware `PicoStation` (`include/cmd.h`) `[CORROBORADO]`

| Nibble | Hardware `CXD2510Q`/`CXD2545Q` (`ASEQ=0/1`) | Inicial `cmd.h` | Comportamento `src/cmd.cpp` & Divergência |
|---|---|---|---|
| `$0X`–`$2X` | `FZC`, `AS`, `TZC` | `1, 1, 1` | Estáticos em `1` (HC05 checa principalmente em servo físico) |
| `$3X` | `SSTP / AGOK / XAVEBSY ($38) / RegRead ($39xx)` | `1` | Estático em `1` (`$39xx` do `CXD2545Q` não emulado) |
| **`$4X`** | **`XBUSY` (`0` = ocupado em AutoSeq, `1` = livre)** | **`1`** | **Invertido no `src/`!** `FOCUS_ON` põe `1` por 15 ms e depois `0` (`resetXBUSY()`). Funciona com HC05 esperando transição ou `CLV`, porém diverge do datasheet Sony `[CONFIRMADO]`. |
| `$5X` | `FOK` (`1` = Focus OK) | `0` | Sobe para `1` em `ASEQ FOCUS_ON` (`cmd.cpp:107`) |
| `$6X`–`$9X` | `SEIN` (`$6X`) / `0` (`ASEQ=1`) | `1, 1, 1, 1` | Estáticos em `1` |
| `$AX` | `GFS` (`1` = Good Frame Sync) | `0` | `1` em `CLVS/H/P/A`, `0` em `CLV STOP` (habilita `SCOR`/SubQ) |
| `$BX` | `COMP` (Traverse counter == target) | `0` | Estático em `0` |
| `$CX` | `COUT` (Toggle cada `N` trilhas em `$Bxxxx`) | `0` | Alternado em `moveSled()` cada 256 trilhas (`drive_mechanics.cpp`) |
| `$EX` | `/OV64` (`0` = EFM > 64T) | `0` | Estático em `0` |

### 4.4 Barramentos `SCOR`/`SQCK`/`SQSO`/`SOCT` e `I2S` (`DA15`/`DA16`/`LRCK`) `[CONFIRMADO]`
- **`SubQ` (12 bytes = 96 bits):** 10 bytes de controle/TOC/MSF (`ctrladdr, tno, point/index, min, sec, frame, zero, pmin/amin, psec/asec, pframe/aframe`) + **2 bytes CRC-16-CCITT big-endian** (polinômio `0x1021`, invertido; no `src/` padrão envia `0xBEEF` a menos que `-DPICOSTATION_SUBQ_CRC=ON`; no `RC5` binário sempre calcula CRC real `[CONFIRMADO]`). Enviado **LSB-first por byte** pela SM `subq` após pulso `SCOR` (`75 Hz` em 1x, `150 Hz` em 2x). Note-se o suporte opcional a **LibCrypt** (setores específicos com bits de CRC propositalmente corrompidos por setor, exigindo `.sbi` ou tabela de setores).
- **`SOCT` (`$8x` bit 13):** No hardware real envia bits de status interno (`PER0..7, C1F, C2F, FOK, GFS, LOCK`); no PicoStation envia 17 bits `1` (`0xFFFFFFF` truncado por `x=16`) e usa padrão de transições (`10 -> 255 -> 10`) como heurística para `skip_bootsector` (`include/cmd.h`) `[CONFIRMADO]`.
- **`I2S` (`DA15`, `DA16`, `LRCK`):** Configurado pelo `CXD1815Q` (`DRVIF=0x00`): **`48 BCLKs` por quadro estéreo (`24 BCLKs`/canal L/R), MSB-first, canal L com `LRCK=0`**. No PicoStation: `LRCK` `wrap=1535`, `DA15` `wrap=31` (razão `48:1`), e `scramble_data()` empacota cada sample 16-bit como `(word16 << 8)` (bits `[31:24]=0` + 16 bits úteis `[23:8]` — formato **24-bit right-justified Sony**) em 1176 dwords (`2352 bytes`/setor) por quadro DMA `[CONFIRMADO]`.

---

## 5. Proteção `SCEx`, Anti-Modchip e Interfaces Externas (`SIO0`, `SIO1`, `PIO`, `AV`) `[CONFIRMADO]`

### 5.1 `SCEx` e Detecção Anti-Modchip
- **Wobble / `SCEx`:** 250 bps nominais (`4000 µs/bit`), quadro **8N2 invertido** (repouso/stop = `0V`, start = `+3.3/3.5V`, bits de dados LSB-first invertidos), 44 bits = `176 ms`/string (`"SCEA"`, `"SCEE"`, `"SCEI"`). Apenas nos primeiros setores (`0 < sector < 4500`, região wobble) `[CONFIRMADO]`.
- **Anti-Modchip (Spyro 3, Dino Crisis, Um Jammer Lammy, etc.):**
  1. Dá `Setloc` fora da área de boot (`01:01:01` = setor `4426`), `Setmode(CDDA)` + `Mute` + `Play`.
  2. Executa `19h,04h` (`ResetSCExInfo`) → pausa longa → `19h,05h` (`GetSCExInfo`).
  3. Se o contador retornar != 0 (modchip não-stealth injetando fora do lead-in), bloqueia o jogo `[CONFIRMADO]`. Por isso o PicoStation só injeta quando `0 < sector < 4500` + `GFS=1` + histerese `c_modchipHysteresis = 17` (`~227 ms`)!

### 5.2 Protocolo `SIO0` (Controles & Memory Cards — `0x1F801040..4F`)
- SPI síncrono `~250 kHz` (`BR = 0x0088`, clock base `33.8688 MHz`), **LSB-first**, **`CPOL=1, CPHA=1`** (muda dados na descida do `SCK`, amostra na subida).
- **Handshake `/ACK` (`DSR`):** open-drain com pull-up no console; dispositivo pulsa `LOW >= 2 µs` (`2..100 µs` após cada byte, exceto no último). Glitch conhecido no PS1 exige verificar `STAT.7=0` antes de limpar `STAT.9` `[CONFIRMADO]`.
- **Endereços:** `0x01` Joypad (`0x42` read; IDs `5A41h` digital, `5A73h` DualShock analog, `5A12h` mouse, `5A23h` neGcon, `5A63h` GunCon c/ IRQ10, `5A80h` Multitap); `0x81` Memory Card (128 KB = 16 blocos × 64 frames × 128 B; `0x52` Read, `0x57` Write, `0x53` GetID; checksum XOR; atraso extra ~31.000 ciclos em cards originais Sony; base para `FreePSXBoot` e `GameID` em `MemCard Pro`/`PicoMemcard`) `[CONFIRMADO]`.
- **`SIO1` (`0x1F801050..5F`):** UART assíncrona 3.5V c/ lógica invertida por transistores `Q105`/`Q106` na PCB (Link Cable `SCPH-1040`).
- **`CN103 PIO` (`0x1F000000` DEV0):** barramento paralelo 8/16-bit de expansão ROM (512 KB header + `"Licensed by Sony..."` + `0x1F000080` post-boot hook), usado por Action Replay/GameShark, UniROM flash cart, Caetla e pelo **`PSIO`** (via pinos repurposed `31=/IRQ2`, `65=/CS5`, `5=SBEN`) `[CONFIRMADO]`.
