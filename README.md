# PicoStation — Schickacka Release 1

Fork do [megavolt85/PicoStation](https://github.com/megavolt85/PicoStation) (originalmente criado por [paulocode/picostation](https://github.com/paulocode/picostation)), um emulador de drive óptico (ODE) de código aberto baseado em Raspberry Pi Pico (`RP2040` / `RP2350`) para o PlayStation 1 (`PU-8` e `PU-18`).

## Estado atual

Esta revisão (**Release 1**) compila sem avisos (`0 warnings`) em toda a matriz de 16 variantes (`PU-18`/`PU-8`, regiões `SCEA`/`SCEE`/`SCEI`/`Generic`, com e sem overclock) e foi validada por análise de **40 traces de CD-ROM/BIOS no DuckStation** cobrindo **17 jogos NTSC e PAL**, além de **233 arquivos `.sbi` do Redump**.

A validação final em console físico (`PU-18` e `PU-8`) está em andamento. Relatos de teste em bancada real são bem-vindos.

---

## O que mudou nesta versão

### Mecânica do drive, `LMTSW` e boot de jogos na borda do disco
- **Atualização em tempo real em `moveSled()` (`src/drive_mechanics.cpp`):** `m_sector`, `cur_zone` e o pino `LMTSW` (`sector > 3000`) agora são atualizados a cada pulso de trilha (`29 us` padrão / `15 us` em overclock) sob proteção de interrupção e barreira `__dmb()`, parando corretamente nos limites `0` e `c_sectorMax`.
  - Corrige o travamento em tela preta após o logo da PlayStation em títulos cujo `SYSTEM.CNF`/executável fica próximo à borda externa do disco (como *Resident Evil 1 DC / 2 / 3*, *Biohazard 3*, *Dino Crisis 1 e 2*, *Spyro 1*, *Crash Bandicoot PAL* e *MediEvil PAL*), nos quais `CDROM: Init` ou `ReadTOC + GetID` exige `SLED_REVERSE` contínuo até `00:00:00` (`LMTSW == 0`).
- **Janela de freio (`src/cmd.cpp`):** `m_sledBreak` passou a ser consumido uma única vez dentro de uma janela de `40 ms` após a parada do sled, evitando que buscas reais seguintes sejam descartadas.
- **Proteção durante deslocamento (`src/i2s.cpp`, `src/modchip.cpp`):** leitura antecipada no cartão SD e envio de `SCEX` ficam bloqueados enquanto o sled estiver em movimento.
- **Comandos de salto e zona:** handlers `10TRK` e `MTRK` restaurados e correção do clamp de zona (`zoneForSector()`).

### Suporte nativo a `.sbi` (LibCrypt PAL) e precisão de SubQ / TOC
- **Carregamento automático de `.sbi` (`src/disc_image.cpp`):** ao montar `<jogo>.cue`, o firmware procura automaticamente `<jogo>.sbi` na mesma pasta (sem diferenciar maiúsculas/minúsculas em FAT32/exFAT), carrega até 64 setores protegidos em tabela estática na RAM (sem alocação dinâmica) e faz busca binária em SRAM.
- **Emulação fiel de `CRCF = 0` (`src/subq.cpp`):** nos setores listados no `.sbi`, o firmware mantém `SQSO = 0` antes de pulsar `SCOR` (`4 us`), reproduzindo o comportamento do `CXD2510Q`/`CXD2545Q` quando o CRC-16 do SubQ falha — o `MC68HC05` mantém a sincronia de setor, ignora o SubQ corrompido e devolve o setor anterior em `GetlocP`.
- **Alinhamento do MSF absoluto (`src/disc_image.cpp`):** corrigido `sector_abs = sector - c_leadIn` (removendo o desvio `+1` que deixava `A - R = 151` em vez dos `150` setores / `00:02:00` do padrão).
- **Lead-out em discos multi-track (`src/disc_image.cpp`):** corrigido para `leadOut64 = indices[0] + size`, eliminando o deslocamento de `+150` setores em jogos com várias trilhas (validado até 57 trilhas em *Tomb Raider*).
- **Como usar `.sbi` no cartão SD:** coloque o arquivo `.sbi` do Redump na mesma pasta do `.cue`, com exatamente o mesmo nome-base (por exemplo, `Final Fantasy VIII (Europe, Australia) (Disc 1).cue` e `Final Fantasy VIII (Europe, Australia) (Disc 1).sbi`).

### Perfis unificados `PU-18` / `PU-8`, Overclock (`_OC`) e `SCEX`
- **Clock do Mechacon (`GPIO 3`) no código-fonte (`src/picostation.cpp`):** os perfis de PWM do `GPIO 3` presentes nos binários fechados do RC5 foram portados para `src/` e controlados via CMake:

| Perfil CMake | `GPIO 3` (`IC304` pino 14) | `SCEX` (`8N2` bit-bang) | `c_MaxTrackMoveTime` |
|---|---|---|---|
| `PU18` (padrão) | `4,2375 MHz` (`wrap=63, level=32`) | `250 bps` (`4000 us/bit`) | `29 us` |
| `PU18` + `OC=ON` | `7,5333 MHz` (`wrap=35, level=18`) | `444 bps` (`2252 us/bit`) | `15 us` |
| `PU8` | `3,9882 MHz` (`wrap=67, level=34`) | `250 bps` (`4000 us/bit`) | `29 us` |
| `PU8` + `OC=ON` | `6,4571 MHz` (`wrap=41, level=21`) | `404 bps` (`2475 us/bit`) | `15 us` |

- **`SCEX` não-bloqueante (`src/modchip.cpp`):** temporização exata de `250 bps 8N2` (escalada automaticamente em `_OC`), escolha de região única (`SCEA`, `SCEE`, `SCEI`) ou genérica (`Generic`), além de modo open-drain opcional (`PICOSTATION_SCEX_OPEN_DRAIN=ON`) para evitar contenção com o `IC706` na `PU-18`.

> **Atenção elétrica (`GPIO 3`):** nunca ligue o fio do `GPIO 3` (`MECHACON_CLK`) ao pino 14 (`OSC1`) do `IC304` se `IC701.68` (`PU-18`) ou o cristal `X302` (`PU-8`) ainda estiver conectado à placa. Mantenha o `GPIO 3` desconectado ao usar o clock original do console, ou isole o oscilador/pino original antes de ligar o `GPIO 3`.

### Menu embutido e documentação de hardware
- **Menu (`binary/picostation-menu.bin`):** interface traduzida para português (`pt_BR`) dentro da tabela de glifos ASCII da fonte nativa, créditos atualizados para **Release 1** / **Schickacka** mantendo todos os colaboradores originais, com EDC/ECC Mode 2 Form 1 recalculado.
- **Documentação técnica (`docs/hardware/`):** levantamento completo de arquitetura, inventário de CIs, pontos de solda, protocolos/temporização e netlist CSV/JSON das placas `PU-8` e `PU-18`.

---

## Placas suportadas
- **PU-8** (`SCPH-100x`, primeiras `3000`/`3500`/`5000`)
- **PU-18** (`SCPH-550x`, `5552`)

---

## Como compilar

### Pré-requisitos
- `cmake` >= 3.24, `ninja` (ou `make`), `arm-none-eabi-gcc` (13.x ou 14.x)
- [Raspberry Pi Pico SDK 2.1.1](https://github.com/raspberrypi/pico-sdk) (`PICO_SDK_PATH`)
- Inicializar os submódulos:
  ```sh
  git submodule update --init --recursive
  ```

### Build individual (padrão: `PU-18`, sem overclock, região `Generic`)
```sh
cmake -S . -B build -G Ninja \
  -DPICO_SDK_PATH=/caminho/para/pico-sdk \
  -DPICO_BOARD=pico \
  -DPICOSTATION_VARIANT=picostation_pico1 \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICOSTATION_MAINBOARD=PU18 \
  -DPICOSTATION_OVERCLOCK=OFF \
  -DPICOSTATION_SCEX_REGION=3
ninja -C build
```

### Opções do CMake

| Opção | Valores | Descrição |
|---|---|---|
| `PICOSTATION_VARIANT` | `picostation_pico1`, `picostation_pico2`, `picostation_plus_pico1`, `picostation_plus_pico2` | Placa Pico e pinagem |
| `PICOSTATION_MAINBOARD` | `PU18` (padrão), `PU8` | Perfil de clock e temporização da placa do PS1 |
| `PICOSTATION_OVERCLOCK` | `OFF` (padrão), `ON` | Ativa perfil de overclock do Mechacon (`_OC`) |
| `PICOSTATION_SCEX_REGION` | `0`=SCEA, `1`=SCEE, `2`=SCEI, `3`=Generic (padrão) | Região da string SCEX |
| `PICOSTATION_SCEX_OPEN_DRAIN` | `OFF` (padrão), `ON` | Nível alto em alta impedância no pino `SCEX_DATA` |
| `PICOSTATION_SUBQ_CRC` | `OFF` (padrão), `ON` | Cálculo experimental de CRC-16 no SubQ (padrão mantém `0xbeef` do RC5) |

### Matriz completa (16 arquivos `.uf2`)
```sh
./tools/build_release_matrix.sh
```
Gera todas as 16 combinações (`PU18`/`PU8` x `SCEA`/`SCEE`/`SCEI`/`Generic` x padrão/`_OC`) em `releases/src-build/`.

---

## Créditos e projetos relacionados
- **Base deste fork:** [megavolt85/PicoStation](https://github.com/megavolt85/PicoStation)
- **Projeto original:** [paulocode/picostation](https://github.com/paulocode/picostation)
- **Agradecimentos:** Gemmi, megavolt85, Rama, Skitchin, Raijin, SpicyJpeg, Danhans42, NicholasNoble e comunidade PSX.dev.
- **Bibliotecas de terceiros:**
  - `cueparser` do [grumpycoders/pcsx-redux](https://github.com/grumpycoders/pcsx-redux) (Nicolas "Pixel" Noble)
  - Driver SD [carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico](https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
  - Primitivas atômicas [TSprech/RP2040_Pseudo_Atomic](https://github.com/TSprech/RP2040_Pseudo_Atomic)
- **Licença:** Apache License 2.0 (veja [LICENSE](LICENSE))
