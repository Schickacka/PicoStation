# PicoManager — Gerenciador Desktop para Cartão SD

[← Voltar ao Índice da Wiki](README.md) | [PicoPlayer](PicoPlayer.md) | [README principal](../README.md)

O **PicoManager** é o aplicativo desktop open-source (Windows, Linux e macOS Apple Silicon) criado para preparar e organizar todo o cartão microSD do **PicoStation**, **PicoMenu** e **PicoPlayer** sem precisar lidar com conversões manuais ou formatos binários.

---

## Download e requisitos

Baixe o executável pronto para o seu sistema na aba [**Releases**](https://github.com/Schickacka/PicoStation/releases):

| Sistema | Executável | Observações |
|---|---|---|
| **Windows (64-bit)** | `PicoManager.exe` | Executável único (não requer instalação). |
| **Linux (x86_64)** | `PicoManager` | Dê permissão de execução: `chmod +x PicoManager && ./PicoManager`. |
| **macOS (Apple Silicon)** | `PicoManager-macOS-arm64` | Dê permissão e execute no Terminal (`chmod +x PicoManager-macOS-arm64`); se o Gatekeeper avisar, permita em *Ajustes do Sistema → Privacidade e Segurança*. |

> **Requisito para conversão de áudio:** tenha o [**FFmpeg**](https://ffmpeg.org/) instalado no `PATH` do sistema (no Linux: `sudo apt install ffmpeg`; no macOS: `brew install ffmpeg`; no Windows: `winget install Gyan.FFmpeg`). Sem o FFmpeg, todas as funções de jogos, capas `.cov`, `config.ini`, papel de parede e geração de `index.pp` continuam funcionando normalmente.

---

## Funcionalidades por aba

### 1. Aba **Jogos & Capas**
- Lista automaticamente todos os jogos e pastas do cartão SD selecionado.
- Mostra status de capa (`.cov`) e arquivo de cheats (`.cht`) de cada jogo.
- Permite associar qualquer imagem (`.png`, `.jpg`, `.jpeg`, `.webp`, `.bmp`) e convertê-la automaticamente para o formato exato do PS1 (**`128×128` 8bpp indexado, paleta BGR555, `16.896 B`**) com quantização de alta qualidade.

### 2. Aba **PicoPlayer (Áudio)**
- Gerencia a pasta **`SD:/PicoPlayer/`** com visualização de título, artista, álbum, formato, bitrate e presença de capa `.cov`.
- **Importação e conversão de áudio de alta fidelidade** (`.flac`, `.wav`, `.mp3`, `.ogg`, `.m4a`, `.aac`, `.opus`, `.wma`):
  - **`VBR V0 (~245 kbps, LAME -V 0 -q 0 — Recomendado)`:** transparência praticamente indistinguível de CD com arquivos compactos e otimizados para o decoder SRAM do PicoStation.
  - **`320 kbps CBR (LAME -b 320 -q 0)`:** bitrate constante máximo do padrão MP3.
  - **`WAV 16-bit 44.1 kHz (Sem Perdas / CD)`:** PCM puro idêntico a CD de áudio Red Book.
  - **Qualidade audiófila:** fontes que já estão em `44.1 kHz` passam **bit-exatas sem resampling**; fontes em `48/96 kHz` usam resampler **SoXR** (`precision=28`) com dither triangular (`triangular_hp`).
  - Extrai automaticamente a **capa embutida** (ID3 `APIC` / FLAC Picture) ou aceita imagem externa, gerando `.cov` `128×128` 8bpp automaticamente.
- **Botão `Gerar/Atualizar TOC (index.pp)`:**
  - Reconstrói `SD:/PicoPlayer/index.pp` em menos de 1 segundo com hash da pasta, durações exatas (Xing/Info/VBRI), metadados ID3 e **deduplicação automática de capas por SHA-256** (acelerando drasticamente a abertura de álbuns no PS1).

### 3. Aba **Configurações (`config.ini`) & Wallpaper**
- Edita visualmente as cores do tema do **PicoMenu** (`background`, `frame`, `filelist`, `cursor`, `text`).
- Seleciona o **idioma** do menu (incluindo **Português do Brasil — `lang = 8`**), layout da lista (`Estilo 0` com capa ou `Estilo 1` tela cheia) e remapeamento dos botões `Abrir`/`Voltar`.
- Converte qualquer imagem para **`SD:/background.raw`** (`320×240` 8bpp, `77.312 B`) e ativa `wallpaper = 1` com um clique.

---

## Como rodar pelo código-fonte (Python)

```sh
cd picomanager
python3 -m venv .venv
source .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -r requirements.txt
python app.py
```

---

[← PicoPlayer & SoundScope](PicoPlayer.md) | [Índice da Wiki](README.md) | [README principal](../README.md)
