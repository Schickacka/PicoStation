# PicoStation & PicoMenu — Schickacka Release 1.1

Fork open-source do [megavolt85/PicoStation](https://github.com/megavolt85/PicoStation) (originalmente criado por [paulocode/picostation](https://github.com/paulocode/picostation)) combinado com o **PicoMenu Release 1.1** (menu open-source reescrito sobre o [megavolt85/picostation-menu](https://github.com/megavolt85/picostation-menu) com paridade total ao menu fechado RC5), para Raspberry Pi Pico (`RP2040` / `RP2350`) no PlayStation 1 (`PU-8` e `PU-18`).

## Estado atual

**Release 1.1** (tanto **PicoStation** quanto **PicoMenu**) compila com **0 warnings** em toda a matriz de 16 variantes (`PU-18`/`PU-8`, regiões `SCEA`/`SCEE`/`SCEI`/`Generic`, com e sem overclock `_OC`) e foi **validada em hardware real PS1 `PU-8` (`_OC`, BIOS japonesa `SCEI`, OpenWirelessMC / MemCard PRO, BlueRetro v0.7.3)** além de **6 cenários HIL dual-core (ASan + UBSan + LSan)** e testes automatizados no DuckStation.

### Validado em console físico (`PU-8 OC SCEI` + `OpenWirelessMC` + `BlueRetro`)
- **Fast Boot & Full Boot:** *Resident Evil 2 (Dual Shock)*, *Resident Evil 3: Nemesis*, *PaRappa the Rapper (BRA — tradução PT-BR com logo customizado)* e *Tomb Raider (57 arquivos `.bin` CDDA separados)* abrindo de primeira tanto em Fast Boot quanto em Full Boot.
- **PicoMenu Release 1.1:** interface em `pt_BR` (`lang=8`), capas 128×128 (`8bpp`) em pastas e `.cue`, rolagem suave sem travamentos, envio de **GameID (`0x21`)** automático para **OpenWirelessMC / MemCard PRO / SD2PSX** sem disparo espúrio de vibração no controle, suporte a cheats (`.cht`) e `config.ini` / `background.raw`.

---

## Novidades da Release 1.1

### 1. Suporte completo a jogos multi-track CDDA extremos (ex.: *Tomb Raider* com 57 `.bin`)
- **Carregamento sob demanda de trilhas de áudio (`third_party/posix_file.c`, `third_party/posix_file.h`, `src/disc_image.cpp`):** em vez de manter dezenas de `FIL` abertos simultaneamente (esgotando SRAM e levando vários segundos em `MOUNT_FILE`), o `DiscImage::load()` faz uma única varredura `f_opendir`/`f_readdir` (`DirSizeCache`) para coletar os tamanhos de todos os `.bin` em ~15 ms, mantém a trilha 1 (dados) fixa aberta e abre sob demanda com cache limitado a 1 trilha de áudio por vez.
- **Ordem Red Book / Sony no Lead-In TOC (`src/disc_image.cpp`):** os pontos de controle `A0` (primeira trilha), `A1` (última trilha) e `A2` (lead-out) agora são emitidos nos primeiros 9 setores (`~120 ms`) antes dos pontos individuais de cada trilha (em vez de após `57 × 3 = 171` setores / `2,28 s`), permitindo que o `HC05`/Mechacon reconheça imediatamente discos com 57+ trilhas no Fast Boot sem reportar *"Imagem CDDA"*.
- **Proteção contra leitura durante `MOUNT_FILE` (`include/disc_image.h`, `src/i2s.cpp`) e recuperação `CDROM_CMD_INIT` (`picomenu/src/rc5_boot.c`):** cancela o `SubQ` alarm antes de trocar o disco, protege `m_discLoading` no Core 1 e adiciona retry com `CDROM_CMD_INIT` no menu caso o Mechacon precise reescanear a TOC.

### 2. Correção de vibração espúria do controle ao abrir o PicoMenu e ao lançar jogos (`picomenu/src/controller.c`, `main.c`, `rc5_boot.c`)
- **Ordem correta de ACK no SIO0 (`controller.c`):** corrigido `exchangePacket()` e `sendPacketNoAcknowledge()` para limpar o latch interno de `/DSR` (`SIO_CTRL_ACKNOWLEDGE`) **antes** de limpar `IRQ_STAT` (`~(1 << IRQ_SIO0)`) e antes de baixar `/DTR`, além de limpar o latch de `/DSR` e drenar o FIFO RX ao sair de `sendPacketNoAcknowledge()` (onde o OWMC/MemCard PRO pulsa `/DSR` durante o envio do GameID `0x81 0x21`).
  - Anteriormente, o `IRQ_SIO0` residual fazia o `waitForAcknowledge()` seguinte retornar em `0 us` (antes do byte `0x01` terminar de ser transmitido), dessincronizando o pacote `0x01 0x42` seguinte e fazendo adaptadores como o BlueRetro lerem bytes residuais do GameID (`0x15`, `'c'`) como comando de vibração dos motores.
- **Sincronização do byte de endereço (`exchangeByte(address)`):** aguarda o término completo do shift de `0x01`/`0x81` antes de checar `/DSR`, impedindo colisão entre o byte 0 e o byte 1 (`0x42`) no barramento SIO0.
- **Inicialização limpa do barramento (`initControllerBus`, `main.c`, `rc5_boot.c`):** removido `SIO_CTRL_RESET` (alinhado ao RC5 `FUN_8001c39c`) e removida a chamada ociosa `checkMCPpresent()` na abertura do menu (que disparava sondas `0x81 0x53`/`0x81 0x20` desnecessárias antes da primeira leitura do controle), além de liberar `/DTR` e limpar `IRQ_SIO0` antes do `LoadExec`.

---

## O que também está incluído desde a Release 1

### Firmware — PicoStation (Release 1)

#### 1. Bootsector completo `LBA 0..11` para BIOS japonesa (`SCEI`) e jogos traduzidos/patchados
- **Correção de licença + logo 3D (`src/disc_image.cpp`):** quando *Correção do setor de boot* está ativada (`!skip_bootsector`), o firmware agora substitui toda a área de boot da Sony (**`LBA 0..11`**, 12 setores) a partir da imagem embarcada, em vez de apenas `LBA 0..4`.
  - Corrige o travamento permanente no logo branco da Sony em consoles japoneses (`SCEI`) com jogos traduzidos/patchados que alteram o logo 3D do PlayStation ou possuem EDC corrompido no `LBA 4` (como *PaRappa the Rapper (BRA)*).
- **Controle explícito de `skip_bootsector` (`src/cmd.cpp`, `src/i2s.cpp`):** removida a heurística antiga (`10 -> 255 -> 10`) que desativava o patch de bootsector no meio do Full Boot de jogos fora de região em BIOS `SCEI`, alinhando ao comportamento do RC5 oficial.

#### 2. Concorrência Core 0 / Core 1 sem corrida entre capas e navegação de pastas
- **Sequenciador atômico `g_fileCmdSeq` + isolamento `g_readyDataKind` (`include/picostation.h`, `src/cmd.cpp`, `src/i2s.cpp`, `src/picostation.cpp`):**
  - Elimina a condição de corrida em que um `GET_COVER` (ou `GET_CFG` / `GET_BG` / `GET_CHEAT`) terminando no Core 1 sobrescrevia um `GOTO_DIRECTORY` / `GOTO_PARENT` / `MOUNT_FILE` recém-chegado no Core 0 e devolvia a última página da pasta anterior (menu corrompido com poucos itens e botão *Voltar*).
  - Cada setor virtual (`4750` listagem, `4800` config, `4850..4857` capa, `4900..4902` cheat, `4950..4983` wallpaper) só é servido quando `g_readyDataKind` corresponde exatamente ao dado solicitado.

#### 3. Mecânica do drive, `LMTSW`, `ModChip` e boot na borda do disco
- **Atualização em tempo real em `moveSled()` (`src/drive_mechanics.cpp`):** `m_sector`, `cur_zone` e `LMTSW` (`sector > 3000`) atualizados a cada passo de trilha com barreira `__dmb()`, corrigindo tela preta após o logo da PlayStation em títulos cujo `SYSTEM.CNF`/executável fica na borda externa do disco (*Resident Evil 1/2/3*, *Biohazard 3*, *Dino Crisis 1/2*, *Spyro*, *Crash*, *MediEvil*).
- **Janela de freio (`src/cmd.cpp`) e proteção durante seek (`src/i2s.cpp`):** evita descartar buscas legítimas e bloqueia leituras antecipadas no SD durante deslocamento do sled.
- **`ModChip` (`src/modchip.cpp`, `include/modchip.h`):** injeção não-bloqueante em `250 bps 8N2` (escalada automaticamente em `_OC`), sem abortar strings `SCEX` de 44 bits no meio da transmissão, e `ModChip::requestReset()` no reset do console.
- **Limpeza de estado em `MOUNT_FILE` (`src/i2s.cpp`):** cancela `SubQ` alarm, `SOCT`, restaura áudio/velocidade e reinicia flags `skip_bootsector`/`skip_edc` antes das opções por jogo do menu.

#### 4. Suporte nativo a `.sbi` / `.lsd` (LibCrypt PAL) e precisão de SubQ / TOC
- **Carregamento automático de `.sbi` / `.lsd` (`src/disc_image.cpp`):** carrega até 64 setores protegidos em tabela estática na SRAM com busca binária O(log N).
- **Emulação fiel de `CRCF = 0` (`src/subq.cpp`):** mantém `SQSO = 0` antes do pulso `SCOR` (`4 us`) nos setores protegidos pelo LibCrypt.
- **Alinhamento MSF absoluto e Lead-out multi-track (`src/disc_image.cpp`):** corrige `A - R = 150` (`00:02:00`) e o cálculo de lead-out (`indices[0] + size`) para discos com múltiplas trilhas.

#### 5. Perfis unificados `PU-18` / `PU-8` e Overclock (`_OC`)
| Perfil CMake | `GPIO 3` (`IC304` pino 14) | `SCEX` (`8N2`) | `c_MaxTrackMoveTime` |
|---|---|---|---|
| `PU18` (padrão) | `4,2375 MHz` (`wrap=63, level=32`) | `250 bps` (`4000 us/bit`) | `29 us` |
| `PU18` + `OC=ON` | `7,5333 MHz` (`wrap=35, level=18`) | `444 bps` (`2252 us/bit`) | `15 us` |
| `PU8` | `3,9882 MHz` (`wrap=67, level=34`) | `250 bps` (`4000 us/bit`) | `29 us` |
| `PU8` + `OC=ON` | `6,4571 MHz` (`wrap=41, level=21`) | `404 bps` (`2475 us/bit`) | `15 us` |

> **Atenção elétrica (`GPIO 3`):** nunca ligue `GPIO 3` (`MECHACON_CLK`) ao pino 14 (`OSC1`) do `IC304` enquanto `IC701.68` (`PU-18`) ou o cristal `X302` (`PU-8`) ainda estiver conectado à placa.

---

### Menu Open-Source — PicoMenu (Release 1 — `picomenu/`)

- **Paridade completa com o RC5 + código-fonte 100% aberto (`picomenu/src/`):**
  - **Fonte `8bpp` Latin-1 (13 px) e tradução nativa em `pt_BR` (`lang=8`):** suporte completo a acentuação (`á, é, í, ó, ú, ã, õ, ç...`), além dos outros 9 idiomas compatíveis com `config.ini`.
  - **Tela de Créditos customizada:** exibe `PicoStation (Release 1)`, `PicoMenu (Release 1)`, `Por: Schickacka` e agradecimentos, com atalho de bootloader RP2040.
  - **Capas assíncronas 128×128 `8bpp` (`main.c`, `gpu.c`, `psxproject/cdrom.c`):** suporte em **pastas e arquivos `.cue`**, trava `cdromBusy` até `PAUSE INT2`/`INT5`, proteção contra `INT1` espúrio em 2x (evitando overflow de DMA), retry em leitura ocupada e limpeza de cache de textura da GPU (`GP0 = 0x01000000`).
  - **Leitura de controle e GameID (`controller.c`):** protocolo SIO corrigido (`SIO_CTRL_ACKNOWLEDGE` antes de `IRQ_STAT`, proteção por IRQ, tolerância a glitch de 1 frame, suporte a Portas 0/1, DualShock, NeGcon e Mouse) — 1 linha exata por toque, sem saltos nem congelamentos. Envio incondicional de **GameID (`0x21`)** nas portas 0 e 1 com `BYTE_DELAY = 60`, 100% compatível com **OpenWirelessMC (OWMC)**, **MemCard PRO** e **SD2PSX**.
  - **Fast Boot + Full Boot + Cheats (`rc5_boot.c`, `rc5_cheats.c`, `psxproject/system.s`):**
    - Reset de kernel + trampolim `0x8000FF2C` idênticos ao RC5 oficial, preservando `DMA_DPCR` e `COP0_STATUS_CU2` (GTE) para que jogos comerciais como *Resident Evil 2/3* e *PaRappa* iniciem instantaneamente via `LoadExec`.
    - Lista de exclusão automática de overclock (8 títulos), opções por jogo (*Overclock*, *Correção EDC*, *Correção do setor de boot*), suporte a `.cht` GameShark e comandos estendidos espaçados por frame.
  - **Navegação de pastas livre de corrida (`main.c`):** envio único por comando de diretório + bloqueio de `stepAsyncCoverLoader()` no frame de ação.
  - **Imagem enxuta (`93 setores` / `214 KB`):** cabe com folga na flash do RP2040 mantendo os setores `LBA 4..11` oficiais da Sony intactos.

---

## Como compilar

### 1. Compilar o PicoMenu (`picomenu/`) — opcional (binário pré-compilado já incluído em `picomenu/picostation-menu.bin`)
Requer `mipsel-none-elf-gcc`, `cmake`, `ninja` e `mkpsxiso`:
```sh
cd picomenu
cmake -B build -G Ninja
cmake --build build
cp build/picostation-menu.bin ./picostation-menu.bin
```

### 2. Compilar o PicoStation (16 variantes `.uf2`)
Requer `cmake >= 3.24`, `ninja`, `arm-none-eabi-gcc` e [Raspberry Pi Pico SDK 2.1.1](https://github.com/raspberrypi/pico-sdk):
```sh
git submodule update --init --recursive
./tools/build_release_matrix.sh
```
As 16 variantes (`PU18`/`PU8` × `SCEA`/`SCEE`/`SCEI`/`Generic` × padrão/`_OC`) serão geradas em `releases/Release_1.1/`.

### Opções do CMake
| Opção | Valores | Descrição |
|---|---|---|
| `PICOSTATION_VARIANT` | `picostation_pico1`, `picostation_pico2`, `picostation_plus_pico1`, `picostation_plus_pico2` | Placa Pico e pinagem |
| `PICOSTATION_MAINBOARD` | `PU18` (padrão), `PU8` | Perfil de clock e temporização da placa do PS1 |
| `PICOSTATION_OVERCLOCK` | `OFF` (padrão), `ON` | Ativa perfil de overclock do Mechacon (`_OC`) |
| `PICOSTATION_SCEX_REGION` | `0`=SCEA, `1`=SCEE, `2`=SCEI, `3`=Generic (padrão) | Região da string SCEX |
| `PICOSTATION_SCEX_OPEN_DRAIN` | `OFF` (padrão), `ON` | Nível alto em alta impedância no pino `SCEX_DATA` |
| `PICOSTATION_SUBQ_CRC` | `OFF` (padrão), `ON` | Cálculo experimental de CRC-16 no SubQ (padrão mantém `0xbeef` do RC5) |
| `PICOSTATION_MENU_BIN` | `picomenu/picostation-menu.bin` (padrão) | Caminho da imagem `.bin` do menu embarcado |

---

## Créditos e agradecimentos
- **Desenvolvimento deste fork:** **Schickacka**
- **Agradecimentos:** Gemmi, megavolt85, Rama, Skitchin, Raijin, spicyjpeg, Danhans42, NicolasNoble, Maniac Vera e comunidade PSX.dev.
- **Base:** [megavolt85/PicoStation](https://github.com/megavolt85/PicoStation), [megavolt85/picostation-menu](https://github.com/megavolt85/picostation-menu) e [paulocode/picostation](https://github.com/paulocode/picostation).
- **Licença:** Apache License 2.0 ([LICENSE](LICENSE)).
