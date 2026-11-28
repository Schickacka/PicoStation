#!/usr/bin/env python3
"""
PicoManager Core — Open-source SD Card Manager & Audio Encoder for PicoStation 2.1.

Features:
- PS1 ISO9660 GameID extraction from .cue/.bin (SYSTEM.CNF -> SLUS-00748)
- Cover (.cov 128x128 8bpp), Cheat (.cht), and LibCrypt (.lsd) auto-installer
- Custom PNG/JPG -> .cov (16,896 B) and background.raw (77,312 B) converters
- config.ini reader/writer (100% compatible with PicoMenu RC5 & Release 2.0 PU-20)
- PicoPlayer Audio Encoder (LAME MP3 320 CBR / VBR V0 & WAV 44.1 kHz 16-bit Stereo)
  with free zero-key online ID3 auto-tagging (iTunes Search + MusicBrainz)
"""

from __future__ import annotations

import json
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unicodedata
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

try:
    from PIL import Image
except ImportError:
    Image = None  # type: ignore

AUDIO_EXTS = {".mp3", ".wav", ".flac", ".ogg", ".m4a", ".opus", ".aac", ".wma", ".aiff", ".aif"}
FFMPEG_DOWNLOAD_URL = "https://ffmpeg.org/download.html"


def get_app_dir() -> Path:
    """Returns the folder containing the PicoManager executable (or core.py when running from source)."""
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


def _subprocess_kwargs() -> Dict[str, object]:
    """Always decode ffmpeg/ffprobe output as UTF-8 and hide Windows console flashes."""
    kw: Dict[str, object] = {"encoding": "utf-8", "errors": "replace"}
    if sys.platform.startswith("win"):
        kw["creationflags"] = getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000)
    return kw


def _find_media_tool(tool_name: str) -> Optional[str]:
    """
    Locates ffmpeg / ffprobe in priority order:
      1) <PicoManager dir>/ffmpeg/<tool>[.exe]
      2) <PicoManager dir>/ffmpeg/bin/<tool>[.exe]
      3) <PicoManager dir>/<tool>[.exe]
      4) <cwd>/ffmpeg/[bin/]<tool>[.exe]
      5) System PATH via shutil.which()
    """
    exe_name = f"{tool_name}.exe" if sys.platform.startswith("win") else tool_name
    app_dir = get_app_dir()
    cwd = Path.cwd()
    candidates = [
        app_dir / "ffmpeg" / exe_name,
        app_dir / "ffmpeg" / "bin" / exe_name,
        app_dir / exe_name,
        cwd / "ffmpeg" / exe_name,
        cwd / "ffmpeg" / "bin" / exe_name,
    ]
    for cand in candidates:
        try:
            if cand.is_file():
                return str(cand)
        except OSError:
            continue
    return shutil.which(tool_name)


def find_ffmpeg() -> Optional[str]:
    return _find_media_tool("ffmpeg")


def find_ffprobe() -> Optional[str]:
    return _find_media_tool("ffprobe")

# Languages matching picomenu/src/rc5_lang.c
LANGUAGES: List[Tuple[int, str]] = [
    (8, "Português (BR)"),
    (0, "English"),
    (2, "Español"),
    (1, "Deutsch"),
    (3, "Français"),
    (5, "Italiano"),
]

# Controller bit indices matching picomenu/src/controller.h & rc5_config.c
# (Square is reserved for PicoPlayer; Start is excluded from Open/Back)
BUTTON_OPTIONS: List[Tuple[int, str]] = [
    (14, "× (Cruz)"),
    (13, "○ (Círculo)"),
    (12, "△ (Triângulo)"),
]

PU20_PRESET = {
    "background": "080828",
    "frame":      "3141C5",
    "filelist":   "222662",
    "cursor":     "4C5EF0",
    "text":       "FFFFFF",
}

RC5_CLASSIC_PRESET = {
    "background": "000020",
    "frame":      "FFFFFF",
    "filelist":   "000040",
    "cursor":     "800000",
    "text":       "FFFFFF",
}


@dataclass
class MenuConfigData:
    background: str = "080828"
    frame: str = "3141C5"
    filelist: str = "222662"
    cursor: str = "4C5EF0"
    text: str = "FFFFFF"
    style: int = 0          # 0 = com capas, 1 = tela cheia
    wallpaper: int = 0      # 0 = desativado (anéis PU-20), 1 = background.raw
    actopen: int = 14       # X
    actback: int = 13       # Circle
    lang: int = 8           # PT-BR


@dataclass
class ScanStats:
    cues_found: int = 0
    covers_copied: int = 0
    folder_covers_copied: int = 0
    cheats_copied: int = 0
    lsds_copied: int = 0
    missing_covers: int = 0
    errors: int = 0


# ---------------------------------------------------------------------------
# PS1 GameID extraction from .cue / .bin (ISO9660 Track 1 -> SYSTEM.CNF)
# ---------------------------------------------------------------------------

_CUE_FILE_RE = re.compile(r'FILE\s+"([^"]+)"|FILE\s+(\S+)', re.IGNORECASE)
_BOOT_ID_RE = re.compile(r'([A-Z]{4})[_-](\d{3})\.?(\d{2})', re.IGNORECASE)


def find_first_bin_from_cue(cue_path: Path) -> Optional[Path]:
    try:
        text = cue_path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return None
    for line in text.splitlines():
        m = _CUE_FILE_RE.search(line)
        if m:
            fname = m.group(1) or m.group(2)
            cand = cue_path.parent / fname
            if cand.is_file():
                return cand
            # Case-insensitive fallback on Linux
            low = fname.lower()
            for child in cue_path.parent.iterdir():
                if child.name.lower() == low and child.is_file():
                    return child
    return None


def _read_iso_sector(fp, lba: int, raw_2352: bool) -> bytes:
    if raw_2352:
        fp.seek(lba * 2352)
        sec = fp.read(2352)
        if len(sec) < 2352:
            return b""
        # Mode 2 Form 1 user data starts at +24; Mode 1 at +16
        mode = sec[15]
        return sec[24:24 + 2048] if mode == 2 else sec[16:16 + 2048]
    fp.seek(lba * 2048)
    return fp.read(2048)


