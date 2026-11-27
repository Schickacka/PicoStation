# PicoPlayer & SoundScope — Player de Áudio para PlayStation 1

[← Voltar ao Índice da Wiki](README.md) | [PicoMenu](PicoMenu.md) | [PicoManager →](PicoManager.md)

O **PicoPlayer** é um reprodutor de música nativo integrado ao PicoStation e ao PicoMenu, com interface inspirada no clássico CD Player do BIOS Sony **PU-20** em **`640×480i`** e o visualizador reativo **SoundScope** com **22 modos gráficos** em **`320×240p` double-buffered**.

---

## Como funciona e formatos suportados

Ao pressionar **`□` (Quadrado)** na tela raiz do **PicoMenu**, o RP2040 lê a pasta **`SD:/PicoPlayer/`** e monta um **CD de Áudio Red Book virtual**, decodificando as músicas em tempo real e enviando PCM 16-bit 44.1 kHz puro pelo barramento **I2S** diretamente para a SPU e DAC do PlayStation 1!

| Formato | Especificações suportadas |
|---|---|
| **MP3 (`.mp3`)** | MPEG-1 Layer III estéreo/mono, **CBR e VBR até 320 kbps** (incluindo **LAME VBR V0**), 44.1 kHz nativo (e resampling automático para 48 / 32 / 22.05 kHz). Decoder `libhelix-mp3` otimizado rodando em **SRAM** do RP2040 com buffer de 16 KB em setores alternados (áudio limpo, zero engasgos). |
| **WAV (`.wav`)** | PCM não comprimido **16-bit, 44.1 kHz** (estéreo ou mono — qualidade CD sem perdas). |
| **Metadados** | Tags **ID3v2.3 / ID3v2.4** (`TIT2`, `TPE1`, `TALB`) e **ID3v1**, além de taxa de bits e frequência exibidas na tela. |
| **Capas de Álbum (`.cov`)** | `128×128` 8bpp (`16.896 B`). Pode haver uma capa por música (`Musica.cov`) ou uma única capa compartilhada da pasta (`cover.cov`). |

---

## Aceleração com `index.pp` e deduplicação de capas

- Quando você usa o [**PicoManager**](PicoManager.md) para organizar `SD:/PicoPlayer/`, ele gera automaticamente o arquivo **`SD:/PicoPlayer/index.pp`** (índice TOC pré-calculado).
- Ao abrir o PicoPlayer no PS1, o firmware valida o hash da pasta em ~5 ms e carrega todas as faixas, durações e tags ID3 em menos de 20 ms (sem precisar abrir arquivo por arquivo no SD).
- Além disso, o `index.pp` identifica capas com conteúdo idêntico (SHA-256) e atribui o mesmo `artGroup`, fazendo o PS1 transferir cada capa única **apenas 1 vez** (por exemplo, um álbum de 13 faixas transfere a capa só 1 ou 2 vezes em vez de 13!).
- Se você copiar arquivos manualmente sem o PicoManager, o PicoPlayer continua funcionando normalmente fazendo a varredura automática direto no RP2040.

---

## Controles do PicoPlayer (`640×480i`)

| Botão | Ação |
|---|---|
| **D-Pad (↑ ↓ ← →)** | Navega entre a lista de músicas, a coluna de transporte (esquerda) e os botões de modo (base) |
| **`✕`** | Reproduz a faixa selecionada ou ativa o botão em foco |
| **`START`** | Play / Pause rápido |
| **`L1 / R1`** | Página anterior / próxima página na lista (ou faixa anterior / próxima fora da lista) |
| **`L2 / R2`** | Retrocede `-5 s` / Avança `+5 s` na música (segurar repete) |
| **`△` (Triângulo)** | Abre o teclado numérico rápido para digitar o número da faixa |
| **`SELECT`** | Entra no visualizador em tela cheia **SoundScope** |
| **`○` (ou botão `SAIR`)** | Sai do PicoPlayer e retorna ao **PicoMenu** |

### Botões do painel inferior
- **`CONTINUAR`:** reprodução sequencial normal
- **`ALEATÓRIO`:** toca as faixas em ordem aleatória (Shuffle)
- **`PROGRAMAR`:** permite montar uma fila personalizada de faixas
- **`REPETIR`:** alterna entre `OFF`, `REPETIR 1` (faixa atual) e `REPETIR TUDO`
- **`TEMPO`:** alterna o visor entre tempo decorrido (`TEMPO`), tempo restante da faixa (`REST. FAIXA`) e tempo restante do disco (`REST. DISCO`)
- **`SAIR`:** faz o fade-to-black no texto `SAIR`, desmonta o CD de áudio virtual e volta ao **PicoMenu**

---

## SoundScope — 22 Visualizadores em Tempo Real (`320×240p`)

Pressione **`SELECT`** durante a reprodução para abrir o **SoundScope**, que captura o áudio real em tempo real diretamente dos buffers de captura CD-Left/CD-Right da SPU do PlayStation 1 e calcula o espectro de 24 bandas (filtro de Goertzel) a **60 FPS** em **`320×240p` double-buffered** (cores vivas em vídeo composto, S-Video e RGB, com flip sincronizado no VBlank).

### Controles no SoundScope

| Botão | Ação |
|---|---|
| **`← / →`** ou **`△`** | Troca entre os **22 modos de visualização** |
| **`↑ / ↓`** | Aumenta / diminui o ganho e a velocidade de resposta (`1` a `8`) |
| **`○` (Bola)** | Alterna entre **5 paletas de cores** (*Fire/Spectrum*, *Ice/Cyan*, *Neon/Magenta*, *Emerald*, *Gold/Solar*) |
| **`□` (Quadrado)** | Alterna entre **4 níveis de rastros de fósforo** (*OFF*, *Curto*, *Médio*, *Longo*) |
| **`L1 / R1`** | Faixa anterior / Próxima faixa |
| **`L2 / R2`** | Retrocede `-5 s` / Avança `+5 s` |
| **`START`** | Play / Pause |
| **`SELECT`** | Volta para a tela do **PicoPlayer** |

---

[← PicoMenu](PicoMenu.md) | [Índice da Wiki](README.md) | [Próximo: PicoManager →](PicoManager.md)
