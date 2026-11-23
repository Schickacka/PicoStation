# Documento B — Inventário Completo de Componentes e Pinagens Verificadas (PS1 / PSone)

> Todas as pinagens abaixo foram transcritas e auditadas contra `psx-spx.consoledev.net/pinouts/`, `psdevwiki.com/ps1/*`, `consolemods.org/wiki/PS1:*` e `x-station/xstation-releases`. Números de pinos ou circuitos não presentes nas fontes consultadas estão explicitamente marcados como `[DESCONHECIDO]` ou `[INFERIDO]` — **nenhum pino foi inventado**.

---

## 1. CPU Principal (`IC103`) — `[CONFIRMADO]`

- **Designador / Fabricante:** `IC103` — Core MIPS R3051 (`LSI Logic CoreWare CW33300`, 33.8688 MHz, 30 MIPS) + COP0 + COP2 (`GTE`, 66 MIPS) + `MDEC` (80 MIPS) + DMA 7 canais + barramentos `RAM (32b)`, `GPU/VBUS (32b)` e `SBUS (16/8b)`, cache I-cache 4 KB + Scratchpad 1 KB `[CONFIRMADO]`.
- **Encapsulamento / Processo / Variantes por Revisão:** 208 pinos (QFP):
  - `SONY CXD8530Q` / `CXD8530AQ` (`PU-7` muito iniciais, `0.6 µm`, `128 mm²`, ~1M transistores, QFP metálico) `[CORROBORADO]`.
  - `SONY CXD8530BQ` (`PU-7`, die shrink ASIC `66 mm²`, QFP plástico) `[CORROBORADO]`.
  - `SONY CXD8530CQ` (`PU-7`, `PU-8`, `PU-16`, `66 mm²`) `[CORROBORADO]`.
  - `SONY CXD8606Q` (`PU-8` tardia, `PU-18`, `66 mm²`, 850k transistores) `[CORROBORADO]`.
  - `SONY CXD8606AQ` (algumas `PU-8` muito tardias e `PU-18`, huge die shrink `26.5 mm²`) `[CORROBORADO]`.
  - `SONY CXD8606BQ` (`PU-20`, `PU-22`, `PU-23`, `PM-41` iniciais, `26.5 mm²`) `[CORROBORADO]`.
  - `SONY CXD8606CQ` (`PM-41` tardia / `PM-41(2)`, `26.5 mm²`) `[CORROBORADO]`.
  - *(Arcade: `CXD8661R` em Capcom ZN-2 / Namco Sys 12)*.
- **Alimentação:** `VDD = +3.5V` (`~3.48–3.53V`), `GND`.
- **Agrupamento dos Barramentos:** Pinos `5..68` = Main RAM bus; `69..84` = Clocks/Reset/SIO1/SIO0; `85..152` = `SBUS`; `102, 153..206` = `GPU/VBUS` & Clocks (`SYSCK0=33MHz`, `SYSCK1=33MHz`, `DSYSCK0=67MHz`).

### Pinagem Completa `IC103` (208 pinos) `[CONFIRMADO]`

| Pinos | Sinais |
|---|---|
| `1..13` | `1:VDD`, `2:VDD`, `3:CRYSTALN`, `4:CRYSTALP`, `5:RAM.D31`, `6:RAM.D30`, `7:RAM.D29`, `8:RAM.D28`, `9:RAM.D27`, `10:RAM.D26`, `11:RAM.D25`, `12:RAM.D24`, `13:RAM.D23` |
| `14..27` | `14:VDD`, `15:GND`, `16:RAM.D22`, `17:RAM.D21`, `18:RAM.D20`, `19:RAM.D19`, `20:RAM.D18`, `21:RAM.D17`, `22:RAM.D16`, `23:RAM.D15`, `24:RAM.D14`, `25:RAM.D13`, `26:VDD`, `27:GND` |
| `28..39` | `28:RAM.D12`, `29:RAM.D11`, `30:RAM.D10`, `31:RAM.D9`, `32:RAM.D8`, `33:RAM.D7`, `34:RAM.D6`, `35:RAM.D5`, `36:RAM.D4`, `37:RAM.D3`, `38:VDD`, `39:GND` |
| `40..52` | `40:RAM.D2`, `41:RAM.D1`, `42:RAM.D0`, `43:RAM./CAS (/WE)`, `44:RAM./RAS1`, `45:RAM./RAS0`, `46:RAM./WE3 (/CAS3)`, `47:RAM./WE2 (/CAS2)`, `48:RAM./WE1 (/CAS1)`, `49:RAM./WE0 (/CAS0)`, `50:VDD`, `51:GND`, `52:GND` |
| `53..68` | `53:VDD`, `54:VDD`, `55:RAM.A11`, `56:RAM.A10`, `57:RAM.A9`, `58:RAM.A8`, `59:RAM.A7`, `60:RAM.A6`, `61:RAM.A5`, `62:RAM.A4`, `63:RAM.A3`, `64:RAM.A2`, `65:GND`, `66:VDD`, `67:RAM.A1`, `68:RAM.A0` |
| `69..84` | `69:/RC_NET (3.5V)`, `70:SIO1./RTS`, `71:SIO1./CTS`, `72:SIO1./DTR`, `73:SIO1./DSR`, `74:SIO1.TX`, `75:SIO1.RX`, `76:/EXT_RESET`, `77:SIO0./DTR2`, `78:GND`, `79:VDD`, `80:SIO0./DTR1`, `81:SIO0./SCK`, `82:SIO0./DSR`, `83:SIO0.TX`, `84:SIO0.RX` |
| `85..104` | `85:SBUS.DACK5_PIO`, `86:SBUS.DREQ5_PIO`, `87:SBUS.DACK4_SPU`, `88:SBUS.DREQ4_SPU`, `89:/IRQ10_PIO`, `90:/IRQ9_SPU`, `91:GND`, `92:VDD`, `93:/CSHTST (GND)`, `94:/IRQ2_CDROM`, `95:SBUS./CS5_CDROM`, `96:SBUS./CS4_SPU`, `97:SBUS./CS2_BIOS`, `98:SBUS./CS0_PIO`, `99:SBUS./WR1`, `100:SBUS./WR0`, `101:SBUS./RD`, `102:/IRQ1_GPU`, `103:GND`, `104:GND` |
| `105..130` | `105:VDD`, `106:VDD`, `107..116:SBUS.D0..D9`, `117:GND`, `118:VDD`, `119..124:SBUS.D10..D15`, `125..129:SBUS.A0..A4`, `130:GND` |
| `131..156` | `131:VDD`, `132..142:SBUS.A5..A15`, `143:GND`, `144:VDD`, `145..152:SBUS.A16..A23`, `153:GPU.A0`, `154:SYSCK1`, `155:GND`, `156:GND` |
| `157..182` | `157:VDD`, `158:VDD`, `159:TIMER1.CLK (HBLANK)`, `160:TIMER0.CLK (DOTCLK)`, `161..169:GPU.D0..D8`, `170:GND`, `171:VDD`, `172..181:GPU.D9..D18`, `182:GND` |
| `183..208` | `183:VDD`, `184..194:GPU.D19..D29`, `195:GND`, `196:VDD`, `197:GPU.D30`, `198:GPU.D31`, `199:/IRQ0`, `200:GPU.DREQ2`, `201:SYSCK0`, `202:GPU.DACK2`, `203:GPU./WR`, `204:GPU./RD`, `205:GPU./CS7`, `206:DSYSCK0`, `207:GND`, `208:GND` |