def extract_game_id_from_cue(cue_path: Path) -> Optional[str]:
    bin_path = find_first_bin_from_cue(cue_path)
    if not bin_path:
        return None
    try:
        with bin_path.open("rb") as fp:
            hdr = fp.read(16)
            raw_2352 = (len(hdr) >= 12 and hdr[:12] == b"\x00" + b"\xff" * 10 + b"\x00")
            pvd = _read_iso_sector(fp, 16, raw_2352)
            if len(pvd) < 190 or pvd[1:6] != b"CD001":
                return None
            # Root directory record at offset 156
            root_lba = struct.unpack_from("<I", pvd, 156 + 2)[0]
            root_size = struct.unpack_from("<I", pvd, 156 + 10)[0]
            root_sectors = max(1, min(8, (root_size + 2047) // 2048))

            sys_lba = None
            sys_size = 0
            for s in range(root_sectors):
                sec = _read_iso_sector(fp, root_lba + s, raw_2352)
                off = 0
                while off < len(sec):
                    rec_len = sec[off]
                    if rec_len == 0:
                        break
                    name_len = sec[off + 32]
                    name = sec[off + 33:off + 33 + name_len].decode("ascii", errors="ignore").upper()
                    if name.startswith("SYSTEM.CNF"):
                        sys_lba = struct.unpack_from("<I", sec, off + 2)[0]
                        sys_size = struct.unpack_from("<I", sec, off + 10)[0]
                        break
                    off += rec_len
                if sys_lba is not None:
                    break

            if sys_lba is None:
                return None
            cnf_data = _read_iso_sector(fp, sys_lba, raw_2352)[: max(256, min(2048, sys_size))]
            cnf_text = cnf_data.decode("ascii", errors="ignore")
            m = _BOOT_ID_RE.search(cnf_text)
            if m:
                return f"{m.group(1).upper()}-{m.group(2)}{m.group(3)}"
    except OSError:
        return None
    return None


# ---------------------------------------------------------------------------
# PS1 Image Converters (.cov 128x128 8bpp & background.raw 320x240 8bpp)
# ---------------------------------------------------------------------------

def _rgb888_to_ps1_rgb555(r: int, g: int, b: int) -> int:
    return ((r >> 3) & 0x1F) | (((g >> 3) & 0x1F) << 5) | (((b >> 3) & 0x1F) << 10)


def convert_image_to_cov(src_image: Path, dst_cov: Path) -> None:
    if Image is None:
        raise RuntimeError("Pillow (PIL) não instalado. Instale com: pip install pillow")
    img = Image.open(src_image).convert("RGB").resize((128, 128), Image.Resampling.LANCZOS)
    pal_img = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    palette = list(pal_img.getpalette() or [])
    if len(palette) < 768:
        palette.extend([0] * (768 - len(palette)))
    clut = bytearray(512)
    for i in range(256):
        r, g, b = palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]
        struct.pack_into("<H", clut, i * 2, _rgb888_to_ps1_rgb555(r, g, b))
    pixels = pal_img.tobytes()
    assert len(pixels) == 16384
    dst_cov.parent.mkdir(parents=True, exist_ok=True)
    dst_cov.write_bytes(bytes(clut) + pixels)


def convert_image_to_wallpaper(src_image: Path, dst_raw: Path) -> None:
    """Converts any PNG/JPG to PicoStation 320x240 8bpp background.raw (77,312 bytes)."""
    if Image is None:
        raise RuntimeError("Pillow (PIL) não instalado. Instale com: pip install pillow")
    img = Image.open(src_image).convert("RGB").resize((320, 240), Image.Resampling.LANCZOS)
    pal_img = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    palette = list(pal_img.getpalette() or [])
    if len(palette) < 768:
        palette.extend([0] * (768 - len(palette)))
    clut = bytearray(512)
    for i in range(256):
        r, g, b = palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]
        struct.pack_into("<H", clut, i * 2, _rgb888_to_ps1_rgb555(r, g, b))
    raw = pal_img.tobytes()
    # Split into 256x240 left strip + 64x240 right strip (matching rc5WallpaperLoad)
    left = bytearray(256 * 240)
    right = bytearray(64 * 240)
    for y in range(240):
        row = raw[y * 320:(y + 1) * 320]
        left[y * 256:(y + 1) * 256] = row[:256]
        right[y * 64:(y + 1) * 64] = row[256:320]
    dst_raw.parent.mkdir(parents=True, exist_ok=True)
    dst_raw.write_bytes(bytes(clut) + bytes(left) + bytes(right))


# ---------------------------------------------------------------------------
# SD Card Cover / Cheat / LSD Scanner
# ---------------------------------------------------------------------------

def decode_cov_to_ppm(cov_path: Path) -> Optional[bytes]:
    """Decodes a PicoStation .cov (16,896 B or 16,912 B with 16B GameID trailer: 512B RGB555 CLUT + 128x128 8bpp) into raw PPM (P6) bytes for tk.PhotoImage."""
    if not cov_path.is_file():
        return None
    try:
        data = cov_path.read_bytes()
        if len(data) < 16896:
            return None
        pal: List[Tuple[int, int, int]] = []
        for i in range(256):
            w = struct.unpack_from("<H", data, i * 2)[0]
            r = (w & 0x1F) << 3
            g = ((w >> 5) & 0x1F) << 3
            b = ((w >> 10) & 0x1F) << 3
            pal.append((r, g, b))
        rgb = bytearray(128 * 128 * 3)
        px = data[512:16896]
        for idx, ci in enumerate(px):
            r, g, b = pal[ci]
            rgb[idx * 3]     = r
            rgb[idx * 3 + 1] = g
            rgb[idx * 3 + 2] = b
        return b"P6\n128 128\n255\n" + bytes(rgb)
    except Exception:
        return None


def decode_cov_to_pil(cov_path: Path):
    """Decodes a PicoStation .cov (16,896 B or 16,912 B with 16B GameID trailer) into a PIL RGB Image."""
    if Image is None or not cov_path.is_file():
        return None
    try:
        data = cov_path.read_bytes()
        if len(data) < 16896:
            return None
        pal: List[Tuple[int, int, int]] = []
        for i in range(256):
            w = struct.unpack_from("<H", data, i * 2)[0]
            r = (w & 0x1F) << 3
            g = ((w >> 5) & 0x1F) << 3
            b = ((w >> 10) & 0x1F) << 3
            pal.append((r, g, b))
        rgb = bytearray(128 * 128 * 3)
        px = data[512:16896]
        for idx, ci in enumerate(px):
            r, g, b = pal[ci]
            rgb[idx * 3]     = r
            rgb[idx * 3 + 1] = g
            rgb[idx * 3 + 2] = b
        return Image.frombytes("RGB", (128, 128), bytes(rgb))
    except Exception:
        return None


