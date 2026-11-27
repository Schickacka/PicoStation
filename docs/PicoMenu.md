# PicoMenu — Menu Open-Source para PlayStation 1

[← Voltar ao Índice da Wiki](README.md) | [Firmware](PicoStation-Firmware.md) | [PicoPlayer →](PicoPlayer.md)

O **PicoMenu** é o menu open-source embarcado dentro do firmware do PicoStation (`320×240p` double-buffered, escrito em C/MIPS sobre `ps1-bare-metal`). Ele carrega instantaneamente ao ligar o console, permitindo escolher jogos, gerenciar cheats, ajustar opções por jogo e abrir o **PicoPlayer**.

---

## Controles na tela principal

| Botão | Ação |
|---|---|
| **↑ / ↓** | Move a seleção na lista (segurar ativa auto-repetição) |
| **← / →** ou **L1 / R1** | Avança ou volta uma página inteira |
| **`✕` (ou botão `Abrir`)** | Abre uma pasta ou abre o menu de **Opções** do arquivo `.cue` selecionado |
| **`○` (ou botão `Voltar`)** | Volta para a pasta anterior |
| **`□` (Quadrado)** | Abre o **PicoPlayer** *(disponível na pasta raiz)* |
| **`SELECT`** | Abre/fecha a tela de **Créditos** e versão *(disponível na pasta raiz)* |
| **`△` (nos Créditos)** | Coloca o RP2040 em modo **Bootloader (`RPI-RP2`)** |

> Os botões de **Abrir** (`actopen`) e **Voltar** (`actback`) podem ser invertidos ou remapeados via `config.ini` ou **PicoManager**.

---

## Menu de Opções do Jogo

Ao selecionar um arquivo `.cue`, o menu de opções oferece:

1. **Inicialização rápida (`Fast Boot`):** monta o jogo, lê `SYSTEM.CNF`, envia o **GameID** ao Memory Card virtual e executa diretamente o binário do jogo via kernel BIOS sem reiniciar o console (mais rápido).
2. **Inicialização normal (`Regular Boot` / Full Boot):** monta o jogo, envia o **GameID** e reinicia o PS1 pela BIOS completa (mostrando os logos Sony e PlayStation). Recomendado para jogos com proteção especial ou telas de boot específicas.
3. **Códigos de trapaça (`Cheats`):** carrega o arquivo `.cht` do jogo e abre a lista interativa de códigos GameShark.
4. **Overclock:** liga/desliga o overclock do Mechacon para o boot atual *(os 8 jogos conhecidos com incompatibilidade de overclock são desativados automaticamente)*.
5. **Correção EDC (`EDC correction`):** ativa/desativa regeneração de EDC/ECC em tempo real.
6. **Correção Bootsector (`Bootsector fix`):** aplica patch do setor de licença/logo (LBA `0..11`) para permitir boot de jogos fora de região ou traduzidos (inclusive em consoles com BIOS Japonesa `SCEI`).

---

## Menu de Cheats (`.cht` / GameShark)

Coloque um arquivo `.cht` com o mesmo nome do `.cue` dentro da pasta do jogo (ex.: `Metal Slug X.cht`).

- **Todas as chavinhas já abrem ativadas por padrão** (comportamento fiel ao RC5).
- **Controles no menu de Cheats:**
  - **↑ / ↓:** navega entre os códigos
  - **`START`:** liga / desliga o cheat selecionado
  - **`L2`:** liga **todos** os cheats de uma vez
  - **`R2`:** desliga **todos** os cheats de uma vez
  - **`✕` (`Jogar`):** inicia o jogo com todos os cheats ativados instalados no hook de VBlank GameShark
  - **`○` (`Voltar`):** fecha o menu de cheats

---

## Capas (`.cov`), Papel de Parede (`background.raw`) e `config.ini`

### Capas de jogos (`.cov`)
- Formato: **128×128 pixels, 8bpp indexado** (`512 B` paleta BGR555 + `16.384 B` pixels = `16.896 B`).
- Pode ser colocada tanto com o nome da pasta (`Meu Jogo.cov` ao lado da pasta `Meu Jogo/`) quanto com o nome do `.cue` (`Meu Jogo.cov` dentro da pasta).
- O **PicoManager** converte qualquer `.png`/`.jpg`/`.webp` para `.cov` automaticamente.

### Papel de parede (`SD:/background.raw`)
- Formato: **320×240 pixels, 8bpp indexado** (`512 B` paleta + `76.800 B` pixels = `77.312 B`).
- Para ativar, defina `wallpaper = 1` em `SD:/config.ini` (ou use a aba **Configurações** do **PicoManager**).

### Arquivo `SD:/config.ini`
Exemplo completo das chaves aceitas pelo PicoMenu:
```ini
background = 080828
frame      = 3141C5
filelist   = 222662
cursor     = 4C5EF0
text       = FFFFFF
style      = 0        ; 0 = lista + capa à direita | 1 = lista em tela cheia
wallpaper  = 0        ; 0 = fundo PU-20 anéis azuis | 1 = usa SD:/background.raw
lang       = 8        ; 0=EN, 1=DE, 2=ES, 3=FR, 4=IT, 8=PT-BR
actopen    = 14       ; bit do botão Abrir (14 = X, 13 = O)
actback    = 13       ; bit do botão Voltar (13 = O, 14 = X)
```

---

## Compatibilidade com acessórios modernos

- **OpenWirelessMC (OWMC), MemCard PRO e SD2PSX:** o PicoMenu envia automaticamente o **GameID** (`0x81 0x21`) nas Portas 1 e 2 antes de iniciar o jogo, trocando o canal de Memory Card virtual automaticamente.
- **BlueRetro / controles sem fio:** o barramento `SIO0` só é consultado após o término completo do DMA da GPU (`waitForDMADone()` + `waitForGP0Ready()`), eliminando qualquer ruído no barramento ou vibração fantasma nos menus.

---

## Como compilar o PicoMenu

Requer `mipsel-none-elf-gcc`, `cmake`, `ninja` e `mkpsxiso`:
```sh
cd picomenu
cmake -B build -G Ninja
cmake --build build
cp build/picostation-menu.bin ./picostation-menu.bin
```

---

[← Firmware](PicoStation-Firmware.md) | [Índice da Wiki](README.md) | [Próximo: PicoPlayer & SoundScope →](PicoPlayer.md)