- **Notas de Projeto e Diferenças:**
  - `RAM.A11` (pino `55`) vai ao `A8` das DRAMs (em 512Kx32); `RAM.A8` e `RAM.A10` ficam NC `[CONFIRMADO]`.
  - CPU inicial (`PU-7`, `EARLY-PU-8`): pino `43 = /CAS` e pinos `46..49 = /WE3..0`. A partir de revisões usadas na `LATE-PU-8+`, o bit 3 de `DRAM_CTRL` (`0x1F801060`) remapeia pino `43 = /WE` e pinos `46..49 = /CAS3..0` `[CONFIRMADO]`.
  - `CRYSTALP` (pino `4`) recebe clock `67.7376 MHz` (`X101`, ou `IC204.5`, ou `80 MHz` em overclock mod com pino `4` levantado `[CONFIRMADO]` `consolemods`); `CRYSTALN` (pino `3`) permanece aberto `[CONFIRMADO]`.

---

## 2. GPU — Graphics Processing Unit (`IC203`) — `[CONFIRMADO]`

### 2.1 `IC203` Antiga 160 pinos (`SONY CXD8514Q`) — `PU-7`, `EARLY-PU-8 (-11/-12)`
- Projetada com Toshiba; cache de textura `2 KB`, FIFO `64 B`; barramentos duplos para VRAM dual-port `A` e `B`; **não possui DAC/saída RGB 24-bit integrada** (saída digital vai da VRAM ao `IC207 CXD2923AR`) `[CONFIRMADO]`.
- **Pinagem Completa (160p):**
  - `1..20`: `1:VCC`, `2:GND`, `3:/GPUCS (22Ω)`, `4:GPU.A2`, `5:/GPURD (22Ω)`, `6:/GPUWR (22Ω)`, `7:DACK2`, `8:/RESET`, `9:VCC`, `10:GND`, `11:SYSCK0 (22Ω)`, `12:VCC`, `13:GND`, `14:DREQ2`, `15:/IRQ1`, `16:HBLANK`, `17:VBLANK`, `18:high?(TP)`, `19:high?(TP)`, `20:VCC`
  - `21..64`: `21:GND`, `22..27:D31..D26`, `28:VCC`, `29:GND`, `30..35:D25..D20`, `36:VCC`, `37:GND`, `38..42:D19..D15`, `43:VCC`, `44:GND`, `45..49:D14..D10`, `50:GND`, `51:VCC`, `52..58:D9..D3`, `59:GND`, `60:VCC`, `61..63:D2..D0`, `64:GND`
  - `65..100`: `65:VCC`, `66..69:A8'a..A5'a`, `70:GND`, `71..75:A4'a..A0'a`, `76:GND`, `77:VCC`, `78..85:D15'a..D8'a`, `86:VCC`, `87:GND`, `88..95:D7'a..D0'a`, `96:VCC`, `97:DSF`, `98:/CAS'b (22Ω)`, `99:/CAS'a (22Ω)`, `100:VCC`
  - `101..140`: `101:GND`, `102:DT/OE'b (22Ω)`, `103:DT/OE'a (22Ω)`, `104:/RAS (100Ω)`, `105:/WE'a (100Ω)`, `106:/WE'b (100Ω)`, `107:/SE (220Ω)`, `108:SC (22Ω)`, `109:VCC`, `110:GND`, `111..118:D15'b..D8'b`, `119:VCC`, `120:GND`, `121..128:D7'b..D0'b`, `129:VCC`, `130:GND`, `131..139:A8'b..A0'b`, `140:VCC`
  - `141..160`: `141:GND`, `142:53MHz`, `143:VCC`, `144:GND`, `145:FSC (220+2k2)`, `146:VCC`, `147:GND`, `148:DOTCLK (22Ω)`, `149:VCC`, `150:GND`, `151:MEMCK1`, `152:MEMCK2`, `153:BLANK`, `154:/24BPP`, `155:/CSYNC (2k2)`, `156:/HSYNC (TP)`, `157:/VSYNC (TP)`, `158:VCC`, `159:GND`, `160:DSYSCK0 (22Ω)`.

### 2.2 `IC203` Nova 208 pinos (`CXD8561xQ` / `CXD9500Q`) — `LATE-PU-8 (-21+)` e posteriores
- Variantes: `CXD8561Q` (`LATE-PU-8`, dithering 24-bit e melhorias), `CXD8561AQ` (`PU-20`/`22`), `CXD8561BQ` (`PU-16`/`18`/`20`, `54 mm²`), `CXD8561CQ` (`PU-20`/`22`/`23`/`PM-41` até `-51`), `CXD9500Q` (`PM-41 -61+` e `PM-41(2)` c/ SGRAM 1 MB integrada no die) `[CORROBORADO]`.
- **Pinagem Completa (208p):**
  - `1..23`: `1:HOST./CS`, `2:HOST.A0`, `3:HOST./RD`, `4:HOST./WR`, `5:HOST.DACK`, `6:/RESET`, `7:VDD`, `8:GND`, `9:/SYSCLK`, `10:VDD`, `11:GND`, `12:HOST.DREQ`, `13:HOST./IRQ`, `14:HBLANK`, `15:GND`, `16:VDD`, `17:VBLANK`, `18:HVHLD (PU 4.7k)`, `19:GND`, `20:GND`, `21:NC`, `22:VDD`, `23:VDD`
  - `24..67`: `24..28:HOST.D31..D27`, `29:VDD`, `30:GND`, `31..36:HOST.D26..D21`, `37:VDD`, `38:GND`, `39..42:HOST.D20..D17`, `43:VDD`, `44:GND`, `45..50:HOST.D16..D11`, `51:VDD`, `52:GND`, `53..59:HOST.D10..D4`, `60:GND`, `61:VDD`, `62..65:HOST.D3..D0`, `66:GND`, `67:VDD`
  - `68..120`: `68..70:PCKSL2..0 (NC)`, `71..74:TEST3..0 (3.5V)`, `75:VDD`, `76:GND`, `77..79:SGRAM.D31..D29`, `80:VDD`, `81:GND`, `82..86:SGRAM.D28..D24`, `87:VDD`, `88:GND`, `89..95:SGRAM.D23..D17`, `96:GND`, `97:VDD`, `98..104:SGRAM.D16..D10`, `105:GND`, `106:VDD`, `107..112:SGRAM.D9..D4`, `113:GND`, `114:VDD`, `115..118:SGRAM.D3..D0`, `119:GND`, `120:VDD`
  - `121..156`: `121:SGRAM./CS1 (arcade)`, `122:SGRAM./CS0`, `123:SGRAM.DSF`, `124:SGRAM./RAS`, `125:SGRAM./CAS`, `126:SGRAM./WE`, `127:SGRAM.DQMH`, `128:SGRAM.DQML`, `129:GND`, `130:VDD`, `131:MCLKOUT`, `132:GND`, `133:VDD`, `134:MCLKIN`, `135:GND`, `136:VDD`, `137..140:SGRAM.A9..A6`, `141:VDD`, `142:GND`, `143..145:SGRAM.A5..A3`, `146:GND`, `147:VDD`, `148..150:SGRAM.A2..A0`, `151:VDD`, `152:GND`, `153:FSC`, `154:VDD`, `155:GND`, `156:CSYNC`
  - `157..208`: `157:NTSC/PAL`, `158:/VSYNC`, `159:/HSYNC`, `160..163:DAC.B0..B3`, `164:GND`, `165:VDD`, `166..169:DAC.B4..B7`, `170..173:DAC.G0..G3`, `174:GND`, `175:VDD`, `176..179:DAC.G4..G7`, `180..183:DAC.R0..R3`, `184:GND`, `185:VDD`, `186..189:DAC.R4..R7`, `190:GND`, `191:VDD`, `192:VCLK_NTSC`, `193:VDD`, `194:GND`, `195:VDD`, `196:VCLK_PAL`, `197:VDD`, `198:GND`, `199:PCK`, `200:GND`, `201:VDD`, `202:DMASK`, `203:ODE2`, `204:GND`, `205:VDD`, `206:/DSYSCK`, `207:GND`, `208:VDD`.