def list_storage_drives(include_fixed: bool = False) -> List[Tuple[str, str]]:
    """
    Returns list of (mount_path, display_label) for mounted drives.
    By default returns only removable drives (SD cards / USB).
    """
    drives: List[Tuple[str, str]] = []
    sys_name = platform.system()

    if sys_name == "Linux":
        try:
            res = subprocess.run(
                ["lsblk", "-J", "-o", "NAME,RM,HOTPLUG,MOUNTPOINT,LABEL,SIZE,TYPE"],
                capture_output=True, text=True, timeout=5, check=False,
            )
            if res.returncode == 0 and res.stdout:
                data = json.loads(res.stdout)
                def _walk(nodes, parent_rm=False):
                    for n in nodes or []:
                        rm = bool(n.get("rm")) or bool(n.get("hotplug")) or parent_rm
                        mp = n.get("mountpoint")
                        if mp and mp not in ("/", "/boot", "/boot/efi", "[SWAP]"):
                            if include_fixed or rm:
                                lbl = n.get("label") or Path(mp).name or n.get("name", "")
                                sz = n.get("size") or ""
                                tag = "SD/USB" if rm else "Fixo"
                                drives.append((mp, f"[{tag}] {lbl} ({sz}) — {mp}"))
                        _walk(n.get("children"), rm)
                _walk(data.get("blockdevices"))
        except Exception:
            pass
    elif sys_name == "Windows":
        try:
            import ctypes
            bitmask = ctypes.windll.kernel32.GetLogicalDrives()
            for i in range(26):
                if bitmask & (1 << i):
                    letter = f"{chr(65 + i)}:\\"
                    dtype = ctypes.windll.kernel32.GetDriveTypeW(letter)
                    # 2 = DRIVE_REMOVABLE, 3 = DRIVE_FIXED
                    if dtype == 2 or (include_fixed and dtype == 3):
                        tag = "SD/USB" if dtype == 2 else "Fixo"
                        drives.append((letter, f"[{tag}] {letter}"))
        except Exception:
            pass

    return drives


@dataclass
class GameEntry:
    cue_path: Path
    rel_path: str
    folder_name: str
    game_id: str
    has_cue_cov: bool
    has_folder_cov: bool
    has_cht: bool
    has_lsd: bool


def list_sd_games(sd_root: Path) -> List[GameEntry]:
    """Lists all .cue games on the SD card (excluding UPDATE and PicoPlayer)."""
    games: List[GameEntry] = []
    skip_dirs = {"update", "picoplayer", "system volume information", "$recycle.bin"}
    for dirpath, dirnames, filenames in os.walk(sd_root):
        rel = Path(dirpath).relative_to(sd_root)
        if str(rel) == ".":
            dirnames[:] = sorted(d for d in dirnames if d.lower() not in skip_dirs and not d.startswith("."))
        else:
            dirnames[:] = sorted(d for d in dirnames if not d.startswith("."))
        for fn in sorted(filenames):
            if fn.lower().endswith(".cue") and not fn.startswith("."):
                cue = Path(dirpath) / fn
                gid = extract_game_id_from_cue(cue) or "—"
                cue_cov = cue.with_suffix(".cov").is_file()
                folder_cov = (cue.parent != sd_root and (cue.parent.parent / f"{cue.parent.name}.cov").is_file())
                games.append(GameEntry(
                    cue_path=cue,
                    rel_path=str(cue.relative_to(sd_root)),
                    folder_name=cue.parent.name if cue.parent != sd_root else "",
                    game_id=gid,
                    has_cue_cov=cue_cov,
                    has_folder_cov=folder_cov,
                    has_cht=cue.with_suffix(".cht").is_file(),
                    has_lsd=cue.with_suffix(".lsd").is_file(),
                ))
    return games


def detect_default_db_dir() -> Optional[Path]:
    candidates: List[Path] = []
    if getattr(sys, "frozen", False):
        exe_dir = Path(sys.executable).resolve().parent
        candidates.append(exe_dir)
        candidates.append(exe_dir / "db")
    candidates.extend([
        Path.cwd(),
        Path.cwd() / "cloned_files" / "PicoStation_SD_Manager_07",
        Path.cwd() / "cloned_files" / "PicoStation.SD.manager",
        Path(__file__).resolve().parent,
        Path(__file__).resolve().parent.parent / "cloned_files" / "PicoStation_SD_Manager_07",
    ])
    for c in candidates:
        if (c / "covers").is_dir():
            return c
    return None


def _find_db_file(folder: Path, game_id: str, ext: str) -> Optional[Path]:
    """Finds <GAMEID>.<ext> supporting both SLUS-01279 and SLUS_012.79 naming (case-insensitive)."""
    if not folder.is_dir():
        return None
    # Build both canonical formats: SLUS-01279 and SLUS_012.79
    candidates = [f"{game_id}{ext}", f"{game_id}{ext.upper()}"]
    if "-" in game_id and len(game_id) == 10:
        prefix, digits = game_id.split("-", 1)
        if len(digits) == 5:
            alt_id = f"{prefix}_{digits[:3]}.{digits[3:]}"
            candidates.extend([f"{alt_id}{ext}", f"{alt_id}{ext.upper()}"])
    for name in candidates:
        p = folder / name
        if p.is_file():
            return p
    return None


