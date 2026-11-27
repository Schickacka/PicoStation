# PicoStation

Emulador de drive óptico (ODE) 100% open-source para **PlayStation 1 (`PU-8` e `PU-18`)** baseado no **Raspberry Pi Pico (`RP2040`)**, acompanhado do **PicoMenu** (menu open-source embarcado), **PicoPlayer** (player de áudio MP3/WAV com visualizador SoundScope) e **PicoManager** (gerenciador desktop de cartão SD para Windows, Linux e macOS).

> **Histórico de versões, correções e downloads:** consulte sempre a aba [**Releases**](https://github.com/Schickacka/PicoStation/releases).

---

## O que é e de onde veio

O **PicoStation** substitui o leitor de CD original do PlayStation 1 por um **Raspberry Pi Pico**, permitindo carregar jogos (`.cue`/`.bin`) e áudio diretamente de um cartão microSD com velocidade real de drive óptico, mantendo o console fiel ao hardware original.

Este repositório é um fork open-source construído sobre o trabalho de referência da comunidade PS1:
- **Origem:** [paulocode/picostation](https://github.com/paulocode/picostation) (criador original do PicoStation)
- **Base de firmware e menu:** [megavolt85/PicoStation](https://github.com/megavolt85/PicoStation) e [megavolt85/picostation-menu](https://github.com/megavolt85/picostation-menu)
- **Ecossistema open-source:** `ps1-bare-metal` (spicyjpeg), `FatFS`, `cueparser` e `libhelix-mp3`

O objetivo deste fork é manter **100% do código-fonte aberto** (firmware RP2040, menu PS1 em MIPS, player de áudio e utilitário desktop), com foco em estabilidade em hardware real, compatibilidade com jogos multi-trilha CDDA, BIOS de todas as regiões (`SCEA`/`SCEE`/`SCEI`), acessórios modernos (`OpenWirelessMC`, `MemCard PRO`, `SD2PSX`, `BlueRetro`) e suporte a `PU-8` e `PU-18`.

---

## Como funciona

No PlayStation 1, o microcontrolador do drive (**Mechacon / `HC05`**) se comunica com o processador de sinal do CD (**DSP**) através de sinais seriais de comando, subcanal **SubQ** (`SCOR`, `SQCK`, `SQSO`), sensores mecânicos (`LMTSW`, sled/foco) e barramento de dados de áudio/setores **I2S** (`DATA`, `BCLK`, `LRCK`).

O **RP2040** usa seus dois núcleos e as máquinas de estado **PIO** para emular esse barramento em tempo real:
- **Core 0:** responde aos comandos do Mechacon, simula a mecânica do sled, gera o **SubQ** (TOC, posição MSF, LibCrypt) e injeta o sinal de região (**ModChip `SCEX`**).
- **Core 1:** lê setores `.bin`/`.cue` (ou decodifica MP3/WAV no PicoPlayer) diretamente do cartão microSD via **SPI/FatFS**, aplica o *scrambling* CD-ROM e alimenta o barramento **I2S via DMA**.
- **Menu embarcado (`PicoMenu`):** o próprio RP2040 apresenta inicialmente um disco virtual contendo o menu open-source; ao escolher um jogo ou abrir o PicoPlayer, o PicoStation desmonta o menu e monta a imagem selecionada no SD.

---

## Recomendações importantes da comunidade (Hardware & SD)

A estabilidade de qualquer ODE no PS1 depende muito da qualidade da instalação física e do cartão microSD:

### Cartão microSD
- **Use cartões originais de boa procedência** (ex.: SanDisk Ultra/Extreme, Samsung EVO/PRO, Kingston Canvas Select/Go!). Cartões genéricos ou falsificados costumam ter picos altos de latência aleatória, causando travamentos em FMVs, áudio picotado ou falha ao montar jogos multi-trilha.
- **Formato:** **FAT32** ou **exFAT** (cluster de `32 KB` ou `64 KB` recomendado).
- **Evite fragmentação excessiva:** ao copiar muitos jogos de uma vez (especialmente jogos com dezenas de `.bin` CDDA), copiar para um cartão recém-formatado melhora o tempo de busca.

### Soldagem, fiação e elétrica (`PU-8` e `PU-18`)
- **Mantenha os fios os mais curtos possível** — especialmente os sinais rápidos de clock/I2S (`BCLK`, `LRCK`, `DATA`), SPI do leitor microSD e `MECHACON_CLK` (`GPIO 3`). Fios longos funcionam como antenas, introduzindo ruído, erros de leitura ou instabilidade de boot.
- **Use fios finos adequados (`28–30 AWG` para sinais e `24–26 AWG` para `3.3V`/`GND`)** e garanta um **bom ponto de `GND`** próximo à placa do PS1 e ao módulo microSD.
- **Leitor de cartão microSD:** prefira módulos passivos de **3.3V direto** (sem regulador 5V→3.3V nem conversor de nível em série, que derrubam tensão e degradam o sinal SPI).
- **Atenção elétrica obrigatória (`GPIO 3` / Overclock `_OC`):** nunca ligue o pino `GPIO 3` (`MECHACON_CLK`) do RP2040 ao pino `14` (`OSC1`) do `IC304` enquanto o sinal original da placa (`IC701.68` na `PU-18` ou cristal `X302` na `PU-8`) ainda estiver conectado.
- **Limpeza pós-solda:** remova resíduos de fluxo com álcool isopropílico para evitar capacitância parasita nos pinos do DSP/Mechacon.

---

## Como escolher o arquivo `.uf2` correto

Cada release disponibiliza **16 variantes** para Raspberry Pi Pico (`RP2040`):

| Parâmetro | Opções | Quando usar |
|---|---|---|
| **Placa** | `PU8` / `PU18` | Conforme a revisão da placa-mãe do seu PlayStation 1 |
| **Clock** | Padrão / `_OC` | Use `_OC` **somente** se a ligação de clock do Mechacon (`GPIO 3`) estiver instalada |
| **Região (`SCEX`)** | `SCEA` (EUA), `SCEE` (EUR), `SCEI` (JPN), `Generic` | Escolha a região da **BIOS do seu console** (ou `Generic` se não tiver certeza) |

### Estrutura recomendada no cartão microSD
```text
SD:/
├── config.ini              # Configurações do PicoMenu (gerado pelo menu ou PicoManager)
├── background.raw          # Papel de parede 320x240 8bpp opcional
├── PicoPlayer/             # Músicas .mp3 / .wav + capas .cov + index.pp (Release 2.0+)
└── Jogos/
    └── Meu Jogo/
        ├── Meu Jogo.cue
        ├── Meu Jogo.bin
        ├── Meu Jogo.cov    # Capa 128x128 8bpp opcional
        ├── Meu Jogo.cht    # Cheats GameShark opcionais
        └── Meu Jogo.lsd    # Dados LibCrypt PAL opcionais (.sbi/.lsd)
```

---

## Como compilar

### Firmware PicoStation (16 variantes `.uf2`)
Requer `cmake >= 3.24`, `ninja`, `arm-none-eabi-gcc` e [Raspberry Pi Pico SDK 2.1.1](https://github.com/raspberrypi/pico-sdk):
```sh
git submodule update --init --recursive
./tools/build_release_matrix.sh
```

### PicoMenu (`picomenu/`)
Requer `mipsel-none-elf-gcc`, `cmake`, `ninja` e `mkpsxiso`:
```sh
cd picomenu
cmake -B build -G Ninja
cmake --build build
cp build/picostation-menu.bin ./picostation-menu.bin
```

---

## Créditos e licença

- **Fork e manutenção:** **Schickacka**
- **Agradecimentos:** Gemmi, megavolt85, paulocode, Rama, Skitchin, Raijin, spicyjpeg, Danhans42, NicolasNoble, Maniac Vera e toda a comunidade PSX.dev.
- **Licença:** Apache License 2.0 ([LICENSE](LICENSE)).