- **Nota de Segurança (`psx-spx`):** Medir próximo ao pino `10` com ponta de prova é historicamente a causa #1 de curtos que danificam a placa (`3.5V`/`GND` adjacentes) `[CONFIRMADO]`.

---

## 3. SPU & CIs Integrados SPU + CD-ROM (`IC308`, `IC732`)

### 3.1 `IC308 SONY CXD2922Q` / `CXD2922BQ` (100p, `PU-7`, `EARLY-PU-8`) — `[DESCONHECIDO]`
- Listada em `psx-spx` e `psdevwiki/SPU` (`CXD2922Q` em `PU-7` e `PU-8 -11`), porém tabela de pinos completa não consta nas fontes lidas (`[DESCONHECIDO]`, ver `DOC_G`).

### 3.2 `IC308 SONY CXD2925Q` (100p, `LATE-PU-8`, `PU-16`, `PU-18`, `PU-20`) — `[CONFIRMADO]`
- **Barramentos:** `1..36` CPU `SBUS`/DMA4; `45..87` SPU-RAM (`IC310`, onde `A0, A10..15, /WE1, /OE1 = NC`); `91..99` áudio serial I²S (`BCIA/LRIA/DTIA` do CD-ROM, `BCIB/LRIB/DTIB` do conector PIO, `BCKO/LRCO/DATO/WCKO` saída p/ DAC `IC402` + PIO). Pino `79 = 5V`.
- **Pinagem (100p):**
  - `1..25`: `1:D0`, `2:D1`, `3:3.5V`, `4:GND`, `5..14:D2..D11`, `15:GND`, `16..19:D12..D15`, `20..25:A1..A6`
  - `26..50`: `26:A7`, `27:A8`, `28:3.5V`, `29:GND`, `30:A9`, `31:/SPU`, `32:/RD`, `33:/WR`, `34:DACK`, `35:/IRQ`, `36:DREQ`, `37:MUTE`, `38:/RST`, `39:NC`, `40:GND`, `41:SYSCK`, `42:GND`, `43:TEST`, `44:TES2`, `45..50:RAM.D15..D10`
  - `51..75`: `51..52:D9..D8`, `53:3.5V`, `54:GND`, `55..62:D7..D0`, `63:/RAS`, `64:/CAS`, `65:GND`, `66..75:A15..A6`
  - `76..100`: `76..77:A5..A4`, `78:GND`, `79:5V`, `80..83:A3..A0`, `84:/WE0`, `85:/OE0`, `86:/WE1`, `87:/OE1`, `88:GND`, `89:XCK`, `90:GND`, `91:BCIA`, `92:LRIA`, `93:DTIA`, `94:BCIB`, `95:LRIB`, `96:DTIB`, `97:BCKO`, `98:LRCO`, `99:DATO`, `100:WCKO`.