def scan_and_populate_sd(
    sd_root: Path,
    db_root: Path,
    cover_style: str = "default",
    folder_cover_style: Optional[str] = "default",
    apply_folder_covers: bool = True,
    copy_cheats: bool = True,
    copy_lsd: bool = True,
    overwrite: bool = False,
    log: Callable[[str], None] = print,
    progress: Optional[Callable[[int, int], None]] = None,
) -> ScanStats:
    stats = ScanStats()
    cue_files: List[Path] = []
    skip_dirs = {"update", "picoplayer", "system volume information", "$recycle.bin"}

    for dirpath, dirnames, filenames in os.walk(sd_root):
        rel = Path(dirpath).relative_to(sd_root)
        if str(rel) == ".":
            dirnames[:] = [d for d in dirnames if d.lower() not in skip_dirs and not d.startswith(".")]
        else:
            dirnames[:] = [d for d in dirnames if not d.startswith(".")]
        for fn in sorted(filenames):
            if fn.lower().endswith(".cue") and not fn.startswith("."):
                cue_files.append(Path(dirpath) / fn)

    stats.cues_found = len(cue_files)
    total = max(1, len(cue_files))
    log(f"Encontrados {len(cue_files)} arquivos .cue no SD Card.")

    cover_dir = db_root / "covers" / cover_style
    alt_cover_dir = db_root / "covers" / "default"
    folder_cov_dir = db_root / "covers" / (folder_cover_style or cover_style)
    cht_dir = db_root / "cht"
    lsd_dir = db_root / "lsd"
    done_folders: set[Path] = set()

    for idx, cue in enumerate(cue_files):
        if progress:
            progress(idx + 1, total)

        game_id = extract_game_id_from_cue(cue)
        if not game_id:
            log(f"[AVISO] Sem SYSTEM.CNF/GameID: {cue.relative_to(sd_root)}")
            continue

        # 1) Cover for .cue (Disc image)
        src_cov = _find_db_file(cover_dir, game_id, ".cov") or _find_db_file(alt_cover_dir, game_id, ".cov")
        dst_cov = cue.with_suffix(".cov")
        if src_cov is not None:
            if overwrite or not dst_cov.is_file():
                try:
                    shutil.copy2(src_cov, dst_cov)
                    stats.covers_copied += 1
                    log(f"[DISCO] {game_id} -> {dst_cov.relative_to(sd_root)}")
                except OSError as e:
                    stats.errors += 1
                    log(f"[ERRO] Disco {dst_cov.name}: {e}")
        else:
            stats.missing_covers += 1
            log(f"[FALTA] Capa {game_id}.cov não encontrada para {cue.name}")

        # 2) Cover for parent game folder (Box/Cover image)
        if apply_folder_covers and cue.parent != sd_root and cue.parent not in done_folders:
            done_folders.add(cue.parent)
            f_src = _find_db_file(folder_cov_dir, game_id, ".cov") or src_cov
            f_dst = cue.parent.parent / f"{cue.parent.name}.cov"
            if f_src is not None and (overwrite or not f_dst.is_file()):
                try:
                    shutil.copy2(f_src, f_dst)
                    stats.folder_covers_copied += 1
                    log(f"[CAPA] {game_id} -> {f_dst.relative_to(sd_root)}")
                except OSError as e:
                    stats.errors += 1
                    log(f"[ERRO] Capa pasta {f_dst.name}: {e}")

        # 3) Cheat (.cht — supports both SLUS-01279.cht and SLUS_012.79.cht)
        if copy_cheats:
            src_cht = _find_db_file(cht_dir, game_id, ".cht")
            dst_cht = cue.with_suffix(".cht")
            if src_cht is not None and (overwrite or not dst_cht.is_file()):
                try:
                    shutil.copy2(src_cht, dst_cht)
                    stats.cheats_copied += 1
                    log(f"[CHEAT] {game_id} -> {dst_cht.relative_to(sd_root)}")
                except OSError as e:
                    stats.errors += 1
                    log(f"[ERRO] Cheat {dst_cht.name}: {e}")

        # 4) LibCrypt (.lsd — supports both SLUS-01279.lsd and SLUS_012.79.lsd)
        if copy_lsd:
            src_lsd = _find_db_file(lsd_dir, game_id, ".lsd")
            dst_lsd = cue.with_suffix(".lsd")
            if src_lsd is not None and (overwrite or not dst_lsd.is_file()):
                try:
                    shutil.copy2(src_lsd, dst_lsd)
                    stats.lsds_copied += 1
                    log(f"[LSD] {game_id} -> {dst_lsd.relative_to(sd_root)}")
                except OSError as e:
                    stats.errors += 1
                    log(f"[ERRO] LSD {dst_lsd.name}: {e}")

    return stats


# ---------------------------------------------------------------------------
# config.ini Reader / Writer
# ---------------------------------------------------------------------------

def load_config_ini(sd_root: Path) -> MenuConfigData:
    cfg = MenuConfigData()
    path = sd_root / "config.ini"
    if not path.is_file():
        return cfg
    try:
        for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or line.startswith(";") or "=" not in line:
                continue
            k, v = [p.strip() for p in line.split("=", 1)]
            kl = k.lower()
            if kl in ("background", "frame", "filelist", "cursor", "text"):
                hv = v.lstrip("#").upper()
                if len(hv) == 6 and all(c in "0123456789ABCDEF" for c in hv):
                    setattr(cfg, kl, hv)
            elif kl in ("style", "wallpaper", "actopen", "actback", "lang"):
                try:
                    setattr(cfg, kl, int(v, 0))
                except ValueError:
                    pass
    except OSError:
        pass
    return cfg


def save_config_ini(sd_root: Path, cfg: MenuConfigData) -> Path:
    path = sd_root / "config.ini"
    content = (
        f"background={cfg.background.upper()}\n"
        f"frame={cfg.frame.upper()}\n"
        f"filelist={cfg.filelist.upper()}\n"
        f"cursor={cfg.cursor.upper()}\n"
        f"text={cfg.text.upper()}\n"
        f"style={cfg.style}\n"
        f"wallpaper={cfg.wallpaper}\n"
        f"actopen={cfg.actopen}\n"
        f"actback={cfg.actback}\n"
        f"lang={cfg.lang}\n"
    )
    path.write_text(content, encoding="utf-8")
    return path


# ---------------------------------------------------------------------------
# PicoPlayer Audio Encoder (MP3/WAV) + Free Online ID3 Auto-Tagger
# ---------------------------------------------------------------------------

_TYPO_TRANS = str.maketrans({
    "\u2018": "'",   # ‘
    "\u2019": "'",   # ’
    "\u201A": "'",   # ‚
    "\u201B": "'",   # ‛
    "\u2032": "'",   # ′
    "\u201C": '"',   # “
    "\u201D": '"',   # ”
    "\u201E": '"',   # „
    "\u2033": '"',   # ″
    "\u2010": "-",   # ‐
    "\u2011": "-",   # ‑
    "\u2012": "-",   # ‒
    "\u2013": "-",   # –
    "\u2014": "-",   # —
    "\u2015": "-",   # ―
    "\u2212": "-",   # −
    "\u2026": "...", # …
    "\u00A0": " ",   # NBSP
})


def _sanitize_latin1(text: str) -> str:
    """Keeps clean Latin-1 characters supported by PicoPlayer's PU-20 font."""
    text = text.translate(_TYPO_TRANS).strip()
    out = []
    for ch in text:
        if 0x20 <= ord(ch) <= 0xFF:
            out.append(ch)
        else:
            norm = unicodedata.normalize("NFKD", ch).encode("latin-1", "ignore").decode("latin-1")
            out.append(norm)
    return "".join(out).strip()


