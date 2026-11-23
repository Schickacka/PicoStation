# PicoStation — Schickacka Release 1

Fork of [megavolt85/PicoStation](https://github.com/megavolt85/PicoStation) (originally created by [paulocode/picostation](https://github.com/paulocode/picostation)), an open-source Raspberry Pi Pico (`RP2040` / `RP2350`) Optical Drive Emulator (ODE) for the original Sony PlayStation (`PU-8` and `PU-18`).

> **Status (Release 1 / WIP):** All firmware changes compile with **0 warnings** across the full 16-variant matrix (`PU-18`/`PU-8` × `SCEA`/`SCEE`/`SCEI`/`Generic` × Standard/OC) and have been validated against **40 DuckStation CD-ROM/BIOS traces across 17 NTSC & PAL titles** plus **233 Redump `.sbi` files**. **Final physical console bench validation on `PU-18` and `PU-8` hardware is currently in progress** — hardware testing reports and feedback are welcome!

---

## Key Improvements in Schickacka R1

### 1. Sled Tracking, `LMTSW` & Anti-Swap Boot Fix
- **Real-time `moveSled()` tracking (`src/drive_mechanics.cpp`):** Updates `m_sector`, `cur_zone`, and `Pin::LMTSW` (`sector > 3000`) live per track tick (`29 µs` normal / `15 µs` OC) under IRQ lock + `__dmb()`, stopping cleanly at disc boundaries (`0` / `c_sectorMax`) and publishing `skip_subq` once on stop.
  - Resolves the black-screen hang after the PlayStation logo on outer-edge games (such as *Resident Evil 1 DC / 2 / 3*, *Biohazard 3*, *Dino Crisis 1 & 2*, *Spyro 1*, *Crash Bandicoot PAL*, and *MediEvil PAL*) when `CDROM: Init` or `ReadTOC + GetID` drives `SLED_REVERSE` back to `00:00:00` (`LMTSW == 0`).
- **Single-use brake window (`src/cmd.cpp`):** Fixes `m_sledBreak` to a single-use `< 40 ms` window from `m_lastSledStop`, preventing genuine seeks from being dropped after `Init` or consecutive reverse moves.
- **Sled gating (`src/i2s.cpp`, `src/modchip.cpp`):** Gates SD prefetch and `SCEX` injection with `g_driveMechanics.isSledStopped()`.
- **Track jump & zone fixes:** Restores `10TRK` / `MTRK` handlers and fixes off-by-one zone clamping (`zoneForSector()`).

### 2. Native `.sbi` (LibCrypt PAL) Support & SubQ Alignment
- **Automatic `.sbi` loader (`src/disc_image.cpp`):** Automatically loads `<game>.sbi` alongside `<game>.cue` into a static RAM table (`m_sbiSectors[64]`, zero heap allocation, `O(log N)` binary search in SRAM).
- **Hardware-accurate `CRCF = 0` emulation (`src/subq.cpp`):** Per the Sony `CXD2510Q`/`CXD2545Q` datasheet, protected `.sbi` sectors drive `SQSO = LOW` (`CRCF = 0`) prior to pulsing `SCOR` (`4 µs`), so the `MC68HC05` maintains sector framing while ignoring the corrupted SubQ and returning the previous sector on `GetlocP`.
- **Absolute/Relative MSF alignment (`src/disc_image.cpp`):** Fixes `sector_abs = sector - c_leadIn` (eliminating the `+1` off-by-one discrepancy where `A - R = 151` instead of `150` / `00:02:00`).
- **Multi-track Lead-Out fix (`src/disc_image.cpp`):** Computes `leadOut64 = indices[0] + size`, eliminating the `+150`-sector (`+2 s`) TOC drift on multi-track discs (validated up to 57-track *Tomb Raider*).
- **SD card usage for PAL LibCrypt games:** Simply place the Redump `.sbi` file next to the `.cue` with the exact same basename (e.g., `Final Fantasy VIII (Europe, Australia) (Disc 1).cue` + `.sbi`).

### 3. Unified `PU-18` / `PU-8` Profiles, Overclock (`_OC`) & Stealth `SCEX`
- **Unified Mechacon Clock (`GPIO 3`) in source (`src/picostation.cpp`):** Ports the closed-source RC5 `GPIO 3` PWM clock profiles directly into `src/` via CMake options:
  | Profile | `GPIO 3` (`IC304` OSC1) | `SCEX` Bit-Bang (`8N2`) | `c_MaxTrackMoveTime` |
  |---|---|---|---|
  | `PU18` (default) | `4.2375 MHz` (`wrap=63, level=32`) | `250 bps` (`4000 µs/bit`) | `29 µs` |
  | `PU18` + `OC=ON` | `7.5333 MHz` (`wrap=35, level=18`) | `444 bps` (`2252 µs/bit`) | `15 µs` |
  | `PU8` | `3.9882 MHz` (`wrap=67, level=34`) | `250 bps` (`4000 µs/bit`) | `29 µs` |
  | `PU8` + `OC=ON` | `6.4571 MHz` (`wrap=41, level=21`) | `404 bps` (`2475 µs/bit`) | `15 µs` |
- **Non-blocking `SCEX` bit-bang (`src/modchip.cpp`):** Exact `250 bps 8N2` timing (scaled automatically in `_OC`), selectable single-region (`SCEA`, `SCEE`, `SCEI`) or universal (`Generic`), and optional open-drain mode (`PICOSTATION_SCEX_OPEN_DRAIN=ON`) for safer operation with `IC706` on `PU-18`.

> **Hardware Safety Warning (`GPIO 3`):** Never connect Pico `GPIO 3` (`MECHACON_CLK`) to `IC304` pin 14 (`OSC1`) while `IC701.68` (`PU-18`) or crystal `X302` (`PU-8`) is still connected. Leave `GPIO 3` disconnected if using the motherboard's stock clock, or isolate `IC701.68`/`X302` before wiring `GPIO 3`.

### 4. Menu & Hardware Documentation
- Updated embedded menu (`binary/picostation-menu.bin`) credits to **PicoStation Menu - Release 1** / **Schickacka** while preserving all original contributors, with bit-exact Mode 2 Form 1 EDC/ECC recomputed.
- Complete PlayStation 1 (`PU-8` / `PU-18`) electrical netlists and architecture reverse-engineering notes in [`docs/hardware/`](docs/hardware/).

---

## Supported Consoles
- **PU-8** (`SCPH-100x` / early `3000`/`3500`/`5000`)
- **PU-18** (`SCPH-550x` / `5552`)

---

## Building from Source

### Prerequisites
- `cmake` ≥ 3.24, `ninja` (or `make`), `arm-none-eabi-gcc` (13.x or 14.x)
- [Raspberry Pi Pico SDK 2.1.1](https://github.com/raspberrypi/pico-sdk) (`PICO_SDK_PATH`)
- Initialize git submodules:
  ```sh
  git submodule update --init --recursive
  ```

### Single Build (default: `PU-18`, non-OC, `Generic` region)
```sh
cmake -S . -B build -G Ninja \
  -DPICO_SDK_PATH=/path/to/pico-sdk \
  -DPICO_BOARD=pico \
  -DPICOSTATION_VARIANT=picostation_pico1 \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICOSTATION_MAINBOARD=PU18 \
  -DPICOSTATION_OVERCLOCK=OFF \
  -DPICOSTATION_SCEX_REGION=3
ninja -C build
```

### CMake Options
| Option | Values | Description |
|---|---|---|
| `PICOSTATION_VARIANT` | `picostation_pico1`, `picostation_pico2`, `picostation_plus_pico1`, `picostation_plus_pico2` | Pico board & pinout |
| `PICOSTATION_MAINBOARD` | `PU18` (default), `PU8` | Target PS1 motherboard clock/timing profile |
| `PICOSTATION_OVERCLOCK` | `OFF` (default), `ON` | Enable Mechacon overclock (`_OC`) |
| `PICOSTATION_SCEX_REGION` | `0`=SCEA, `1`=SCEE, `2`=SCEI, `3`=Generic (default) | SCEX license string region |
| `PICOSTATION_SCEX_OPEN_DRAIN` | `OFF` (default), `ON` | Open-drain/Hi-Z high level on `SCEX_DATA` |
| `PICOSTATION_SUBQ_CRC` | `OFF` (default), `ON` | Experimental CRC-16 SubQ output (default keeps RC5 `0xbeef`) |

### Full 16-Variant Release Matrix
```sh
./tools/build_release_matrix.sh
```
Generates all 16 `PU18`/`PU8` × `SCEA`/`SCEE`/`SCEI`/`Generic` × Standard/OC `.uf2` images under `releases/src-build/`.

---

## Credits & Upstream Links
- **Upstream fork base:** [megavolt85/PicoStation](https://github.com/megavolt85/PicoStation)
- **Original project:** [paulocode/picostation](https://github.com/paulocode/picostation)
- **Huge thanks:** Gemmi, megavolt85, Rama, Skitchin, Raijin, SpicyJpeg, Danhans42, NicholasNoble, and the PSX.dev community.
- **Third-party libraries:**
  - `cueparser` from [grumpycoders/pcsx-redux](https://github.com/grumpycoders/pcsx-redux) by Nicolas "Pixel" Noble
  - SD card driver from [carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico](https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
  - Atomic helpers from [TSprech/RP2040_Pseudo_Atomic](https://github.com/TSprech/RP2040_Pseudo_Atomic)
- **License:** Apache License 2.0 (see [LICENSE](LICENSE))