### 3.3 `IC732 SONY CXD2938Q` (208p, SPU + CD-ROM + DAC, `PU-22`, `PU-23`, `PM-41 -11..-61`) — `[CONFIRMADO]`
- Alimentado por `3.5V` lógica e **`3.48V`** dedicado (pinos `146, 151` via `IC602`). Saídas analógicas de áudio `ZZ11` (pino `147`) e `ZZ7` (pino `150`) via capacitores a `IC405`.
- **Pinagem (208p):**
  - `1..26` (Clocks CD/PLL): `1:SCLK`, `2..3:GNDed`, `4:SBSO`, `5:WFCK (sync modchip)`, `6:GNDed`, `7:C16M`, `8:3.5V`, `9:C4M`, `10:GNDed`, `11:4.3MHz`, `12:12MHz`, `13:V16M`, `14:DOUT`, `15:LACK (44.1k)`, `16:WDCK (88.2k)`, `17:3.5Ved`, `18:LOCK`, `19:GND`, `20:MDS`, `21:MDP`, `22:3.5Ved`, `23:AVDO`, `24:ASYO`, `25:ASYI`, `26:BIAS`
  - `27..52` (RF/Servo analógico): `27:RFAC`, `28:GNDed`, `29:CLTV`, `30:PCO`, `31:FILI`, `32:FILO`, `33:VCTL`, `34:VPC02`, `35:VPC01`, `36:VC`, `37:FE`, `38:SE`, `39:TE`, `40:CE`, `41:CEO`, `42:CEI (data SCEx)`, `43:RFDC`, `44:ADIO`, `45:GND`, `46:IGEN`, `47:AVD1`, `48..49:GNDed`, `50:GND`, `51..52:GNDed`
  - `53..78` (Driver `IC722` & Sub-CPU): `53:TrckR`, `54:TrckF`, `55:FocuR`, `56:3.5V`, `57:FocuF`, `58:SledR`, `59:SledF`, `60:NC`, `61:GND`, `62:NC`, `63:GND`, `64..65:(tst)`, `66:MIRR_in(4K7->723.19)`, `67:note`, `68:(tst)`, `69:3.5V`, `70..73:(tst)`, `74:DATA(22Ω)`, `75:XLAT(22Ω)`, `76:CLOK(22Ω)`, `77:SCOR`, `78:GND`
  - `79..104` (Sub-CPU): `79:/XINT`, `80:SQCK`, `81:SQSO`, `82:SENSE`, `83..84:GND`, `85..92:CD.D7..D0`, `93:3.5V`, `94:CD/CS`, `95:CD/WR`, `96:CD/RD`, `97..99:CD.A0..A2`, `100:GND`, `101..102:CDA3..4`, `103:/CD (100Ω)`, `104:/SPU (100Ω)`
  - `105..144` (CPU `SBUS`): `105:A0`, `106:3.5V`, `107..111:A1..A5`, `112:3.5V`, `113..116:A6..A9`, `117:/IRQ2`, `118:/IRQ9`, `119:/RD (22Ω)`, `120:/WR (22Ω)`, `121:DMA4`, `122..123:GND`, `124:/SPUW`, `125..130:D15..D10`, `131:3.5V`, `132..136:D9..D5`, `137:3.5V`, `138..142:D4..D0`, `143:GND`, `144:33MHzS`
  - `145..159` (Áudio Analógico / Reset): `145:(blank)`, `146:3.48V`, `147:ZZ11`, `148..149:GND`, `150:ZZ7`, `151:3.48V`, `152:/RES`, `153:3.5V`, `154:ZZ5`, `155..158:(tst)`, `159:GND`
  - `160..208` (Sound RAM `IC310` & Teste): `160:D15`, `161:D0`, `162:D14`, `163:D1`, `164:D13`, `165:3.5V`, `166:D2`, `167:D12`, `168:D3`, `169:D11`, `170:D10`, `171:D4`, `172:D9`, `173:GND`, `174:D5`, `175:D8`, `176:D6`, `177:D7`, `178:/CAS`, `179:/WE`, `180:3.5V`, `181:/OE`, `182:/RAS`, `183..186:A8..A5`, `187:GND`, `188..192:A4..A0`, `193:3.5V`, `194:NC`, `195:(tst)`, `196:GND`, `197:(tst)`, `198..200:NC`, `201:3.5V`, `202..204:NC`, `205:GND`, `206..207:(tst)`, `208:GND`.

### 3.4 `IC732 SONY CXD2941R` (176p, `PM-41(2)`) — `[CONFIRMADO]`
- Integra **SPU-RAM de 512 KB on-chip** além do CD-DSP, Servo, Decoder, SPU e DAC de áudio (`AOUTL` pino `165`, `AOUTR` pino `168`).
- **Pinagem Completa (176p):** `1..17: DA16, DA15, DA14, VDDM0, DA13, DA12, LRCK, WDCK, VDD0, VSS0, PSSL, ASYE, GND, C4M, C16M, FSOF, XTSL`; `18..44: VDD1, GND, VPCO1, VPCO2, VCTL, FILO, FILI, PCO, CLTV, AVSSO, RFAC, BIAS, ASYI, AVDDO, ASYO, VC, CE, CEO, CEI, RFDC, ADIO, AVDD1, IGEN, AVSS1, TE, SE, FE`; `45..69: LOCK, SSTP, SFDR, SRDR, TFDR, TRDR, VSSM1, FFDA, FRDA, MDP, MDS, VDD2, VSS2, MIRR, DFCT, AVSM1, AVDM1, FOK, PWMI, FSW, MON, ATSK, FSTO, COUT, XDRST`; `70..96: DA11..08, AVSMO, AVDMO, DA07..06, VDDM1, DA05..01, WFCK, SCOR, SBSO, EXCK, SQCK, SCSY, SCLK, SQSO, SENS, DATA, XLAT, CLOK, XINT`; `97..126: A4..A0, D7..D4, VSSM2, D3..D0, XCS, XRD, XWR, HINT, XIRQ, VDDM2, XSCS, XHCS, XHRD, XHWR, DACK, DREQ, XRST, VDD3, SYSCK, VSS3`; `127..158: HD15..HD6, VDD4, HD5..HD0, VSSM3, HA9..HA4, VDD5, HA3..HA2, VSS5, HA1, HA0, VDDM3`; `159..176: XCK, DTIB, BCKO, LRCO, DAVDD0, DAREFL, AOUTL, DAVSS0, DAVSS1, AOUTR, DAREFR, DAVDD1, MUTO, DATO, MTS3..MTS0`.

---

## 4. Memórias do Sistema (`Main RAM`, `VRAM/SGRAM`, `SPU-RAM`, `CD-SRAM`, `BIOS ROM`) — `[CONFIRMADO]`

### 4.1 Main RAM / CPU-RAM (`IC106..IC109` / `IC106` — 2 MB)
1. **4× 28 pinos (512K×8 = 2 MB) — `PU-7`, `PU-8 (-11..-23)`, `PU-16`, `PU-18 (-11,-21,-52,-62)`, `PU-20 (-12,-21)`:**
   - Chips: `SEC KM48V514BJ-6` (25ns Hyper Page), `NEC 424805AL-A60` (`PU-8`, pinagem separada `[DESCONHECIDO]`).
   - Pinagem `KM48V514BJ-6` (28p): `1:VCC, 2..5:DQ0..3, 6:NC, 7:/W, 8:/RAS, 9:A9, 10..13:A0..3, 14:VCC, 15:GND, 16..20:A4..8, 21:NC, 22:/OE, 23:/CAS, 24..27:DQ4..7, 28:GND`.
2. **1× 70 pinos (512K×32 = 2 MB) — `LATE-PU-8 (-41/-42)`, `PU-18 (-31,-41,-72,-82)`, `PU-20 (-22+)`, `PU-22`, `PU-23`, `PM-41` (`-82` muda p/ footprint IOP-RAM de PS2 `[DESCONHECIDO]`):**
   - Chips: `Samsung K4Q153212M-JC60` (27ns Hyper Page) ou `Toshiba T7X16`. Nota: `/OE` (pino `43`) ligado a `GND` na `PM-41`.
   - Pinagem (70p): `1:VCC, 2..5:DQ0..3, 6:VCC, 7..10:DQ4..7, 11:NC, 12:VCC, 13..16:DQ8..11, 17:VCC, 18..21:DQ12..15, 22..26:NC(23=NC/VSS), 27:/RAS, 28..34:A0..6, 35:VCC, 36:VSS, 37..39:A7..9, 40..42:NC, 43:/OE, 44:/W, 45..48:/CAS3..0, 49:NC, 50..53:DQ16..19, 54:VSS, 55..58:DQ20..23, 59:VSS, 60:NC, 61..64:DQ24..27, 65:VSS, 66..69:DQ28..31, 70:VSS`.