def _sanitize_audio_stem(stem: str) -> str:
    """Produces a clean ASCII filename stem using only A-Z, a-z, 0-9 and '_' (accents/punctuation stay in ID3)."""
    s = stem.translate(_TYPO_TRANS)
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode("ascii")
    s = s.replace("'", "").replace('"', "")
    s = re.sub(r"[^A-Za-z0-9]+", "_", s)
    s = re.sub(r"_+", "_", s).strip("_")
    return s or "track"


def probe_audio_tags(src_path: Path) -> Dict[str, str]:
    tags = {"title": "", "artist": "", "album": ""}
    ffprobe_bin = find_ffprobe()
    if not ffprobe_bin:
        return tags
    try:
        res = subprocess.run(
            [ffprobe_bin, "-v", "quiet", "-print_format", "json", "-show_format", str(src_path)],
            capture_output=True, text=True, timeout=10, check=False, **_subprocess_kwargs(),
        )
        if res.returncode == 0 and res.stdout:
            data = json.loads(res.stdout)
            raw = data.get("format", {}).get("tags", {}) or {}
            low = {k.lower(): str(v) for k, v in raw.items()}
            tags["title"]  = _sanitize_latin1(low.get("title", ""))
            tags["artist"] = _sanitize_latin1(low.get("artist", ""))
            tags["album"]  = _sanitize_latin1(low.get("album", ""))
    except Exception:
        pass
    return tags


def lookup_online_id3(query_name: str) -> Dict[str, str]:
    """Free zero-key online ID3 metadata lookup via iTunes Search + MusicBrainz fallback."""
    # Clean track prefixes like "01 - ", "01. ", "[FLAC]", "(320kbps)"
    cleaned = re.sub(r"^\s*\d{1,3}[\s._-]+", "", query_name)
    cleaned = re.sub(r"[\[\(][^\]\)]*(kbps|flac|mp3|wav|ost|remaster|official)[^\]\)]*[\]\)]", "", cleaned, flags=re.I)
    cleaned = cleaned.replace("_", " ").strip()
    if len(cleaned) < 2:
        return {}

    # 1) iTunes Search API (free, no key, fast)
    try:
        url = "https://itunes.apple.com/search?" + urllib.parse.urlencode({
            "term": cleaned, "media": "music", "limit": 1
        })
        req = urllib.request.Request(url, headers={"User-Agent": "PicoManager/2.0"})
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode("utf-8", errors="ignore"))
            results = data.get("results") or []
            if results:
                r0 = results[0]
                art_url = str(r0.get("artworkUrl100", "") or "")
                if art_url:
                    art_url = art_url.replace("100x100bb", "300x300bb")
                return {
                    "title":       _sanitize_latin1(r0.get("trackName", "")),
                    "artist":      _sanitize_latin1(r0.get("artistName", "")),
                    "album":       _sanitize_latin1(r0.get("collectionName", "")),
                    "artwork_url": art_url,
                }
    except Exception:
        pass

    # 2) MusicBrainz API fallback (open-source, free, no key)
    try:
        url = "https://musicbrainz.org/ws/2/recording/?" + urllib.parse.urlencode({
            "query": cleaned, "fmt": "json", "limit": 1
        })
        req = urllib.request.Request(url, headers={"User-Agent": "PicoManager/2.0 (https://github.com/Schickacka/PicoStation)"})
        with urllib.request.urlopen(req, timeout=6) as resp:
            data = json.loads(resp.read().decode("utf-8", errors="ignore"))
            recs = data.get("recordings") or []
            if recs:
                r0 = recs[0]
                artist = ""
                ac = r0.get("artist-credit") or []
                if ac and isinstance(ac[0], dict):
                    artist = ac[0].get("name", "")
                album = ""
                rels = r0.get("releases") or []
                if rels and isinstance(rels[0], dict):
                    album = rels[0].get("title", "")
                return {
                    "title":  _sanitize_latin1(r0.get("title", "")),
                    "artist": _sanitize_latin1(artist),
                    "album":  _sanitize_latin1(album),
                }
    except Exception:
        pass

    return {}


def _export_album_art_cov(
    src_file: Path,
    dst_cov: Path,
    artwork_url: str,
    auto_tag_online: bool,
    query_hint: str,
    log: Callable[[str], None],
) -> bool:
    """
    Generates <Track>.cov (128x128 8bpp PS1 format, 16,896 B) for PicoPlayer:
      1) First extracts embedded cover art from src_file via ffmpeg.
      2) Falls back to downloading official album art from iTunes when auto_tag_online is enabled.
    """
    if Image is None:
        return False

    try:
        with tempfile.TemporaryDirectory(prefix="picomanager_art_") as tmpdir:
            tmp_dir = Path(tmpdir)
            emb_img = tmp_dir / "embedded.png"
            # 1) Try embedded art (APIC / attached_pic)
            ffmpeg_bin = find_ffmpeg()
            if ffmpeg_bin:
                res = subprocess.run(
                    [ffmpeg_bin, "-y", "-v", "error", "-i", str(src_file),
                     "-an", "-vframes", "1", str(emb_img)],
                    capture_output=True, text=True, timeout=10, check=False, **_subprocess_kwargs(),
                )
                if res.returncode == 0 and emb_img.is_file() and emb_img.stat().st_size > 0:
                    convert_image_to_cov(emb_img, dst_cov)
                    log(f"  [CAPA] Embutida -> {dst_cov.name}")
                    return True

            # 2) Fallback: online iTunes artwork (if enabled)
            if auto_tag_online:
                url = artwork_url
                if not url and query_hint:
                    found = lookup_online_id3(query_hint)
                    url = found.get("artwork_url", "")
                if url:
                    dl_img = tmp_dir / "online.jpg"
                    req = urllib.request.Request(url, headers={"User-Agent": "PicoManager/2.0"})
                    with urllib.request.urlopen(req, timeout=6) as resp:
                        dl_img.write_bytes(resp.read())
                    if dl_img.is_file() and dl_img.stat().st_size > 0:
                        convert_image_to_cov(dl_img, dst_cov)
                        log(f"  [CAPA] Online -> {dst_cov.name}")
                        return True
    except Exception:
        pass
    return False


_SOXR_CACHE: Tuple[Optional[str], Optional[bool]] = (None, None)


