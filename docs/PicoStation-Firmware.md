# PicoStation — Firmware RP2040

[← Voltar ao Índice da Wiki](README.md) | [README principal](../README.md)

O firmware **PicoStation** transforma um **Raspberry Pi Pico (`RP2040`)** em um emulador de drive óptico (ODE) em tempo real para PlayStation 1 (`PU-8` e `PU-18`), emulando tanto discos de jogos (`.cue`/`.bin`) quanto CDs de áudio virtuais (`.mp3`/`.wav` no **PicoPlayer**).

---

## Arquitetura interna (RP2040 Dual-Core + PIO)

O RP2040 opera a `271.2 MHz` dividindo o trabalho de forma determinística entre os dois núcleos:

- **Core 0 (Tempo real de controle e subcanal):**
  - Recebe e processa os comandos seriais do **Mechacon (`HC05`)** via PIO (`XLAT`, `CMD_CK`, `CMD_DATA`) e responde pelos sinais `SENS` (`GFS`, `FOK`, `COUT`, `XBUSY`, `LMTSW`).
  - Simula a mecânica de deslocamento do sled entre zonas de velocidade angular do CD.
  - Gera o subcanal **SubQ** em tempo real (`SCOR`, `SQCK`, `SQSO`), incluindo Lead-In TOC (`A0`/`A1`/`A2` priorizados para jogos multi-trilha), posição MSF e patches **LibCrypt (`.sbi`)**.
  - Monitora os botões físicos de **Reset** (toque curto = reset do jogo atual; segurar >1 s = volta ao PicoMenu) e **Tampa (`DOOR`)** (troca de disco em jogos multi-CD).
- **Core 1 (Leitura SD, decodificação e streaming I2S):**
  - Mantém o **cache de 32 setores (`CACHED_SECS = 32`)** para jogos PS1, com busca rápida O(1) (`FatFS CLMT` tanto em **FAT32** quanto em arquivos contíguos **exFAT `NoFatChain`**).
  - Gerencia abertura *lazy* de faixas em jogos multi-trilha (ex.: *Tomb Raider* com 57 faixas monta em milissegundos).
  - Injeta a assinatura de região **ModChip (`SCEX`)** por máquina de estado dedicada no **PIO1** (zero jitter mesmo sob carga de SD).
  - Alimenta o barramento **I2S** do PlayStation 1 via **DMA** em sincronia com `LRCK` e `BCLK`.
  - Quando o **PicoPlayer** está ativo, reutiliza os setores ociosos do cache (`~141 KB`) como arena determinística (zero fragmentação de heap) e decodifica **MP3 (`libhelix-mp3` em SRAM)** ou **WAV PCM** em tempo real.

---

## Como escolher entre as 16 variantes `.uf2`

Cada release traz 16 arquivos `.uf2` pré-compilados, organizados em duas pastas (`PU8/` e `PU18/`):

| Parâmetro | Opções | Explicação |
|---|---|---|
| **Placa-mãe** | `PU8` / `PU18` | Escolha conforme a revisão da placa do seu PS1 (`PU-8` ou `PU-18`). |
| **Overclock do Mechacon** | *(padrão)* / `_OC` | Use `_OC` **somente** se você tiver feito a modificação de clock do Mechacon ligada ao `GPIO 3`. Se não instalou esse fio, use a versão **sem `_OC`**. |
| **Região (`SCEX`)** | `SCEA`, `SCEE`, `SCEI`, `Generic` | Deve corresponder à **região da BIOS do seu console**:<br>• `SCEA` = NTSC-U (América)<br>• `SCEE` = PAL (Europa)<br>• `SCEI` = NTSC-J (Japão)<br>• `Generic` = envia todas as regiões (ideal se não souber qual BIOS está instalada). |

> **Dica:** para consoles com BIOS Japonesa (`SCEI`), escolher a variante regional (`_SCEI`) acelera a autenticação do disco em relação à `Generic`.

---

## Como instalar ou atualizar o firmware

### Método 1 — Via USB (`BOOTSEL`)
1. Desligue o PlayStation 1 da tomada.
2. Segure o botão **`BOOTSEL`** do Raspberry Pi Pico e conecte o cabo USB no computador.
3. Solte o botão: aparecerá uma unidade removível chamada **`RPI-RP2`**.
4. Copie o arquivo `.uf2` escolhido para dentro de **`RPI-RP2`**.
5. O Pico reinicia automaticamente e o firmware já está gravado!

### Método 2 — Entrar em Bootloader pelo próprio PicoMenu
1. Na tela raiz do **PicoMenu**, aperte **`SELECT`** para abrir os **Créditos**.
2. Aperte **`△` (Triângulo)** para colocar o RP2040 em modo **Bootloader (`RPI-RP2`)** sem precisar abrir o console para apertar o botão físico `BOOTSEL` (basta ter acesso ao cabo USB).

---

## Recomendações elétricas, soldagem e microSD

- **Cartão microSD:** prefira cartões originais de marca confiável (**SanDisk Ultra/Extreme**, **Samsung EVO/PRO**, **Kingston Canvas**), formatados em **FAT32** ou **exFAT** (cluster `32 KB` ou `64 KB`).
- **Comprimento dos fios:** mantenha todos os fios os mais curtos possível — especialmente `BCLK`, `LRCK`, `DATA`, sinais SPI do leitor microSD e `GPIO 3` (`MECHACON_CLK`).
- **Bitola:** `28–30 AWG` para linhas de sinal e `24–26 AWG` para alimentação (`3.3V` e `GND`), com aterramento sólido próximo à placa.
- **Módulo microSD:** use leitor passivo de **3.3V direto** (sem regulador 5V→3.3V nem conversor de nível em série).
- **Cuidado crítico com `_OC` (`GPIO 3`):** jamais conecte `GPIO 3` ao pino `14` (`OSC1`) do `IC304` sem antes isolar o sinal de clock original da placa (`IC701.68` na `PU-18` ou cristal `X302` na `PU-8`).

---

## Como compilar do código-fonte

### Requisitos
- `cmake >= 3.24` e `ninja`
- Toolchain `arm-none-eabi-gcc`
- [Raspberry Pi Pico SDK 2.1.1](https://github.com/raspberrypi/pico-sdk)

### Compilação da matriz completa (16 `.uf2`)
```sh
git submodule update --init --recursive
./tools/build_release_matrix.sh Release_2.0
```
Os 16 arquivos `.uf2` serão gerados em `releases/Release_2.0/PU8/` e `releases/Release_2.0/PU18/`.

---

[← Voltar ao Índice da Wiki](README.md) | [Próximo: PicoMenu →](PicoMenu.md)