### 4.2 VRAM / SGRAM (`IC201`, `IC202` — 1 MB)
1. **2× 64 pinos Dual-Port VRAM (`PU-7`, `EARLY-PU-8 -11/-12`) — `NEC uPD482445LGW-A70-S` ou `SEC KM4216Y256G-60`:** `D0..15`→GPU; `Q0..15`→`IC207 CXD2923AR`. `/LWE`+`/UWE` curtos → `/WE`.
   - Pinagem (64p): `1:VCC, 2:/DT/OE, 3:GND, 4:Q0, 5:D0, 6:Q1, 7:D1, 8:VCC, 9:Q2, 10:D2, 11:Q3, 12:D3, 13:GND, 14:Q4, 15:D4, 16:Q5, 17:D5, 18:VCC, 19:Q6, 20:D6, 21:Q7, 22:D7, 23:GND, 24:/LWE, 25:/UWE, 26:/RAS, 27..31:A8..A4, 32:VCC, 33:GND, 34..37:A3..A0, 38:QSF, 39:/CAS, 40:NC, 41:DSF, 42:GND, 43:D8, 44:Q8, 45:D9, 46:Q9, 47:VCC, 48:D10, 49:Q10, 50:D11, 51:Q11, 52:GND, 53:D12, 54:Q12, 55:D13, 56:Q13, 57:VCC, 58:D14, 59:Q14, 60:D15, 61:Q15, 62:GND, 63:/SE, 64:SC`.
2. **1× 100 pinos SGRAM (`LATE-PU-8 -21+` .. `PM-41 -51`; on-chip em `PM-41 -61+` / `PM-41(2)`):** `Samsung KM4132G271BQ-10` (1 MB) ou `K4G163222A-PC70` (2 MB, só 1 MB usado pois pino `30` fica em `GND`).
   - Pinagem (100p): `1:DQ3, 2:VDDQ, 3..4:DQ4..5, 5:VSSQ, 6..7:DQ6..7, 8:VDDQ, 9..10:DQ16..17, 11:VSSQ, 12..13:DQ18..19, 14:VDDQ, 15:VDD, 16:VSS, 17..18:DQ20..21, 19:VSSQ, 20..21:DQ22..23, 22:VDDQ, 23:DQM0, 24:DQM2, 25:/WE, 26:/CAS, 27:/RAS, 28:/CS, 29:A9(BA), 30:NC(GND), 31..34:A0..3, 35:VDD, 36..45:NC, 46:VSS, 47..51:A4..8, 52:NC, 53:DSF, 54:CKE, 55:CLK, 56:DQM1, 57:DQM3, 58:NC, 59:VDDQ, 60..61:DQ8..9, 62:VSSQ, 63..64:DQ10..11, 65:VDD, 66:VSS, 67:VDDQ, 68..69:DQ12..13, 70:VSSQ, 71..72:DQ14..15, 73:VDDQ, 74..75:DQ24..25, 76:VSSQ, 77..78:DQ26..27, 79:VDDQ, 80..81:DQ28..29, 82:VSSQ, 83..84:DQ30..31, 85:VSS, 86..95:NC, 96:VDD, 97..98:DQ0..1, 99:VSSQ, 100:DQ2`.

### 4.3 SPU-RAM (`IC310` — 512 KB, 40p) e CD Buffer SRAM (`IC303` — 32 KB, 28p)
- **SPU-RAM `IC310` (40p):** `Toshiba TC51V4260DJ-70` (PseudoSRAM `3.5V`, `PU-8`), `EliteMT M11B416256A-35J` (SOJ `5.0V`, `PM-41`), `Nippon Steel NN514256ALTT-50` (TSOP-II 18mm, exclusivamente `PU-23 -41`); on-chip em `CXD2941R` (`PM-41(2)`). `/CASL` e `/CASH` ligados em curto → `/CAS`.
  - Pinagem (40p): `1:VCC(3.5/5V), 2..5:I/O0..3, 6:VCC, 7..10:I/O4..7, 11..12:NC, 13:/WE, 14:/RAS, 15:NC, 16..19:A0..3, 20:VCC, 21:VSS, 22..26:A4..8, 27:/OE, 28:/CASH, 29:/CASL, 30:NC, 31..34:I/O8..11, 35:VSS, 36..39:I/O12..15, 40:VSS`.
- **CD Buffer SRAM `IC303` (28p):** `HM62W256LFP-7T` (`PU-8` face inferior), `SONY CXK5V8257BTM` (`PU-18`); on-chip a partir do `CXD1817R` (`PU-20+`). `3.5V`.
  - Pinagem (28p): `1:A14, 2:A12, 3..10:A7..A0, 11..13:D0..2, 14:GND, 15..19:D3..7, 20:/CS, 21:A10, 22:/OE, 23:A11, 24:A9, 25:A8, 26:A13, 27:/WE, 28:VCC`.

### 4.4 BIOS ROM (`IC102` — 512 KB ou 1 MB em `PU-16`)
- **40 pinos (`PU-7`, `EARLY-PU-8`, modo 8-bit `/BYTE=GND`):** `1:A18, 2..9:A8..A1, 10:/CS, 11:GND, 12:/OE, 13:D0, 14:D8, 15:D1, 16:D9, 17:D2, 18:D10, 19:D3, 20:D11, 21:VCC, 22:D4, 23:D12, 24:D5, 25:D13, 26:D6, 27:D14, 28:D7, 29:A0(D15), 30:GND, 31:GND(/BYTE), 32..40:A17..A9`.
- **44 pinos (`PU-16`, `SONY M538032E-02`, 1 MB):** `1:NC, 2:A19, 3:A18, 4..11:A8..A1, 12:/CE, 13:GND, 14:/OE, 15..22:D0,D8,D1,D9,D2,D10,D3,D11, 23:VCC, 24..30:D4,D12,D5,D13,D6,D14,D7, 31:D15/A0, 32:GND, 33:/BYT, 34..42:A17..A9, 43..44:NC`.
- **32 pinos (`LATE-PU-8 -22+`, `PU-18..PM-41(2)`):** `M534031C-25`, `SONY 2022` (`PU-8 -23`), `SONY 2030` (`PU-18`), `M534031E-47` / `M27V401D-41` (`PM-41`, onde pino `1` vai a `A19`).
  - Pinagem (32p): `1:A19, 2:A16, 3:A15, 4:A12, 5..12:A7..A0, 13..15:D0..2, 16:GND, 17..21:D3..7, 22:/CE(/BIOS), 23:A10, 24:/OE(/RD), 25:A11, 26:A9, 27:A8, 28:A13, 29:A14, 30:A17, 31:A18, 32:3.5V`.

---

## 5. Subsistema CD-ROM & Mechacon (`IC304`, `IC305`, `IC309`, `IC701`, `IC702`, `IC703`, `IC720`, `IC722`, `IC723`)