def _has_ffmpeg_soxr(ffmpeg_bin: Optional[str] = None) -> bool:
    """Returns True if the resolved ffmpeg binary supports libsoxr resampling."""
    global _SOXR_CACHE
    bin_path = ffmpeg_bin or find_ffmpeg()
    if not bin_path:
        return False
    if _SOXR_CACHE[0] != bin_path or _SOXR_CACHE[1] is None:
        try:
            res = subprocess.run(
                [bin_path, "-v", "quiet", "-buildconf"],
                capture_output=True, text=True, timeout=5, check=False, **_subprocess_kwargs(),
            )
            _SOXR_CACHE = (bin_path, "--enable-libsoxr" in (res.stdout or ""))
        except Exception:
            _SOXR_CACHE = (bin_path, False)
    return bool(_SOXR_CACHE[1])


def _probe_source_sample_rate(src_file: Path) -> Optional[int]:
    """Returns source audio sample rate in Hz via ffprobe, or None if unavailable."""
    ffprobe_bin = find_ffprobe()
    if not ffprobe_bin:
        return None
    try:
        res = subprocess.run(
            [ffprobe_bin, "-v", "quiet", "-select_streams", "a:0",
             "-show_entries", "stream=sample_rate", "-of", "csv=p=0", str(src_file)],
            capture_output=True, text=True, timeout=8, check=False, **_subprocess_kwargs(),
        )
        val = (res.stdout or "").strip().splitlines()
        if res.returncode == 0 and val:
            return int(val[0].strip())
    except Exception:
        pass
    return None


def convert_audio_for_picoplayer(
    src_file: Path,
    sd_root: Path,
    profile: str = "mp3_v0",
    auto_tag_online: bool = True,
    log: Callable[[str], None] = print,
) -> Path:
    """
    Converts a single audio file to PS1/RP2040-optimal format inside <SD>/PicoPlayer/.
    Also extracts embedded album art (or fetches online artwork) to <Track>.cov (128x128 8bpp).
    Profiles: 'mp3_v0' (default, LAME VBR -V 0 -q 0), 'mp3_320' (LAME 320k CBR -q 0), 'wav_cd'
    """
    ffmpeg_bin = find_ffmpeg()
    if not ffmpeg_bin:
        raise RuntimeError(
            f"FFmpeg não encontrado em '{get_app_dir() / 'ffmpeg'}' nem no PATH. "
            f"Baixe em {FFMPEG_DOWNLOAD_URL}"
        )

    out_dir = sd_root / "PicoPlayer"
    out_dir.mkdir(parents=True, exist_ok=True)

    tags = probe_audio_tags(src_file)
    artwork_url = ""
    if auto_tag_online and (not tags["title"] or not tags["artist"]):
        found = lookup_online_id3(src_file.stem)
        if found:
            tags["title"]  = tags["title"]  or found.get("title", "")
            tags["artist"] = tags["artist"] or found.get("artist", "")
            tags["album"]  = tags["album"]  or found.get("album", "")
            artwork_url    = found.get("artwork_url", "")
            log(f"  [ID3] {tags['title']} — {tags['artist']} ({tags['album']})")

    if not tags["title"]:
        tags["title"] = _sanitize_latin1(src_file.stem)

    safe_stem = _sanitize_audio_stem(src_file.stem)
    is_wav = (profile == "wav_cd")
    dst_file = out_dir / (f"{safe_stem}.wav" if is_wav else f"{safe_stem}.mp3")

    cmd = [ffmpeg_bin, "-y", "-v", "error", "-i", str(src_file), "-vn", "-ac", "2"]

    # Preserve bit-exact 44.1 kHz PCM when source is already 44.1 kHz; otherwise use SoXR (precision=28)
    src_sr = _probe_source_sample_rate(src_file)
    if src_sr != 44100:
        if _has_ffmpeg_soxr(ffmpeg_bin):
            cmd += ["-af", "aresample=44100:resampler=soxr:precision=28"]
        else:
            cmd += ["-ar", "44100"]

    if is_wav:
        cmd += ["-c:a", "pcm_s16le"]
    else:
        if profile == "mp3_v0":
            cmd += ["-c:a", "libmp3lame", "-q:a", "0", "-compression_level", "0", "-id3v2_version", "3"]
        else:
            cmd += ["-c:a", "libmp3lame", "-b:a", "320k", "-compression_level", "0", "-id3v2_version", "3"]
        if tags["title"]:
            cmd += ["-metadata", f"title={tags['title']}"]
        if tags["artist"]:
            cmd += ["-metadata", f"artist={tags['artist']}"]
        if tags["album"]:
            cmd += ["-metadata", f"album={tags['album']}"]
    cmd.append(str(dst_file))

    res = subprocess.run(cmd, capture_output=True, text=True, check=False, **_subprocess_kwargs())
    if res.returncode != 0:
        raise RuntimeError(f"ffmpeg falhou em {src_file.name}: {res.stderr.strip()}")

    # Export <Track>.cov (128x128 8bpp PS1 album art) alongside the audio file
    query_hint = f"{tags['artist']} {tags['title']}".strip() if tags["artist"] else src_file.stem
    _export_album_art_cov(src_file, dst_file.with_suffix(".cov"), artwork_url, auto_tag_online, query_hint, log)
    return dst_file


# ---------------------------------------------------------------------------
# PicoPlayer Precomputed TOC Cache (SD:/PicoPlayer/index.pp — "PPT1" v1)
# ---------------------------------------------------------------------------

_PP_MAX_TRACKS = 99
_PP_MAX_DISC_SECTORS = 449800  # ~99:57:00 (leaves headroom for 150-sector pregap in 2-digit BCD)
_PP_FMT_MP3 = 1
_PP_FMT_WAV = 2


def _fnv1a_file_sig(filename: str, fsize: int) -> int:
    """Must match fnv1aFileSig() in src/pico_audio.cpp byte-for-byte."""
    h = 2166136261
    for b in filename.encode("utf-8", errors="ignore"):
        if 65 <= b <= 90:  # ASCII A..Z -> a..z
            b += 32
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    h = ((h ^ 0xFF) * 16777619) & 0xFFFFFFFF
    sz = fsize & 0xFFFFFFFF
    for shift in (0, 8, 16, 24):
        h = ((h ^ ((sz >> shift) & 0xFF)) * 16777619) & 0xFFFFFFFF
    return h


def _pack_fixed_str(text: str, size: int) -> bytes:
    raw = _sanitize_latin1(text).encode("utf-8", errors="ignore")
    if len(raw) >= size:
        raw = raw[: size - 1]
        # Avoid cutting a multi-byte UTF-8 sequence in half
        while raw and (raw[-1] & 0xC0) == 0x80:
            raw = raw[:-1]
        if raw and (raw[-1] & 0x80):
            raw = raw[:-1]
    return raw + b"\x00" * (size - len(raw))


