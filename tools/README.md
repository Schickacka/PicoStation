# Ferramentas de desenvolvimento

Este diretório é usado apenas para ferramentas locais. O firmware continua
podendo ser compilado normalmente com os submodules em `third_party/`.

## Conteúdo

- `pico-sdk/` - Pico SDK 2.1.1 (cópia local, usada se `PICO_SDK_PATH` não for
  definido).
- `RP2040_Pseudo_Atomic/` - dependência de atomics (fallback quando o submodule
  em `third_party/` não está inicializado).
- `venv/` - ambiente Python com `py7zr` para extrair os releases oficiais.
  **Não é portável entre PCs** (contém caminhos absolutos). Não é necessário
  para compilar nem para gravar UF2.
- `uf2_extract.py` - converte um UF2 em imagem plana de flash para análise.
- `uf2_patch.py` - aplica patches de bytes no payload de um UF2.
- `build_src.sh` - compila o source para Pico 1 (RP2040) usando as ferramentas
  locais.

## Compilar o source

```sh
tools/build_src.sh
```

O UF2 fica em `tools/build-src/picostation_pico1.uf2`.

O CMakeLists aceita `PICO_SDK_PATH` externo e usa
`tools/RP2040_Pseudo_Atomic` quando o submodule não existe, então o build
normal por submodules continua funcionando.

## Extrair e patchar UF2

```sh
python3 tools/uf2_extract.py firmware.uf2 -o firmware.bin
python3 tools/uf2_patch.py firmware.uf2 firmware_patched.uf2 0x4D6=10 0x34572=00bf
```

`uf2_patch.py` não altera o arquivo de entrada.

## Mover o projeto para outro PC

Os diretórios `build-src*` contêm caminhos absolutos. Apague-os e rode
`tools/build_src.sh` de novo. Se precisar do `venv`:

```sh
rm -rf tools/venv
python3 -m venv tools/venv
tools/venv/bin/pip install py7zr
```

Os UF2s e patches já gerados não dependem de build local; valide com
`sha256sum -c releases/SHA256SUMS`.