### 5.1 Mechacon / Sub-CPU (`IC304` — Motorola 68HC05, `3.5V`, Clock interno `OSC/2`) `[CONFIRMADO]`
- **52 pinos (`MC68HC05G6`, `LATE-PU-8` em diante):** Clock `4.000 MHz` (`X302` em `PU-8`/`PU-16`) ou `4.2336 MHz` (`IC701.68` em `PU-18`, `CXD1817R` em `PU-20`, `CXD2938Q.11` em `PM-41`).
  - `1..20`: `1:NC(PortF.3 / VCD AVSEL)`, `2:VDD(3.5V)`, `3..5:NC(PortE.7..5?)`, `6..10:DECA4..0(SPU102..97, PortE.4..0)`, `11:VSS(GND)`, `12:NDLY(GND)`, `13:/RES (via 5K6)`, `14:OSC1 (X302 ou 4.3MHz SPU11)`, `15:OSC2 (NC)`, `16:F-BIAS (FOK=NC em SCPH-5500, PortB.0)`, `17:CG (NC; CG em 5500, PortB.1)`, `18:LMTSW (/POS0, PortB.2)`, `19:DOOR (SHELL_OPEN, PortB.3)`, `20:TEST2 (PortB.4)`
  - `21..39`: `21:TEST1 (CL316, PortB.5)`, `22:COUT (PortB.6)`, `23:SENSE (SPU82/CXD2510Q.69, PortB.7)`, `24:SUBQ (SQSO, PortC.0)`, `25:NC (PortC.1)`, `26:SQCK (PortC.2)`, `27:SPEED (IC722.3, PortC.3)`, `28:AL/TE (MIRROR em 5500, PortC.4)`, `29:ROMSEL (SCLK em 5500, PortC.5)`, `30:/XINT (PortC.6)`, `31:SCOR (PortC.7)`, `32:VDD(3.5V)`, `33..38:DECD0..5 (CD.D0..5, PortA.0..5)`, `39:VSS(GND)`
  - `40..52`: `40..41:DECD6..7 (PortA.6..7)`, `42:NC (PortD.0)`, `43:DATA (via 22Ω, PortD.1)`, `44:XLAT (via 22Ω, PortD.2)`, `45:CLOK (via 22Ω, PortD.3)`, `46:DECCS (PortD.4)`, `47:DECWR (PortD.5)`, `48:DECRD (PortD.6)`, `49:LDON (IC723.11, PortD.7)`, `50..52:NC (VCD SIO.IN/OUT/CLK, PortF.0..2)`.
- **80 pinos (`MC68HC05L16` `PU-7`, `EARLY-PU-8`; `MC68HC705L16` `PU-9`, `DTL-H100x`):** Pinagem genérica Motorola (`VDD: 1,47`; `VSS: 16,60`; `NDLY: 17`; `/RESET: 20`; `OSC1: 21`; `OSC2: 22`; `PA0..7: 23..30`; `PB0..7: 31..38`; `PC0..7: 39..46`; `PD1..7: 50..48, 12..9`; `PE0..7: 8..2, 80`; LCD `FP/VLCD` NC). Mapeamento aos pinos PSX é `[INFERIDO]` por equivalência exata `PortX.BitY` no `DOC_C`.

### 5.2 Decoder / FIFO CD-ROM (`IC305 CXD1815Q` / `CXD1199BQ` & `IC720 CXD1817R`)
- **`IC305 SONY CXD1815Q` (100p, `PU-8`, `PU-16`, `PU-18`; `CXD1815BQ` Yaroze; `PU-7` usa `CXD1199BQ` ~99% igual em pinos/regs ao `1199AQ`):** `[CONFIRMADO]`
  - `1..21` (SubCPU HC05): `1:D0, 2:D1, 3:VDD(3.5V), 4:GND, 5..10:D2..7, 11:/CS, 12:/RD, 13:/WR, 14:/XINT, 15:GND, 16..20:A0..4, 21:TD0`
  - `22..42` (Main CPU `SBUS`): `22:/HRS(/RST), 23:/HCS(/CS5), 24..25:HA0..1, 26:HINT(/IRQ2), 27:/HRD(/RD), 28:VDD, 29:GND, 30:/HWR(/WR0), 31..38:HD0..7, 39:HDP, 40:GND, 41:HDRQ(NC), 42:/HAC(3.5V)`
  - `43..75` (SRAM 32KB `IC303`): `43..45:MA0..2, 46..47:T01..02, 48..52:MA3..7, 53:VDD, 54:GND, 55..63:MA8..16(15..16 NC), 64:/MOE, 65:GND, 66:/MWR, 67..74:MDB0..7, 75:MDBP(NC)`
  - `76..100` (Clock & I²S do DSP): `76:XTL2, 77:XTL1, 78:VDD, 79:GND, 80:CLK, 81:HCLK, 82:CKSL, 83:RMCK, 84:LRCK, 85:DATA(DA16), 86:BCLK(DA15), 87:C2PO, 88:EMP, 89:/RST, 90:GND, 91:DATO, 92:LRCO, 93:WCKO, 94:BCKO, 95:MUTE, 96..100:TD7..TD3`.
- **`IC720 SONY CXD1817R` (144p, `PU-20`):** Funde `CXD2545Q + CXD1815Q + 32KB SRAM`. Apenas **pino `49: SCOR`** está verificado nas fontes (`[DESCONHECIDO]` demais 143 pinos).

### 5.3 Servo Amp, CD DSP & Combo DSP+Servo (`IC702`, `IC309`, `IC701`) — `[CONFIRMADO]`
- **`IC702 SONY CXA1782BR` (48p, Servo + RF Amp, `PU-7`, `PU-8`, `PU-16`):** Auto-tracking + ajuste manual focus offset (`RV704`) / gain (`RV701`).
  - `1..24`: `1:FEO, 2:FEI, 3:FDFCT, 4:FGD, 5:FLB, 6:FE_O, 7:FE_M, 8:SRCH, 9:TGU, 10:TG2, 11:FSET, 12:TA_M, 13:RA_O, 14:SL_P, 15:SL_M, 16:SL_O, 17:ISET, 18:VCC, 19:CLK, 20:XLT, 21:DATA, 22:XRST, 23:C.OUT, 24:SENS`
  - `25..48`: `25:FOK, 26:CC2, 27:CC1, 28:CB, 29:CP, 30:RF_I, 31:RF_O, 32:RF_M, 33:LD, 34:PD, 35:PD1, 36:PD2, 37:FE_BIAS, 38:F, 39:E, 40:EI, 41:GND, 42:TEO, 43:LPFI, 44:TEI, 45:ATSC, 46:TZC, 47:TDFCT, 48:VC`.
