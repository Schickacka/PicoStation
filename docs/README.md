# Documentação PicoStation (Wiki)

Bem-vindo à documentação completa do ecossistema **PicoStation**. Use o índice abaixo para navegar pelos guias detalhados de cada componente:

| Componente | O que você encontra neste guia |
|---|---|
| [**PicoStation Firmware**](PicoStation-Firmware.md) | Arquitetura RP2040 (Core 0/Core 1/PIO), matriz das 16 variantes `.uf2`, instalação via USB `BOOTSEL`, cuidados elétricos (`PU-8`/`PU-18` e `GPIO 3`), cartão SD e como compilar |
| [**PicoMenu**](PicoMenu.md) | Navegação e controles no PS1, `Fast Boot` vs `Full Boot`, opções (`Overclock`, `EDC`, `Bootsector`), **Cheats GameShark (`.cht`)**, capas `.cov`, `background.raw`, `config.ini`, idiomas e acessórios (`OWMC`/`MemCard PRO`/`BlueRetro`) |
| [**PicoPlayer & SoundScope**](PicoPlayer.md) | Player de áudio nativo estilo PU-20 (`MP3`/`WAV`), estrutura `SD:/PicoPlayer/`, índice acelerado `index.pp`, capas de álbum, todos os controles do player e os **22 visualizadores** do SoundScope |
| [**PicoManager**](PicoManager.md) | Guia do aplicativo desktop (Windows, Linux e macOS): gerenciamento de jogos/capas, importação e conversão de áudio de alta fidelidade (`VBR V0`, `320k CBR`, `WAV`), geração do `index.pp` e configuração do `config.ini` |

---

## Estrutura recomendada do cartão microSD

```text
SD:/
├── config.ini              # Configurações visuais, idioma, botões e wallpaper do PicoMenu
├── background.raw          # Papel de parede 320x240 8bpp opcional (gerado pelo PicoManager)
├── PicoPlayer/             # Biblioteca de áudio do PicoPlayer
│   ├── index.pp            # Índice TOC pré-calculado com deduplicação de capas (opcional, gerado pelo PicoManager)
│   ├── cover.cov           # Capa padrão da pasta (opcional, 128x128 8bpp)
│   ├── 01. Musica.mp3      # Músicas .mp3 (CBR/VBR até 320 kbps) ou .wav (16-bit 44.1 kHz)
│   └── 01. Musica.cov      # Capa individual da faixa (opcional, 128x128 8bpp)
└── Jogos/
    └── Meu Jogo/
        ├── Meu Jogo.cue    # Cue sheet do jogo
        ├── Meu Jogo.bin    # Faixas de dados/áudio (suporta multi-trilha CDDA)
        ├── Meu Jogo.cov    # Capa 128x128 8bpp opcional (ou Meu Jogo.cov na pasta pai)
        ├── Meu Jogo.cht    # Códigos GameShark opcionais
        └── Meu Jogo.sbi    # Dados de subcanal LibCrypt PAL opcionais (.sbi / .lsd)
```

---

[← Voltar ao README principal](../README.md)