def _ffprobe_audio_info(path: Path) -> Tuple[Optional[float], Dict[str, str]]:
    """Returns (duration_seconds, {title, artist, album}) via ffprobe if available."""
    tags = {"title": "", "artist": "", "album": ""}
    dur: Optional[float] = None
    ffprobe_bin = find_ffprobe()
    if not ffprobe_bin:
        return dur, tags
    try:
        res = subprocess.run(
            [ffprobe_bin, "-v", "quiet", "-print_format", "json", "-show_format", str(path)],
            capture_output=True, text=True, timeout=10, check=False, **_subprocess_kwargs(),
        )
        if res.returncode == 0 and res.stdout:
            fmt = json.loads(res.stdout).get("format", {}) or {}
            if "duration" in fmt:
                try:
                    d = float(fmt["duration"])
                    if d > 0.0:
                        dur = d
                except ValueError:
                    pass
            raw = fmt.get("tags", {}) or {}
            low = {k.lower(): str(v) for k, v in raw.items()}
            tags["title"]  = _sanitize_latin1(low.get("title", ""))
            tags["artist"] = _sanitize_latin1(low.get("artist", ""))
            tags["album"]  = _sanitize_latin1(low.get("album", ""))
    except Exception:
        pass
    return dur, tags


def _probe_wav_for_index(path: Path) -> Optional[dict]:
    fsize = path.stat().st_size
    if fsize < 44:
        return None
    with path.open("rb") as f:
        hdr = f.read(12)
        if len(hdr) < 12 or hdr[:4] != b"RIFF" or hdr[8:12] != b"WAVE":
            return None
        got_fmt = got_data = False
        audio_fmt = channels = bits = 0
        sample_rate = data_off = data_bytes = 0
        while not (got_fmt and got_data):
            chdr = f.read(8)
            if len(chdr) < 8:
                break
            cid = chdr[:4]
            csize = struct.unpack("<I", chdr[4:8])[0]
            cpos = f.tell()
            if cid == b"fmt " and csize >= 16:
                fbuf = f.read(16)
                if len(fbuf) == 16:
                    audio_fmt, channels, sample_rate, _, _, bits = struct.unpack("<HHIIHH", fbuf)
                    got_fmt = True
            elif cid == b"data":
                data_off = cpos
                data_bytes = csize
                got_data = True
            f.seek(cpos + csize + (csize & 1))

    if not (got_fmt and got_data) or audio_fmt != 1 or bits != 16 or channels not in (1, 2) or sample_rate != 44100:
        return None
    if data_off >= fsize:
        return None
    data_bytes = min(data_bytes, fsize - data_off)
    if data_bytes < 2352:
        return None
    bytes_per_sec = 2352 if channels == 2 else 1176
    length_sectors = max(75, min(330000, (data_bytes + bytes_per_sec - 1) // bytes_per_sec))
    _, tags = _ffprobe_audio_info(path)
    return {
        "format": _PP_FMT_WAV,
        "channels": channels,
        "bitrate_kbps": (sample_rate * channels * 16) // 1000,
        "sample_rate_hz": sample_rate,
        "data_offset": data_off,
        "data_bytes": data_bytes,
        "length_sectors": length_sectors,
        "title": tags["title"] or _sanitize_latin1(path.stem),
        "artist": tags["artist"],
        "album": tags["album"] or "SD:/PICOPLAYER/",
    }


def _probe_mp3_for_index(path: Path) -> Optional[dict]:
    fsize = path.stat().st_size
    if fsize < 128:
        return None
    with path.open("rb") as f:
        hdr = f.read(10)
        data_off = 0
        if len(hdr) == 10 and hdr[:3] == b"ID3":
            tag_sz = ((hdr[6] & 0x7F) << 21) | ((hdr[7] & 0x7F) << 14) | ((hdr[8] & 0x7F) << 7) | (hdr[9] & 0x7F)
            data_off = 10 + tag_sz
            if data_off >= fsize:
                data_off = 0
        f.seek(data_off)
        scan = f.read(4096)

        # Check trailing ID3v1 "TAG"
        audio_end = fsize
        if fsize > data_off + 128:
            f.seek(fsize - 128)
            if f.read(3) == b"TAG":
                audio_end = fsize - 128

    br_m1 = (0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0)
    br_m2 = (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0)
    sr_m1 = (44100, 48000, 32000, 0)
    sr_m2 = (22050, 24000, 16000, 0)
    sr_m25 = (11025, 12000, 8000, 0)

    sync = -1
    for i in range(len(scan) - 3):
        if scan[i] == 0xFF and (scan[i + 1] & 0xE0) == 0xE0:
            vb = (scan[i + 1] >> 3) & 0x03
            lb = (scan[i + 1] >> 1) & 0x03
            bi = (scan[i + 2] >> 4) & 0x0F
            si = (scan[i + 2] >> 2) & 0x03
            if vb != 1 and lb == 1 and 0 < bi < 15 and si < 3:
                sync = i
                break
    if sync < 0:
        return None

    h0, h1, h2, h3 = scan[sync : sync + 4]
    vb = (h1 >> 3) & 0x03
    bi = (h2 >> 4) & 0x0F
    si = (h2 >> 2) & 0x03
    pad = (h2 >> 1) & 0x01
    ch_mode = (h3 >> 6) & 0x03

    kbps = br_m1[bi] if vb == 3 else br_m2[bi]
    sr = sr_m1[si] if vb == 3 else (sr_m2[si] if vb == 2 else sr_m25[si])
    if kbps <= 0 or sr <= 0:
        return None
    channels = 1 if ch_mode == 3 else 2
    samples_per_frame = 1152 if vb == 3 else 576
    first_frame_bytes = ((144 if vb == 3 else 72) * kbps * 1000) // sr + pad
    data_off += sync

    # Detect Xing / Info / VBRI header
    xing_frames = 0
    side_info = (17 if channels == 1 else 32) if vb == 3 else (9 if channels == 1 else 17)
    x_off = sync + 4 + side_info
    if x_off + 12 <= len(scan) and scan[x_off : x_off + 4] in (b"Xing", b"Info"):
        flags = struct.unpack(">I", scan[x_off + 4 : x_off + 8])[0]
        if flags & 0x01:
            xing_frames = struct.unpack(">I", scan[x_off + 8 : x_off + 12])[0]
        if first_frame_bytes >= 24 and data_off + first_frame_bytes < audio_end:
            data_off += first_frame_bytes
    elif sync + 36 + 18 <= len(scan) and scan[sync + 36 : sync + 40] == b"VBRI":
        xing_frames = struct.unpack(">I", scan[sync + 36 + 14 : sync + 36 + 18])[0]
        if first_frame_bytes >= 24 and data_off + first_frame_bytes < audio_end:
            data_off += first_frame_bytes

    audio_bytes = max(0, audio_end - data_off)
    if audio_bytes < 128:
        return None

    dur_sec, tags = _ffprobe_audio_info(path)
    if xing_frames > 0:
        length_sectors = (xing_frames * samples_per_frame * 75) // sr
        if length_sectors >= 75:
            kbps = max(8, (audio_bytes * 8 * 75) // (length_sectors * 1000))
    elif dur_sec is not None and dur_sec >= 1.0:
        length_sectors = int(round(dur_sec * 75.0))
        kbps = max(8, int((audio_bytes * 8) / (dur_sec * 1000.0)))
    else:
        length_sectors = (audio_bytes * 8 * 75) // (kbps * 1000)

    length_sectors = max(75, min(330000, length_sectors))
    return {
        "format": _PP_FMT_MP3,
        "channels": channels,
        "bitrate_kbps": min(65535, int(kbps)),
        "sample_rate_hz": sr,
        "data_offset": data_off,
        "data_bytes": audio_bytes,
        "length_sectors": length_sectors,
        "title": tags["title"] or _sanitize_latin1(path.stem),
        "artist": tags["artist"],
        "album": tags["album"] or "SD:/PICOPLAYER/",
    }


def build_picoplayer_index(
    sd_root: Path | str,
    log: Callable[[str], None] = print,
) -> Tuple[Path, int]:
    """
    Scans <SD>/PicoPlayer/ and writes <SD>/PicoPlayer/index.pp ("PPT1" v1) so
    the RP2040 loads the full PicoPlayer playlist in < 20 ms with exact durations
    and automatic FNV-1a directory change detection!
    """
    pp_dir = Path(sd_root) / "PicoPlayer"
    pp_dir.mkdir(parents=True, exist_ok=True)
    idx_path = pp_dir / "index.pp"

    dir_file_count = 0
    dir_hash_xor = 0
    dir_hash_sum = 0
    audio_files: List[Path] = []
    cov_sizes: Dict[str, int] = {}

    for entry in pp_dir.iterdir():
        if not entry.is_file() or entry.name.startswith("."):
            continue
        low = entry.name.lower()
        if low.endswith((".mp3", ".wav", ".cov")):
            sz = entry.stat().st_size
            sig = _fnv1a_file_sig(entry.name, sz)
            dir_hash_xor ^= sig
            dir_hash_sum = (dir_hash_sum + sig) & 0xFFFFFFFF
            dir_file_count += 1
            if low.endswith(".cov"):
                cov_sizes[low] = sz
            else:
                audio_files.append(entry)

    audio_files.sort(key=lambda p: p.name.lower())
    has_folder_art = cov_sizes.get("cover.cov", 0) >= 16896
    # Deduplicate identical .cov files into artGroup IDs (2..254) so PicoPlayer
    # transfers each unique album cover ONCE instead of once per track!
    import hashlib
    cov_hash_to_group: Dict[str, int] = {}
    next_art_group = 2

    packed_entries: List[bytes] = []
    cur_sector = 0

    for p in audio_files:
        if len(packed_entries) >= _PP_MAX_TRACKS:
            break
        fname_raw = p.name.encode("utf-8", errors="ignore")
        if len(fname_raw) >= 128:
            log(f"  [AVISO] Nome longo (>127 B) ignorado: {p.name}")
            continue
        info = _probe_wav_for_index(p) if p.suffix.lower() == ".wav" else _probe_mp3_for_index(p)
        if not info:
            continue
        if info["length_sectors"] > 328500:
            info["length_sectors"] = 328500
        has_art = 0
        cov_candidate = pp_dir / f"{p.stem}.cov"
        if not cov_candidate.is_file():
            # Case-insensitive lookup for <stem>.cov
            want_cov = f"{p.stem.lower()}.cov"
            if cov_sizes.get(want_cov, 0) >= 16896:
                for cand in pp_dir.iterdir():
                    if cand.name.lower() == want_cov:
                        cov_candidate = cand
                        break
        if cov_candidate.is_file() and cov_candidate.stat().st_size >= 16896:
            h = hashlib.sha256(cov_candidate.read_bytes()[:16896]).hexdigest()
            grp = cov_hash_to_group.get(h)
            if grp is None:
                grp = next_art_group if next_art_group < 254 else 1
                if next_art_group < 254:
                    cov_hash_to_group[h] = grp
                    next_art_group += 1
            has_art = grp
        elif has_folder_art:
            has_art = 254
        rec = struct.pack(
            "<128s36s28s28sBBBBHHIIII",
            fname_raw + b"\x00" * (128 - len(fname_raw)),
            _pack_fixed_str(info["title"], 36),
            _pack_fixed_str(info["artist"], 28),
            _pack_fixed_str(info["album"], 28),
            info["format"],
            info["channels"],
            has_art,
            0,
            info["bitrate_kbps"],
            info["sample_rate_hz"],
            info["data_offset"],
            info["data_bytes"],
            cur_sector,
            info["length_sectors"],
        )
        assert len(rec) == 244
        packed_entries.append(rec)
        cur_sector += info["length_sectors"]

    if not packed_entries:
        if idx_path.is_file():
            idx_path.unlink()
        log("[TOC] Nenhuma faixa válida em SD:/PicoPlayer/ (index.pp removido).")
        return idx_path, 0

    header = struct.pack(
        "<4sHHHHII",
        b"PPT1",
        1,
        len(packed_entries),
        dir_file_count,
        0,
        dir_hash_xor,
        dir_hash_sum,
    )
    assert len(header) == 20
    tmp_path = pp_dir / ".index.pp.tmp"
    tmp_path.write_bytes(header + b"".join(packed_entries))
    tmp_path.replace(idx_path)

    total_min = cur_sector // (75 * 60)
    total_sec = (cur_sector // 75) % 60
    log(f"[TOC] index.pp gerado! {len(packed_entries)} faixa(s), {total_min:02d}:{total_sec:02d} ({idx_path.stat().st_size} bytes)")
    return idx_path, len(packed_entries)