- **`IC309 SONY CXD2516Q` (100p, `PU-7`):** Pinagem completa `[DESCONHECIDO]` (sabe-se via `xStation` que pinos `11, 14, 18, 72` são levantados `[CONFIRMADO]`, inferindo-se corresponder a `C2PO/SCOR/SQSO/SENS`).
- **`IC309 SONY CXD2510Q` (80p, DSP, `PU-8`, `PU-16`, `DTL-H2510`):** Atenção: Pino `33 = VDD 5V` `[CONFIRMADO]` (`23 VDDa` e `73 VDD` em `3.5V` `[CORR.]`).
  - `1..25`: `1:FOK, 2:FSW, 3:MON, 4:MDP, 5:MDS, 6:LOCK, 7:NC, 8:VCOO, 9:VCOI, 10:TEST, 11:PDO, 12:GND, 13:TEST0, 14..15:NC, 16:VPCO, 17:VCKI, 18:FILO, 19:FILI, 20:PCO, 21:GNDa, 22:VLTV, 23:VDDa, 24:RF, 25:BIAS`
  - `26..50`: `26:ASYI, 27:ASYO, 28:ASYE, 29:NC, 30:PSSL, 31:WDCK, 32:LRCK, 33:VDD 5V, 34:DA16-SDTA48, 35:DA15-SCLK48, 36:DA14-SDTA64, 37:DA13-SCLK64, 38:DA12-LRCK64, 39:DA11-GTOP, 40:DA10-XUGF, 41:DA09-XPLCK, 42:DA08-GFS, 43:DA07-RFCK, 44:DA06-C2PO, 45:DA05-XRAOF, 46..49:DA04..01-MNT3..0, 50:APTR`
  - `51..80`: `51:APTL, 52:GND, 53:XTAI, 54:XTAO, 55:XTSL, 56:FSTT, 57:FSOF, 58:C16M, 59:MD2, 60:DOUT, 61:EMPH, 62:WFCK, 63:SCOR, 64:SBSO, 65:EXCK, 66:SQSO, 67:SQCK, 68:MUTE, 69:SENS, 70:XRST, 71:DATA, 72:XLAT, 73:VDD, 74:CLOK, 75:SEIN, 76:CNIN, 77:DATO, 78:XLTO, 79:CLKO, 80:MIRR`.
- **`IC701 SONY CXD2545Q` (100p, DSP + Servo, `PU-18`):** `XTAI/XTAO = 16.9344 MHz`, `XTSL = GND`, `FSOF (pino 68) = 4.2336 MHz` → `IC304.14` (`OSC1`). Introduz auto-ajuste de foco (`RV703` manual para push-pull).
  - `1..25`: `1:SRON, 2:SRDR, 3:SFON, 4:TFDR, 5:TRON, 6:TRDR, 7:TFON, 8:FFDR, 9:FRON, 10:FRDR, 11:FFON, 12:VCOO, 13:VCOI, 14:TEST, 15:GND, 16..17:TES2..3, 18:PDO, 19:VPCO, 20:VCKI, 21:VDDa, 22:IGEN, 23:GNDa, 24:ADIO, 25:RFC`
  - `26..50`: `26:RFDC, 27:TE, 28:SE, 29:FE, 30:VC, 31:FILO, 32:FILI, 33:PCO, 34:CLTV, 35:GNDa, 36:RFAC, 37:BIAS, 38:ASYI, 39:ASYO, 40:VDDa, 41:VDD, 42:ASYE, 43:PSSL, 44:WDCK, 45:LRCK, 46:DA16-SDTA48, 47:DA15-SCLK48, 48:DA14-SDTA64, 49:DA13-SCLK64, 50:DA12-LRCK64`
  - `51..75`: `51..61:DA11..DA01, 62:XTAI, 63:XTAO, 64:XTSL(GNDed), 65:GND, 66:FSTI, 67:FSTO, 68:FSOF(4.2336M), 69:C16M, 70:MD2, 71:DOUT, 72:EMPH, 73:WFCK, 74:SCOR, 75:SBSO`
  - `76..100`: `76:EXCK, 77:SQSO, 78:SQCK, 79:MUTE, 80:SENS, 81:XRST, 82:DIRC, 83:SCLK, 84:DFSW, 85:ATSK, 86:DATA, 87:XLAT, 88:CLOK, 89:COUT, 90:VDD, 91:MIRR, 92:DFCT, 93:FOK, 94:FSW, 95:MON, 96:MDP, 97:MDS, 98:LOCK, 99:SSTP, 100:SFDR`.

### 5.4 Amplificadores de RF & Drivers BTL de Motor (`+7.5V..7.92V`)
- **RF Amps:**
  - `PU-7 / PU-8`: discreto (transistores / 8p, acoplado a `CXA1782BR`).
  - `IC703 SONY CXA1791N` (20p, marca `"A1791N"`, `PU-18`): `1:LD, 2:PD, 3:PD1, 4:PD2, 5:GND/VEE, 6:F, 7:E, 8:VR, 9:VC, 10..11:NC, 12:EO, 13:EI, 14:TE, 15:FE_BIAS, 16:FE, 17:RFO, 18:RFI, 19:/LD_ON(ON=GND), 20:VCC`.
  - `CXA2550N` (20p, `"A2550N"`, `PU-20`): pinagem `[DESCONHECIDO]`.
  - `IC723 SONY CXA2575N-T4` (20p, `"A2575N"`, `PU-22..PM-41(2)`): `1:TEIM, 2:TEIG, 3:VEE(GND), 4:E, 5:F, 6:PD2, 7:PD1, 8:PD, 9:LD, 10:VC(CL710), 11:LD_ON, 12:G_CONT, 13:RF0(CL704), 14:RFM, 15:FE(CL708), 16:TE(CL709,15K->SPU39), 17:TE0, 18:COMP+, 19:MIRR(4K7->SPU66), 20:VCC(3.48V)`.
- **Motor BTL Drivers (28p, +7.5V/+7.92V via `PS601` 800mA, heatsink GND tabs, thermal shutdown):**
  - Rohm: `IC704 BA6398FP` (`PU-7`, `EARLY-PU-8 -11`), `IC722 BA6397FP` (`LATE-PU-8`, `PU-16`, quase idêntico ao `6398`, pinos `21,22 = 6..9V` `[CORR.]`), `BA6392FP` (`PU-18`), `BA5977FP` (`PU-20/22/23`), `BA5947FP` (`PM-41`) — e equivalente Panasonic `AN8732SB` (`PU-22/23/PM-41`).
  - Pinagem `IC722 BA5947FP / AN8732SB` (28p + 2 tabs GND): `1:(->24,27), 2:SPINDLE(15K->SPU21), 3:SW(IC304.27), 4:TRK_F, 5:TRK_R, 6:FOC_F, 7:FOC_R, 8:GND, 9:NC_INT(10uF), 10:+7.5V, 11..12:FOCUS_1/2, 13:TRK_1, 14:TRK_2, 15..16:SPINDLE_1/2, 17..18:SLED_1/2, 19:+7.5V, 20:MUTE(/RES 5K6), 21:GND, 22:SLED_R, 23:SLED_F, 24:(->1), 25:(caps->1), 26:BIAS(1.75V), 27:(->1), 28:+7.5V`. (Demais BTLs sem pinagem individual nas fontes: `[DESCONHECIDO]`).

---

## 6. Vídeo (DAC/Encoders), Áudio (DAC/Amps), Clocks, Energia e Reset

### 6.1 Vídeo DAC & Encoders (`+5.0V` via `PS603` na Fat/PM-41 inicial) `[CONFIRMADO]`
- `IC207 SONY CXD2923AR` (64p, VRAM `Q`→RGB, `PU-7`, `EARLY-PU-8`): `5:R, 7:G, 9:B`, `18:MEMCK1, 19:/24BPP, 20:MEMCK2, 21:BLANK, 22:DOTCLK`, `25..59:VRAM D0..15'a/b`.
- `IC202 TDA8771H/AN/AH` / `MC141685FT` (44p, `LATE-PU-8`, `PU-16`): `3..5,8..12:R7..R0`, `13..20:G7..G0`, `21..26,29..30:B7..B0`, `31:CLK`, `36:OUTB`, `40:OUTG`, `44:OUTR`.
- `IC501 SONY CXA1645M` (24p, RGB→CVBS/S-Video, `5V`, `PU-7..PU-16`, notas em `PU-18/20`): `2..4:R/G/B_IN, 6:SCIN(FSC), 7:NPIN(NTSC=VCC,PAL=GND), 10:SYNCIN, 15:COUT, 16:YOUT, 20:CVOUT, 21..23:B/G/R_OUT`.
- `IC502 CXA2106R-T4` / `Rohm BH7240AKV` (48p, DAC 24b+Encoder, `PU-20..PM-41(2)`): `3..5:R/G/B_OUT(75Ω), 6:YOUT(75Ω+220uF), 7:COUT(75Ω+220uF), 8:VOUT(75Ω+cap), 13:NTSC/PAL(/PAL), 14:SYNCIN, 15:SCIN(4.4M via 2K2), 16..40:RGB7..0, 41:VCLK`.
- *Nota:* `CXA2075M` não consta em nenhuma fonte consultada (`[DESCONHECIDO]`).

### 6.2 Áudio DAC & Amplificadores (`+5.0V`) `[CONFIRMADO]`
- `IC402 AKM AK4309VM` (24p, `PU-7..PU-8` `[CORR.]`): `4:/PD, 5:/RST, 6:MCLK, 7:CKS, 8:BICK, 9:SDATA, 10:LRCK, 15:AOUTR, 16:AOUTL, 18:VCCa, 19:GNDa`.
- `IC405 NJM2100E (TE2)` (8p, `PU-8`, `PU-22`, `5V` via `L401`): `1:ROUT, 2:RIN-(SPU.150), 3:RIN+, 4:GND, 5:LIN+, 6:LIN-(SPU.147), 7:LOUT, 8:VCC`.
- `IC405 NJM2174` (14p c/ Mute, `PM-41`): `2:AudioL(100Ω->AV.9), 4:MUTE1(LOW), 5:MUTE2(HIGH), 7:IN-R, 11:IN-L, 13:AudioR(100Ω->AV.11), 14:VCC(5V)`.

### 6.3 Osciladores, PLL `IC204`, Reguladores, Reset e Fusíveis `[CONFIRMADO]`
- **Osciladores:** `X101 = 67.7376 MHz` (`PU-7..PU-18`/`20?`, `÷2=33.8688M`, `÷600h=44.1k`); `X201 (antigo) = 53.203425 MHz` (PAL `÷12=4.433M`) ou `53.693175 MHz` (NTSC `÷15=3.579M`); `X302 = 4.000 MHz` (`PU-7..PU-16` somente); `X201 (novo) = 17.734475 MHz` (PAL) ou `14.31818 MHz` (NTSC) + `IC204` (`CY2081 SL-509/500` ou `2294A` 8p: `1:53M, 2:GND, 3..4:X1/X2, 5:67M, 6:4.4M, 7..8:3.5V`).
- **Regulação & Reset PS1:** Fat: `IC101 M51957B` na PSU; PS one: `IC601 78M05` (`+5V`), `IC602 LP29851MX-3.5` (`+3.48V`), `IC606/607 TL594CD/T594` (PWM), `IC002`(8p)/`IC003`(5p+`Q004`). `/RES` chega via `330Ω` a CPU/GPU e `5.6kΩ` a SPU/`IC722`/`IC304`.
- **Fusíveis `KOA CCP2E` (Fat & PSone `-11..-61`) / `1608 SMT` (`PM-41(2)`):**
  - `CCP2E15` (marc `"15"`): nominal `0.6A` (fusing `1.5A`); `CCP2E20` (`"20"`): `0.8A` (`2.0A`); `CCP2E50` (`"50"`): `2.3A` (`5.0A`); `CCP2E63`: `2.5A` (`PS600` PM-41 intermediária).
  - Fat `PS601` (`800mA`→8V BTL `IC722`), `PS602` (`800mA`→8V Ctrls+PIO), `PS603` (`800mA`→5V DACs Vídeo/Áudio), `PS604` (`2.3A`→3.5V Lógica), `PS605` (`600mA`→3.5V Ctrls/MemCard).
  - PS one: `PS004` (`800mA` Ctrl 8V), `PS600` (`2.5A` em `-21..-61` / `2.0A` em `PM-41(2)`), `PS601` (`800mA` BTL), `PS602` (`-11` apenas: 8V p/ reg 3.5V Ctrl), `PS603` (`-11..-61`: 5V reg), `PS606` (`800mA` reg 3.5V).

---

## 7. Placa Filha VCD `MP-45` (`1-665-192-11` para `PU-16 SCPH-5903`) — `[CONFIRMADO]`
Encaixa via conector de 30 pinos `CN101` (`MP-45` macho) ↔ `CNnnn` (`PU-16` fêmea). Contém: `IC104 CXD1852AQ` (120p MPEG-1), `IC111 CXP10224-603R` (64p SPC700), `IC106/114 MB814260-70` (2×40p DRAM), `IC110 TDA8771AH` (44p RGB DAC), `IC107 6230FV` (20p OSD), `IC109 TLC2932` (14p PLL), `IC112 74HCT32`, `IC113 H74 7H`, `IC102 TA78M05F` (5V), cristais `X103 45MHz`, `X104 12MHz`, `X105 28.636MHz`; muxados na `PU-16` por `4053C` (Áudio & Sync/FSC) e `NJM2283` (RGB) controlados por `IC304 PortF.3` (cmd `1Fh`) `[CONFIRMADO]`.
