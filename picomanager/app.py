#!/usr/bin/env python3
"""
PicoManager 2.1 — Open-source SD Card Manager & PicoPlayer Audio Encoder.
PU-20 inspired desktop GUI (PT-BR / EN) for Linux and Windows.
"""

from __future__ import annotations

import sys
import threading
import tkinter as tk
import webbrowser
from pathlib import Path
from tkinter import colorchooser, filedialog, messagebox, ttk
from typing import Dict, List, Optional, Tuple

try:
    from PIL import ImageTk
except ImportError:
    ImageTk = None  # type: ignore

from core import (
    AUDIO_EXTS,
    BUTTON_OPTIONS,
    FFMPEG_DOWNLOAD_URL,
    LANGUAGES,
    PU20_PRESET,
    RC5_CLASSIC_PRESET,
    GameEntry,
    MenuConfigData,
    build_picoplayer_index,
    convert_audio_for_picoplayer,
    convert_image_to_cov,
    convert_image_to_wallpaper,
    decode_cov_to_ppm,
    detect_default_db_dir,
    find_ffmpeg,
    get_app_dir,
    list_sd_games,
    list_storage_drives,
    load_config_ini,
    save_config_ini,
    scan_and_populate_sd,
)

# PU-20 Palette
BG_DEEP   = "#06081E"
BG_PANEL  = "#121740"
BG_CARD   = "#1A2156"
BORDER_HI = "#3141C5"
ACCENT    = "#3E52E8"
ACCENT_HI = "#6278FF"
FG_MAIN   = "#F4F7FF"
FG_DIM    = "#9AA8DF"

STRINGS: Dict[str, Dict[str, str]] = {
    "pt": {
        "title": "PicoManager",
        "sd_label": "Cartão SD:",
        "show_fixed": "Mostrar fixos / pasta local",
        "browse_folder": "Pasta...",
        "refresh": "↻",
        "db_label": "Banco (/covers):",
        "db_auto": "Detectado automaticamente",
        "tab_covers": "  Capas  ",
        "tab_settings": "  Configurações  ",
        "tab_player": "  PicoPlayer  ",
        "cue_style": "Imagem do Disco:",
        "folder_style": "Imagem da Capa:",
        "apply_folders": "Aplicar capas nas pastas",
        "copy_cht": "Copiar Cheats (.cht)",
        "copy_lsd": "Copiar LibCrypt (.lsd)",
        "overwrite": "Sobrescrever existentes",
        "btn_autoscan": "Auto-instalar do Banco",
        "btn_rescan": "Recarregar Lista",
        "btn_set_disc": "Selecionar Imagem do Disco",
        "btn_set_folder": "Selecionar Imagem da Capa",
        "no_cover": "Sem capa\n128×128",
        "col_game": "Jogo (.cue)",
        "col_id": "Game ID",
        "col_cov": "Capa",
        "col_cht": "CHT",
        "col_lsd": "LSD",
        "cfg_bg_note": "O fundo padrão do PicoMenu é preservado automaticamente (exceto se ativar Wallpaper).",
        "lbl_filelist": "Fundo da Lista:",
        "lbl_frame": "Borda do Retângulo:",
        "lbl_cursor": "Barra de Seleção:",
        "lbl_text": "Cor do Texto:",
        "preset_pu20": "Tema Padrão",
        "preset_rc5": "Tema Clássico",
        "lbl_menu_lang": "Idioma do PicoMenu:",
        "lbl_app_lang": "Idioma do PicoManager:",
        "lbl_style": "Estilo da Lista:",
        "style_0": "0 - Com Capas (Padrão)",
        "style_1": "1 - Tela Cheia",
        "lbl_open": "Botão Abrir:",
        "lbl_back": "Botão Voltar:",
        "chk_wp": "Usar Wallpaper customizado (background.raw)",
        "btn_save_cfg": "Salvar config.ini no SD",
        "btn_make_wp": "Criar Wallpaper (PNG/JPG)",
        "preview_title": "Prévia do PicoMenu:",
        "audio_qual": "Qualidade (44.1 kHz Estéreo):",
        "audio_src": "Origem selecionada:",
        "audio_src_none": "(Nenhum arquivo ou pasta selecionado)",
        "chk_id3": "Buscar tags ID3 e capa automaticamente online",
        "btn_pick_one": "Selecionar 1 Arquivo...",
        "btn_pick_dir": "Selecionar Pasta (Lote)...",
        "btn_build_toc": "Gerar/Atualizar TOC (index.pp)",
        "btn_start_conv": "Iniciar Conversão → SD:/PicoPlayer/",
        "warn_audio_src": "Selecione primeiro um arquivo de áudio ou uma pasta.",
        "warn_sd": "Selecione primeiro o Cartão SD (ou marque 'Mostrar fixos / pasta local' e escolha uma pasta).",
        "warn_db": "Pasta do banco (/covers) não encontrada. Coloque a pasta 'covers/' ao lado do PicoManager ou selecione-a acima.",
        "warn_sel": "Clique em um jogo na lista primeiro.",
        "saved_ok": "Salvo com sucesso!",
    },
    "en": {
        "title": "PicoManager",
        "sd_label": "SD Card:",
        "show_fixed": "Show fixed drives / local folder",
        "browse_folder": "Folder...",
        "refresh": "↻",
        "db_label": "Database (/covers):",
        "db_auto": "Auto-detected",
        "tab_covers": "  Covers  ",
        "tab_settings": "  Settings  ",
        "tab_player": "  PicoPlayer  ",
        "cue_style": "Disc Image:",
        "folder_style": "Cover Image:",
        "apply_folders": "Apply covers to folders",
        "copy_cht": "Copy Cheats (.cht)",
        "copy_lsd": "Copy LibCrypt (.lsd)",
        "overwrite": "Overwrite existing files",
        "btn_autoscan": "Auto-Install from DB",
        "btn_rescan": "Reload List",
        "btn_set_disc": "Select Disc Image",
        "btn_set_folder": "Select Cover Image",
        "no_cover": "No cover\n128×128",
        "col_game": "Game (.cue)",
        "col_id": "Game ID",
        "col_cov": "Cov",
        "col_cht": "CHT",
        "col_lsd": "LSD",
        "cfg_bg_note": "The default PicoMenu background is preserved automatically (unless Wallpaper is enabled).",
        "lbl_filelist": "List Box Fill:",
        "lbl_frame": "Rectangle Border:",
        "lbl_cursor": "Selection Bar:",
        "lbl_text": "Text Color:",
        "preset_pu20": "Default Theme",
        "preset_rc5": "Classic Theme",
        "lbl_menu_lang": "PicoMenu Language:",
        "lbl_app_lang": "PicoManager Language:",
        "lbl_style": "Menu Style:",
        "style_0": "0 - With Covers (Default)",
        "style_1": "1 - Full Width",
        "lbl_open": "Open Button:",
        "lbl_back": "Back Button:",
        "chk_wp": "Use custom Wallpaper (background.raw)",
        "btn_save_cfg": "Save config.ini to SD",
        "btn_make_wp": "Create Wallpaper (PNG/JPG)",
        "preview_title": "PicoMenu Preview:",
        "audio_qual": "Output Quality (44.1 kHz Stereo):",
        "audio_src": "Selected Source:",
        "audio_src_none": "(No file or folder selected)",
        "chk_id3": "Auto-fetch ID3 tags & album art online",
        "btn_pick_one": "Select 1 File...",
        "btn_pick_dir": "Select Folder (Batch)...",
        "btn_build_toc": "Build/Update TOC (index.pp)",
        "btn_start_conv": "Start Conversion → SD:/PicoPlayer/",
        "warn_audio_src": "Select an audio file or folder first.",
        "warn_sd": "Select an SD Card first (or check 'Show fixed drives / local folder' and pick a folder).",
        "warn_db": "Database (/covers) folder not found. Place 'covers/' next to PicoManager or select it above.",
        "warn_sel": "Click a game in the list first.",
        "saved_ok": "Saved successfully!",
    },
}


class PicoManagerApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.app_lang = "pt"
        self.geometry("1020x700")
        self.minsize(980, 640)
        self.configure(bg=BG_DEEP)

        # Fix Tk Combobox dropdown listbox colors globally
        self.option_add("*TCombobox*Listbox.background", BG_CARD)
        self.option_add("*TCombobox*Listbox.foreground", FG_MAIN)
        self.option_add("*TCombobox*Listbox.selectBackground", ACCENT)
        self.option_add("*TCombobox*Listbox.selectForeground", "#FFFFFF")

        self.sd_path: Optional[Path] = None
        self.drive_map: Dict[str, str] = {}
        self.show_fixed_var = tk.BooleanVar(value=False)
        self.drive_var = tk.StringVar(value="")

        default_db = detect_default_db_dir()
        self.db_var = tk.StringVar(value=str(default_db) if default_db else "")

        self.cfg = MenuConfigData()
        self.games: List[GameEntry] = []
        self.selected_game: Optional[GameEntry] = None
        self._cov_photo_folder = None
        self._cov_photo_disc = None
        self._app_icon_photo = None
        self.busy = False

        self._set_window_icon()
        self._setup_styles()
        self._build_ui()
        self._refresh_drives()
        self._refresh_config_ui()
        self.after(250, self._check_ffmpeg_on_startup)

    def _check_ffmpeg_on_startup(self) -> None:
        if find_ffmpeg():
            return
        self._show_ffmpeg_missing_dialog()

    def _show_ffmpeg_missing_dialog(self) -> None:
        ffmpeg_dir = get_app_dir() / "ffmpeg"
        dlg = tk.Toplevel(self)
        dlg.title("FFmpeg não encontrado" if self.app_lang == "pt" else "FFmpeg not found")
        dlg.configure(bg=BG_PANEL)
        dlg.resizable(False, False)
        dlg.transient(self)
        dlg.grab_set()

        frame = tk.Frame(dlg, bg=BG_PANEL, padx=20, pady=18)
        frame.pack(fill="both", expand=True)

        if self.app_lang == "pt":
            heading = "FFmpeg não encontrado"
            body = (
                "O FFmpeg é necessário para converter músicas para o PicoPlayer.\n"
                "(As funções de jogos, capas .cov, config.ini e índice index.pp continuam funcionando normalmente.)\n\n"
                "Para ativar a conversão de áudio, baixe o FFmpeg oficial e coloque os arquivos\n"
                "ffmpeg e ffprobe dentro da pasta abaixo (ou instale no PATH do sistema):"
            )
            btn_dl = "Baixar FFmpeg (Site Oficial)"
            btn_ok = "Continuar"
        else:
            heading = "FFmpeg not found"
            body = (
                "FFmpeg is required to convert music for PicoPlayer.\n"
                "(Games, .cov covers, config.ini, and index.pp TOC still work normally.)\n\n"
                "To enable audio conversion, download official FFmpeg and place\n"
                "ffmpeg and ffprobe inside the folder below (or install to system PATH):"
            )
            btn_dl = "Download FFmpeg (Official Site)"
            btn_ok = "Continue"

        tk.Label(
            frame, text=heading, bg=BG_PANEL, fg="#FFD56B",
            font=("DejaVu Sans", 12, "bold"), anchor="w",
        ).pack(fill="x", pady=(0, 8))

        tk.Label(
            frame, text=body, bg=BG_PANEL, fg=FG_MAIN,
            font=("DejaVu Sans", 9), justify="left", anchor="w",
        ).pack(fill="x", pady=(0, 8))

        path_box = tk.Entry(
            frame, bg=BG_CARD, fg="#8BE9FD", relief="flat",
            font=("DejaVu Sans Mono", 9), readonlybackground=BG_CARD,
        )
        path_box.insert(0, str(ffmpeg_dir))
        path_box.configure(state="readonly")
        path_box.pack(fill="x", ipady=5, pady=(0, 10))

        link_lbl = tk.Label(
            frame, text=FFMPEG_DOWNLOAD_URL, bg=BG_PANEL, fg="#6FB1FF",
            font=("DejaVu Sans", 9, "underline"), cursor="hand2", anchor="w",
        )
        link_lbl.pack(fill="x", pady=(0, 14))
        link_lbl.bind("<Button-1>", lambda _e: webbrowser.open(FFMPEG_DOWNLOAD_URL))

        btns = tk.Frame(frame, bg=BG_PANEL)
        btns.pack(fill="x")
        tk.Button(
            btns, text=btn_dl, bg=ACCENT, fg="#FFFFFF", relief="flat",
            font=("DejaVu Sans", 9, "bold"), padx=12, pady=6, cursor="hand2",
            command=lambda: webbrowser.open(FFMPEG_DOWNLOAD_URL),
        ).pack(side="left")
        tk.Button(
            btns, text=btn_ok, bg=BG_CARD, fg=FG_MAIN, relief="flat",
            font=("DejaVu Sans", 9), padx=14, pady=6, cursor="hand2",
            command=dlg.destroy,
        ).pack(side="right")

        dlg.update_idletasks()
        x = self.winfo_rootx() + max(0, (self.winfo_width() - dlg.winfo_width()) // 2)
        y = self.winfo_rooty() + max(0, (self.winfo_height() - dlg.winfo_height()) // 2)
        dlg.geometry(f"+{x}+{y}")

    def _set_window_icon(self) -> None:
        base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
        ico = base / "assets" / "icon.ico"
        png = base / "assets" / "icon.png"
        try:
            if sys.platform.startswith("win") and ico.is_file():
                self.iconbitmap(default=str(ico))
        except Exception:
            pass
        try:
            if png.is_file():
                self._app_icon_photo = tk.PhotoImage(file=str(png))
                self.iconphoto(True, self._app_icon_photo)
        except Exception:
            pass

    def t(self, key: str) -> str:
        return STRINGS[self.app_lang].get(key, key)

    def _setup_styles(self) -> None:
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure(".", background=BG_DEEP, foreground=FG_MAIN, font=("DejaVu Sans", 10))
        style.configure("TFrame", background=BG_DEEP)
        style.configure("Panel.TFrame", background=BG_PANEL)
        style.configure("Card.TFrame", background=BG_CARD)
        style.configure("TLabel", background=BG_DEEP, foreground=FG_MAIN)
        style.configure("Panel.TLabel", background=BG_PANEL, foreground=FG_MAIN)
        style.configure("Dim.TLabel", background=BG_PANEL, foreground=FG_DIM, font=("DejaVu Sans", 9))
        style.configure("TCheckbutton", background=BG_PANEL, foreground=FG_MAIN)
        style.map("TCheckbutton", background=[("active", BG_PANEL)], foreground=[("active", "#FFFFFF")])
        style.configure("TButton", background=ACCENT, foreground="#FFFFFF", padding=(11, 6), borderwidth=1, relief="flat")
        style.map("TButton", background=[("active", ACCENT_HI), ("disabled", "#252B58")])
        style.configure("TEntry", fieldbackground=BG_CARD, foreground=FG_MAIN, insertcolor=FG_MAIN, bordercolor=BORDER_HI)
        style.configure("TCombobox", fieldbackground=BG_CARD, background=BG_CARD, foreground=FG_MAIN, arrowcolor=FG_MAIN, bordercolor=BORDER_HI)
        style.map("TCombobox", fieldbackground=[("readonly", BG_CARD)], foreground=[("readonly", FG_MAIN)], selectbackground=[("readonly", BG_CARD)], selectforeground=[("readonly", FG_MAIN)])
        style.configure("TNotebook", background=BG_DEEP, borderwidth=0, tabmargins=[2, 4, 2, 0])
        style.configure("TNotebook.Tab", background=BG_PANEL, foreground=FG_DIM, padding=(18, 8), font=("DejaVu Sans", 10, "bold"))
        style.map(
            "TNotebook.Tab",
            background=[("selected", ACCENT)],
            foreground=[("selected", "#FFFFFF")],
            expand=[("selected", [0, 0, 0, 0])],
            padding=[("selected", (18, 8))],
        )
        style.configure("Horizontal.TProgressbar", troughcolor=BG_CARD, background=ACCENT_HI, bordercolor=BORDER_HI)
        style.configure("Treeview", background=BG_DEEP, fieldbackground=BG_DEEP, foreground=FG_MAIN, rowheight=24, bordercolor=BORDER_HI)
        style.configure("Treeview.Heading", background=BG_CARD, foreground="#FFFFFF", font=("DejaVu Sans", 9, "bold"), relief="flat")
        style.map("Treeview", background=[("selected", ACCENT)], foreground=[("selected", "#FFFFFF")])

    def _build_ui(self) -> None:
        for child in self.winfo_children():
            child.destroy()

        self.title(self.t("title"))

        # PU-20 Concentric-Rings Header Banner
        banner = tk.Canvas(self, height=58, bg=BG_DEEP, highlightthickness=0)
        banner.pack(fill="x", padx=0, pady=0)
        for r, col in [(170, "#0D1138"), (130, "#141A52"), (95, "#1E2772"), (62, "#2B3798"), (34, "#4050C8")]:
            banner.create_oval(220 - r, 29 - r, 220 + r, 29 + r, fill=col, outline="")

        # PlayStation symbols: Triangle (green), Circle (red), Cross (blue), Square (pink)
        # Triangle
        banner.create_polygon(28, 20, 19, 37, 37, 37, outline="#38D878", fill="", width=2.4)
        # Circle
        banner.create_oval(47, 20, 65, 38, outline="#FF5A6E", width=2.4)
        # Cross
        banner.create_line(75, 21, 91, 37, fill="#6A9CFF", width=2.6)
        banner.create_line(91, 21, 75, 37, fill="#6A9CFF", width=2.6)
        # Square
        banner.create_rectangle(102, 21, 118, 37, outline="#E874D0", width=2.4)

        # Title Box (like PU-20 header) + version on top-right
        banner.create_rectangle(140, 12, 336, 46, fill="#080B24", outline=BORDER_HI, width=2)
        banner.create_text(238, 29, text="PICOMANAGER", fill="#FFFFFF", font=("DejaVu Sans", 12, "bold"))
        ver_text_id = banner.create_text(944, 29, text="v1.0", anchor="e", fill=FG_DIM, font=("DejaVu Sans", 10, "bold"))
        banner.bind("<Configure>", lambda e, tid=ver_text_id: banner.coords(tid, e.width - 16, 29))

        # Top Storage & DB Bar
        bar = tk.Frame(self, bg=BG_PANEL, highlightbackground=BORDER_HI, highlightthickness=1)
        bar.pack(fill="x", padx=10, pady=(6, 6))
        inner = ttk.Frame(bar, style="Panel.TFrame", padding=10)
        inner.pack(fill="x")

        ttk.Label(inner, text=self.t("sd_label"), style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.drive_combo = ttk.Combobox(inner, textvariable=self.drive_var, state="readonly", width=48)
        self.drive_combo.grid(row=0, column=1, padx=6, sticky="ew")
        self.drive_combo.bind("<<ComboboxSelected>>", self._on_drive_selected)

        ttk.Button(inner, text=self.t("refresh"), width=3, command=self._refresh_drives).grid(row=0, column=2, padx=(0, 6))
        self.btn_browse_sd = ttk.Button(inner, text=self.t("browse_folder"), command=self._choose_sd_folder)
        self.btn_browse_sd.grid(row=0, column=3)
        ttk.Checkbutton(inner, text=self.t("show_fixed"), variable=self.show_fixed_var, command=self._on_toggle_fixed).grid(row=0, column=4, padx=(10, 0))

        ttk.Label(inner, text=self.t("db_label"), style="Panel.TLabel").grid(row=1, column=0, sticky="w", pady=(6, 0))
        ttk.Entry(inner, textvariable=self.db_var, width=48).grid(row=1, column=1, columnspan=2, padx=6, pady=(6, 0), sticky="ew")
        ttk.Button(inner, text=self.t("browse_folder"), command=self._choose_db).grid(row=1, column=3, pady=(6, 0))
        db_status = self.t("db_auto") if self.db_var.get() else ""
        ttk.Label(inner, text=db_status, style="Dim.TLabel").grid(row=1, column=4, sticky="w", padx=(10, 0), pady=(6, 0))
        inner.columnconfigure(1, weight=1)

        # Tabs: Capas | Configurações | PicoPlayer
        self.nb = ttk.Notebook(self)
        self.nb.pack(fill="both", expand=True, padx=10, pady=(0, 10))

        self.tab_covers   = ttk.Frame(self.nb, style="Panel.TFrame", padding=10)
        self.tab_settings = ttk.Frame(self.nb, style="Panel.TFrame", padding=12)
        self.tab_player   = ttk.Frame(self.nb, style="Panel.TFrame", padding=12)

        self.nb.add(self.tab_covers,   text=self.t("tab_covers"))
        self.nb.add(self.tab_settings, text=self.t("tab_settings"))
        self.nb.add(self.tab_player,   text=self.t("tab_player"))

        self._build_covers_tab()
        self._build_settings_tab()
        self._build_player_tab()

    # ------------------------------------------------------------------
    # Tab 1: Capas (Interactive game list + per-game disc/folder cover + batch)
    # ------------------------------------------------------------------
    def _build_covers_tab(self) -> None:
        f = self.tab_covers

        # Top batch controls
        top = ttk.Frame(f, style="Panel.TFrame")
        top.pack(fill="x", pady=(0, 8))

        self.cov_style_var = tk.StringVar(value="default")
        self.folder_cov_style_var = tk.StringVar(value="default")
        self.apply_folders_var = tk.BooleanVar(value=True)
        self.copy_cht_var = tk.BooleanVar(value=True)
        self.copy_lsd_var = tk.BooleanVar(value=True)
        self.overwrite_var = tk.BooleanVar(value=False)

        ttk.Label(top, text=self.t("cue_style"), style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Combobox(top, textvariable=self.cov_style_var, values=["default", "3d", "cd"], width=10, state="readonly").grid(row=0, column=1, padx=6)
        ttk.Label(top, text=self.t("folder_style"), style="Panel.TLabel").grid(row=0, column=2, sticky="w", padx=(10, 0))
        ttk.Combobox(top, textvariable=self.folder_cov_style_var, values=["default", "3d", "cd"], width=10, state="readonly").grid(row=0, column=3, padx=6)

        ttk.Checkbutton(top, text=self.t("apply_folders"), variable=self.apply_folders_var).grid(row=0, column=4, padx=(10, 0))
        ttk.Checkbutton(top, text=self.t("copy_cht"), variable=self.copy_cht_var).grid(row=1, column=0, columnspan=2, sticky="w", pady=(6, 0))
        ttk.Checkbutton(top, text=self.t("copy_lsd"), variable=self.copy_lsd_var).grid(row=1, column=2, columnspan=2, sticky="w", pady=(6, 0), padx=(10, 0))
        ttk.Checkbutton(top, text=self.t("overwrite"), variable=self.overwrite_var).grid(row=1, column=4, sticky="w", pady=(6, 0), padx=(10, 0))

        self.btn_scan = ttk.Button(top, text=self.t("btn_autoscan"), command=self._run_sd_scan)
        self.btn_scan.grid(row=0, column=5, rowspan=2, padx=(12, 4), sticky="nsew")
        ttk.Button(top, text=self.t("btn_rescan"), command=self._reload_game_list).grid(row=0, column=6, rowspan=2, sticky="nsew")
        top.columnconfigure(4, weight=1)

        # Main split: Left = Game Treeview, Right = Selected Game Cover Inspector
        body = ttk.Frame(f, style="Panel.TFrame")
        body.pack(fill="both", expand=True)

        left = ttk.Frame(body, style="Panel.TFrame")
        left.pack(side="left", fill="both", expand=True, padx=(0, 10))

        cols = ("game", "id", "cov", "cht", "lsd")
        self.tree = ttk.Treeview(left, columns=cols, show="headings", selectmode="browse")
        self.tree.heading("game", text=self.t("col_game"))
        self.tree.heading("id",   text=self.t("col_id"))
        self.tree.heading("cov",  text=self.t("col_cov"))
        self.tree.heading("cht",  text=self.t("col_cht"))
        self.tree.heading("lsd",  text=self.t("col_lsd"))
        self.tree.column("game", width=360, anchor="w")
        self.tree.column("id",   width=95,  anchor="center")
        self.tree.column("cov",  width=46,  anchor="center")
        self.tree.column("cht",  width=42,  anchor="center")
        self.tree.column("lsd",  width=42,  anchor="center")
        self.tree.bind("<<TreeviewSelect>>", self._on_game_select)

        sb = ttk.Scrollbar(left, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")

        # Right inspector card (dual 128x128 preview: Cover + Disc)
        right = tk.Frame(body, bg=BG_CARD, highlightbackground=BORDER_HI, highlightthickness=1, width=292)
        right.pack(side="right", fill="y")
        right.pack_propagate(False)

        self.sel_title_lbl = tk.Label(right, text="—", bg=BG_CARD, fg=FG_MAIN, wraplength=272, font=("DejaVu Sans", 9, "bold"))
        self.sel_title_lbl.pack(pady=(10, 2), padx=8)
        self.sel_id_lbl = tk.Label(right, text="", bg=BG_CARD, fg=FG_DIM, font=("DejaVu Sans", 9))
        self.sel_id_lbl.pack(pady=(0, 6))

        prev_row = tk.Frame(right, bg=BG_CARD)
        prev_row.pack(pady=4, padx=8)

        col_l = tk.Frame(prev_row, bg=BG_CARD)
        col_l.pack(side="left", padx=(0, 6))
        tk.Label(col_l, text="Capa" if self.app_lang == "pt" else "Cover", bg=BG_CARD, fg=FG_DIM, font=("DejaVu Sans", 8, "bold")).pack(pady=(0, 3))
        self.cov_folder_canvas = tk.Canvas(col_l, width=128, height=128, bg=BG_DEEP, highlightbackground=BORDER_HI, highlightthickness=2)
        self.cov_folder_canvas.pack()
        self.cov_folder_canvas.create_text(64, 64, text=self.t("no_cover"), fill=FG_DIM, justify="center", font=("DejaVu Sans", 8))

        col_r = tk.Frame(prev_row, bg=BG_CARD)
        col_r.pack(side="left", padx=(6, 0))
        tk.Label(col_r, text="Disco" if self.app_lang == "pt" else "Disc", bg=BG_CARD, fg=FG_DIM, font=("DejaVu Sans", 8, "bold")).pack(pady=(0, 3))
        self.cov_disc_canvas = tk.Canvas(col_r, width=128, height=128, bg=BG_DEEP, highlightbackground=BORDER_HI, highlightthickness=2)
        self.cov_disc_canvas.pack()
        self.cov_disc_canvas.create_text(64, 64, text=self.t("no_cover"), fill=FG_DIM, justify="center", font=("DejaVu Sans", 8))

        ttk.Button(right, text=self.t("btn_set_folder"), command=lambda: self._assign_custom_cover(True)).pack(fill="x", padx=14, pady=(14, 6))
        ttk.Button(right, text=self.t("btn_set_disc"), command=lambda: self._assign_custom_cover(False)).pack(fill="x", padx=14, pady=(0, 8))

        self.games_prog = ttk.Progressbar(f, mode="determinate")
        self.games_prog.pack(fill="x", pady=(8, 4))
        self.games_status = ttk.Label(f, text="", style="Dim.TLabel")
        self.games_status.pack(anchor="w")

    # ------------------------------------------------------------------
    # Tab 2: Configurações (config.ini + PU-20 preserved background + App Lang)
    # ------------------------------------------------------------------
    def _build_settings_tab(self) -> None:
        f = self.tab_settings
        left = ttk.Frame(f, style="Panel.TFrame")
        left.pack(side="left", fill="both", expand=True, padx=(0, 14))
        right = ttk.Frame(f, style="Panel.TFrame")
        right.pack(side="right", fill="y")

        ttk.Label(left, text=self.t("cfg_bg_note"), style="Dim.TLabel", wraplength=480).grid(row=0, column=0, columnspan=3, sticky="w", pady=(0, 10))

        # Only the 4 colors that customize the RC5 list/border/cursor/text over the PU-20 rings!
        self.color_vars: Dict[str, tk.StringVar] = {}
        self.color_swatches: Dict[str, tk.Button] = {}
        color_rows = [
            ("filelist", self.t("lbl_filelist")),
            ("frame",    self.t("lbl_frame")),
            ("cursor",   self.t("lbl_cursor")),
            ("text",     self.t("lbl_text")),
        ]
        for idx, (key, label) in enumerate(color_rows, start=1):
            ttk.Label(left, text=label, style="Panel.TLabel").grid(row=idx, column=0, sticky="w", pady=4)
            var = tk.StringVar(value=getattr(self.cfg, key))
            self.color_vars[key] = var
            ttk.Entry(left, textvariable=var, width=10).grid(row=idx, column=1, padx=6, sticky="w")
            sw = tk.Button(left, text="   ", width=4, relief="flat", command=lambda k=key: self._pick_color(k))
            sw.grid(row=idx, column=2, padx=4, sticky="w")
            self.color_swatches[key] = sw

        presets = ttk.Frame(left, style="Panel.TFrame")
        presets.grid(row=5, column=0, columnspan=3, sticky="w", pady=(10, 10))
        ttk.Button(presets, text=self.t("preset_pu20"), command=lambda: self._apply_preset(PU20_PRESET)).pack(side="left")
        ttk.Button(presets, text=self.t("preset_rc5"), command=lambda: self._apply_preset(RC5_CLASSIC_PRESET)).pack(side="left", padx=8)

        self.lang_var = tk.StringVar()
        ttk.Label(left, text=self.t("lbl_menu_lang"), style="Panel.TLabel").grid(row=6, column=0, sticky="w", pady=4)
        ttk.Combobox(left, textvariable=self.lang_var, values=[n for _, n in LANGUAGES], width=18, state="readonly").grid(row=6, column=1, columnspan=2, sticky="w", padx=6)

        self.app_lang_var = tk.StringVar(value="Português (BR)" if self.app_lang == "pt" else "English")
        ttk.Label(left, text=self.t("lbl_app_lang"), style="Panel.TLabel").grid(row=7, column=0, sticky="w", pady=4)
        cb_app = ttk.Combobox(left, textvariable=self.app_lang_var, values=["Português (BR)", "English"], width=18, state="readonly")
        cb_app.grid(row=7, column=1, columnspan=2, sticky="w", padx=6)
        cb_app.bind("<<ComboboxSelected>>", self._on_app_lang_change)

        self.style_var = tk.StringVar()
        ttk.Label(left, text=self.t("lbl_style"), style="Panel.TLabel").grid(row=8, column=0, sticky="w", pady=4)
        ttk.Combobox(left, textvariable=self.style_var, values=[self.t("style_0"), self.t("style_1")], width=24, state="readonly").grid(row=8, column=1, columnspan=2, sticky="w", padx=6)

        self.open_btn_var = tk.StringVar()
        self.back_btn_var = tk.StringVar()
        ttk.Label(left, text=self.t("lbl_open"), style="Panel.TLabel").grid(row=9, column=0, sticky="w", pady=4)
        ttk.Combobox(left, textvariable=self.open_btn_var, values=[n for _, n in BUTTON_OPTIONS], width=16, state="readonly").grid(row=9, column=1, columnspan=2, sticky="w", padx=6)
        ttk.Label(left, text=self.t("lbl_back"), style="Panel.TLabel").grid(row=10, column=0, sticky="w", pady=4)
        ttk.Combobox(left, textvariable=self.back_btn_var, values=[n for _, n in BUTTON_OPTIONS], width=16, state="readonly").grid(row=10, column=1, columnspan=2, sticky="w", padx=6)

        self.wp_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(left, text=self.t("chk_wp"), variable=self.wp_var).grid(row=11, column=0, columnspan=3, sticky="w", pady=(8, 4))

        act = ttk.Frame(left, style="Panel.TFrame")
        act.grid(row=12, column=0, columnspan=3, sticky="w", pady=(10, 0))
        ttk.Button(act, text=self.t("btn_save_cfg"), command=self._save_cfg).pack(side="left")
        ttk.Button(act, text=self.t("btn_make_wp"), command=self._convert_wallpaper).pack(side="left", padx=8)

        ttk.Label(right, text=self.t("preview_title"), style="Panel.TLabel").pack(anchor="w", pady=(0, 6))
        self.preview = tk.Canvas(right, width=320, height=240, bg="#080828", highlightthickness=2, highlightbackground=BORDER_HI)
        self.preview.pack()

    # ------------------------------------------------------------------
    # Tab 3: PicoPlayer (Audio Encoder + Free Online ID3)
    # ------------------------------------------------------------------
    def _build_player_tab(self) -> None:
        f = self.tab_player
        top = ttk.Frame(f, style="Panel.TFrame")
        top.pack(fill="x", pady=(0, 8))

        self.audio_profile_var = tk.StringVar(value="VBR V0 (~245 kbps, LAME -V 0 -q 0 — Recomendado)")
        self.auto_id3_var = tk.BooleanVar(value=True)
        self.audio_src_var = tk.StringVar(value=self.t("audio_src_none"))
        self.pending_audio_files: List[Path] = []

        ttk.Label(top, text=self.t("audio_qual"), style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Combobox(
            top,
            textvariable=self.audio_profile_var,
            values=[
                "VBR V0 (~245 kbps, LAME -V 0 -q 0 — Recomendado)",
                "320 kbps CBR (LAME -b 320 -q 0)",
                "WAV 16-bit 44.1 kHz (Sem Perdas / CD)",
            ],
            width=48,
            state="readonly",
        ).grid(row=0, column=1, padx=8, sticky="w")

        ttk.Checkbutton(top, text=self.t("chk_id3"), variable=self.auto_id3_var).grid(row=1, column=0, columnspan=2, sticky="w", pady=(8, 0))

        # Source selection row (shows selected file/folder path clearly before converting)
        src_row = ttk.Frame(f, style="Panel.TFrame")
        src_row.pack(fill="x", pady=(2, 6))
        ttk.Label(src_row, text=self.t("audio_src"), style="Panel.TLabel").pack(side="left")
        ttk.Entry(src_row, textvariable=self.audio_src_var, state="readonly").pack(side="left", fill="x", expand=True, padx=(8, 0))

        btns = ttk.Frame(f, style="Panel.TFrame")
        btns.pack(fill="x", pady=(2, 8))
        self.btn_conv_file = ttk.Button(btns, text=self.t("btn_pick_one"), command=self._select_single_audio)
        self.btn_conv_file.pack(side="left")
        self.btn_conv_dir = ttk.Button(btns, text=self.t("btn_pick_dir"), command=self._select_folder_audio)
        self.btn_conv_dir.pack(side="left", padx=8)
        self.btn_build_toc = ttk.Button(btns, text=self.t("btn_build_toc"), command=self._rebuild_picoplayer_toc)
        self.btn_build_toc.pack(side="left", padx=4)
        self.btn_start_conv = ttk.Button(btns, text=self.t("btn_start_conv"), command=self._start_selected_audio, state="disabled")
        self.btn_start_conv.pack(side="right")

        self.audio_prog = ttk.Progressbar(f, mode="determinate")
        self.audio_prog.pack(fill="x", pady=(0, 8))

        self.audio_log = tk.Text(f, bg=BG_DEEP, fg=FG_MAIN, insertbackground=FG_MAIN, relief="flat", height=15, font=("DejaVu Sans Mono", 9), highlightbackground=BORDER_HI, highlightthickness=1)
        self.audio_log.pack(fill="both", expand=True)

    # ------------------------------------------------------------------
    # Drive Selection & DB
    # ------------------------------------------------------------------
    def _refresh_drives(self) -> None:
        include_fixed = self.show_fixed_var.get()
        drives = list_storage_drives(include_fixed=include_fixed)
        self.drive_map = {lbl: path for path, lbl in drives}
        labels = [lbl for _, lbl in drives]
        self.drive_combo["values"] = labels
        self.btn_browse_sd.configure(state="normal" if include_fixed else "disabled")

        if self.sd_path and str(self.sd_path) in self.drive_map.values():
            for lbl, p in self.drive_map.items():
                if p == str(self.sd_path):
                    self.drive_var.set(lbl)
                    break
        elif labels:
            self.drive_var.set(labels[0])
            self._set_sd_path(Path(self.drive_map[labels[0]]))
        else:
            self.drive_var.set("(Nenhum drive removível)" if self.app_lang == "pt" else "(No removable drive)")
            self.sd_path = None

    def _on_toggle_fixed(self) -> None:
        self._refresh_drives()

    def _on_drive_selected(self, _evt=None) -> None:
        lbl = self.drive_var.get()
        if lbl in self.drive_map:
            self._set_sd_path(Path(self.drive_map[lbl]))

    def _choose_sd_folder(self) -> None:
        d = filedialog.askdirectory(title="Selecione a pasta do SD / destino")
        if d:
            p = Path(d)
            lbl = f"[Pasta] {p}"
            self.drive_map[lbl] = d
            vals = list(self.drive_combo["values"])
            if lbl not in vals:
                vals.append(lbl)
                self.drive_combo["values"] = vals
            self.drive_var.set(lbl)
            self._set_sd_path(p)

    def _set_sd_path(self, p: Path) -> None:
        self.sd_path = p
        self.cfg = load_config_ini(p)
        self._refresh_config_ui()
        self._reload_game_list()

    def _choose_db(self) -> None:
        d = filedialog.askdirectory(title="Selecione a pasta com covers/, cht/ e lsd/")
        if d:
            self.db_var.set(d)

    def _require_sd(self) -> Optional[Path]:
        if not self.sd_path or not self.sd_path.is_dir():
            messagebox.showwarning("SD Card", self.t("warn_sd"))
            return None
        return self.sd_path

    # ------------------------------------------------------------------
    # Covers Tab Actions
    # ------------------------------------------------------------------
    def _reload_game_list(self) -> None:
        if not self.sd_path or not self.sd_path.is_dir():
            return
        self.games_status.configure(text="Escaneando jogos..." if self.app_lang == "pt" else "Scanning games...")

        def worker() -> None:
            items = list_sd_games(self.sd_path)
            def _fill() -> None:
                self.games = items
                self.tree.delete(*self.tree.get_children())
                for i, g in enumerate(items):
                    cov_mark = "✓" if (g.has_cue_cov or g.has_folder_cov) else "—"
                    self.tree.insert("", "end", iid=str(i), values=(
                        g.rel_path, g.game_id, cov_mark,
                        "✓" if g.has_cht else "—",
                        "✓" if g.has_lsd else "—",
                    ))
                self.games_status.configure(text=f"{len(items)} jogos encontrados.")
            self.after(0, _fill)

        threading.Thread(target=worker, daemon=True).start()

    def _on_game_select(self, _evt=None) -> None:
        sel = self.tree.selection()
        if not sel:
            return
        idx = int(sel[0])
        if idx < 0 or idx >= len(self.games):
            return
        g = self.games[idx]
        self.selected_game = g
        self.sel_title_lbl.configure(text=g.cue_path.stem)
        self.sel_id_lbl.configure(text=g.game_id)
        self._update_cover_preview(g)

    def _update_cover_preview(self, g: GameEntry) -> None:
        # 1) Folder Cover preview (<Folder>.cov)
        self.cov_folder_canvas.delete("all")
        folder_cov = (g.cue_path.parent.parent / f"{g.folder_name}.cov") if g.folder_name else g.cue_path.with_suffix(".cov")
        ppm_f = decode_cov_to_ppm(folder_cov)
        if ppm_f is not None:
            self._cov_photo_folder = tk.PhotoImage(data=ppm_f)
            self.cov_folder_canvas.create_image(0, 0, anchor="nw", image=self._cov_photo_folder)
        else:
            self._cov_photo_folder = None
            self.cov_folder_canvas.create_text(64, 64, text=self.t("no_cover"), fill=FG_DIM, justify="center", font=("DejaVu Sans", 8))

        # 2) Disc Cover preview (<Game>.cov)
        self.cov_disc_canvas.delete("all")
        disc_cov = g.cue_path.with_suffix(".cov")
        ppm_d = decode_cov_to_ppm(disc_cov)
        if ppm_d is not None:
            self._cov_photo_disc = tk.PhotoImage(data=ppm_d)
            self.cov_disc_canvas.create_image(0, 0, anchor="nw", image=self._cov_photo_disc)
        else:
            self._cov_photo_disc = None
            self.cov_disc_canvas.create_text(64, 64, text=self.t("no_cover"), fill=FG_DIM, justify="center", font=("DejaVu Sans", 8))

    def _assign_custom_cover(self, for_folder: bool) -> None:
        sd = self._require_sd()
        if not sd:
            return
        if not self.selected_game:
            messagebox.showinfo("Capas", self.t("warn_sel"))
            return
        src = filedialog.askopenfilename(title="Escolha uma imagem (PNG/JPG)", filetypes=[("Imagens", "*.png *.jpg *.jpeg *.bmp *.webp")])
        if not src:
            return
        g = self.selected_game
        if for_folder:
            if g.cue_path.parent == sd:
                dst = g.cue_path.with_suffix(".cov")
            else:
                dst = g.cue_path.parent.parent / f"{g.cue_path.parent.name}.cov"
        else:
            dst = g.cue_path.with_suffix(".cov")
        try:
            convert_image_to_cov(Path(src), dst)
            if for_folder:
                g.has_folder_cov = True
            else:
                g.has_cue_cov = True
            self._update_cover_preview(g)
            self._reload_game_list()
            messagebox.showinfo("OK", f"{self.t('saved_ok')}\n{dst.name}")
        except Exception as e:
            messagebox.showerror("Erro", str(e))

    def _run_sd_scan(self) -> None:
        if self.busy:
            return
        sd = self._require_sd()
        if not sd:
            return
        db_s = self.db_var.get().strip()
        if not db_s or not (Path(db_s) / "covers").is_dir():
            messagebox.showwarning("Banco", self.t("warn_db"))
            return

        self.busy = True
        self.btn_scan.configure(state="disabled")
        self.games_prog["value"] = 0

        def worker() -> None:
            try:
                stats = scan_and_populate_sd(
                    sd_root=sd,
                    db_root=Path(db_s),
                    cover_style=self.cov_style_var.get(),
                    folder_cover_style=self.folder_cov_style_var.get(),
                    apply_folder_covers=self.apply_folders_var.get(),
                    copy_cheats=self.copy_cht_var.get(),
                    copy_lsd=self.copy_lsd_var.get(),
                    overwrite=self.overwrite_var.get(),
                    log=lambda m: self.after(0, lambda msg=m: self.games_status.configure(text=msg)),
                    progress=lambda cur, tot: self.after(0, lambda: self.games_prog.configure(maximum=tot, value=cur)),
                )
                msg = (
                    f"Concluído! CUEs: {stats.cues_found} | Capas: {stats.covers_copied} | "
                    f"Pastas: {stats.folder_covers_copied} | CHT: {stats.cheats_copied} | LSD: {stats.lsds_copied}"
                )
                self.after(0, lambda: self.games_status.configure(text=msg))
                self.after(0, self._reload_game_list)
            finally:
                self.busy = False
                self.after(0, lambda: self.btn_scan.configure(state="normal"))

        threading.Thread(target=worker, daemon=True).start()

    # ------------------------------------------------------------------
    # Settings Tab Actions
    # ------------------------------------------------------------------
    def _on_app_lang_change(self, _evt=None) -> None:
        new_lang = "en" if self.app_lang_var.get() == "English" else "pt"
        if new_lang != self.app_lang:
            self.app_lang = new_lang
            cur_tab = self.nb.index("current")
            self._build_ui()
            self._refresh_drives()
            self._refresh_config_ui()
            self.nb.select(cur_tab)

    def _pick_color(self, key: str) -> None:
        cur = "#" + self.color_vars[key].get()
        _, hx = colorchooser.askcolor(color=cur, title=f"Color ({key})")
        if hx:
            self.color_vars[key].set(hx.lstrip("#").upper())
            self._draw_preview()

    def _apply_preset(self, preset: dict) -> None:
        for k in ("filelist", "frame", "cursor", "text"):
            if k in preset:
                self.color_vars[k].set(preset[k])
        self._draw_preview()

    def _refresh_config_ui(self) -> None:
        for k, var in self.color_vars.items():
            var.set(getattr(self.cfg, k))
        self.lang_var.set(next((n for i, n in LANGUAGES if i == self.cfg.lang), LANGUAGES[0][1]))
        self.style_var.set(self.t("style_1") if self.cfg.style == 1 else self.t("style_0"))
        self.open_btn_var.set(next((n for i, n in BUTTON_OPTIONS if i == self.cfg.actopen), BUTTON_OPTIONS[0][1]))
        self.back_btn_var.set(next((n for i, n in BUTTON_OPTIONS if i == self.cfg.actback), BUTTON_OPTIONS[1][1]))
        self.wp_var.set(bool(self.cfg.wallpaper))
        self._draw_preview()

    def _draw_preview(self) -> None:
        c = self.preview
        c.delete("all")
        cols: Dict[str, str] = {}
        for k, var in self.color_vars.items():
            hx = var.get().strip().lstrip("#").upper()
            if len(hx) != 6 or not all(ch in "0123456789ABCDEF" for ch in hx):
                hx = "FFFFFF"
            cols[k] = "#" + hx
            self.color_swatches[k].configure(bg="#" + hx)

        # Fixed PU-20 concentric-rings background!
        c.configure(bg="#060820")
        for r, shade in [(150, "#0C103A"), (120, "#151B58"), (90, "#202A7A"), (60, "#2F3DA6"), (32, "#4656D4"), (14, "#6878FF")]:
            c.create_oval(160 - r, 120 - r, 160 + r, 120 + r, fill=shade, outline="")

        c.create_rectangle(9, 15, 181, 210, fill=cols["filelist"], outline=cols["frame"], width=2)
        c.create_rectangle(10, 29, 180, 42, fill=cols["cursor"], outline="")
        for i, name in enumerate(["Resident Evil 2", "Resident Evil 3", "Tomb Raider", "PaRappa the Rapper"]):
            c.create_text(17, 23 + i * 14, text=name, anchor="w", fill="#040410", font=("DejaVu Sans", 8))
            c.create_text(16, 22 + i * 14, text=name, anchor="w", fill=cols["text"], font=("DejaVu Sans", 8))
        c.create_rectangle(192, 78, 310, 196, fill="#111538", outline=cols["frame"], width=1)
        c.create_text(251, 137, text="CAPA\n128×128", fill=FG_DIM, font=("DejaVu Sans", 9), justify="center")
        c.create_text(14, 224, text="× Abrir   SELECT Créditos", anchor="w", fill=cols["text"], font=("DejaVu Sans", 8))
        c.create_text(312, 224, text="□ PicoPlayer", anchor="e", fill=cols["text"], font=("DejaVu Sans", 8, "bold"))

    def _save_cfg(self) -> None:
        sd = self._require_sd()
        if not sd:
            return
        self.cfg.background = "080828"  # PU-20 dark fallback (rings are always drawn unless wallpaper=1)
        for k, var in self.color_vars.items():
            setattr(self.cfg, k, var.get().strip().lstrip("#").upper())
        self.cfg.lang = next((i for i, n in LANGUAGES if n == self.lang_var.get()), 8)
        self.cfg.style = 1 if self.style_var.get().startswith("1") else 0
        self.cfg.actopen = next((i for i, n in BUTTON_OPTIONS if n == self.open_btn_var.get()), 14)
        self.cfg.actback = next((i for i, n in BUTTON_OPTIONS if n == self.back_btn_var.get()), 13)
        self.cfg.wallpaper = 1 if self.wp_var.get() else 0
        try:
            p = save_config_ini(sd, self.cfg)
            self._draw_preview()
            messagebox.showinfo("OK", f"{self.t('saved_ok')}\n{p}")
        except Exception as e:
            messagebox.showerror("Erro", str(e))

    def _convert_wallpaper(self) -> None:
        sd = self._require_sd()
        if not sd:
            return
        src = filedialog.askopenfilename(title="Wallpaper (PNG/JPG)", filetypes=[("Imagens", "*.png *.jpg *.jpeg *.bmp *.webp")])
        if not src:
            return
        try:
            dst = sd / "background.raw"
            convert_image_to_wallpaper(Path(src), dst)
            self.wp_var.set(True)
            messagebox.showinfo("OK", f"{self.t('saved_ok')}\n{dst}")
        except Exception as e:
            messagebox.showerror("Erro", str(e))

    # ------------------------------------------------------------------
    # PicoPlayer Tab Actions
    # ------------------------------------------------------------------
    def _log_audio(self, msg: str) -> None:
        def _append() -> None:
            self.audio_log.insert("end", msg + "\n")
            self.audio_log.see("end")
        self.after(0, _append)

    def _selected_audio_profile(self) -> str:
        s = self.audio_profile_var.get()
        if s.startswith("VBR"):
            return "mp3_v0"
        if s.startswith("WAV"):
            return "wav_cd"
        return "mp3_320"

    def _select_single_audio(self) -> None:
        src = filedialog.askopenfilename(
            title="Selecione 1 arquivo de áudio",
            filetypes=[("Áudio", "*.mp3 *.wav *.flac *.ogg *.m4a *.opus *.aac *.wma *.aiff")],
        )
        if not src:
            return
        p = Path(src)
        self.pending_audio_files = [p]
        self.audio_src_var.set(f"{p}  (1 faixa)")
        self.btn_start_conv.configure(state="normal")

    def _select_folder_audio(self) -> None:
        d = filedialog.askdirectory(title="Selecione a pasta de músicas (lote)")
        if not d:
            return
        files = sorted(
            p for p in Path(d).rglob("*")
            if p.is_file() and p.suffix.lower() in AUDIO_EXTS and not p.name.startswith(".")
        )
        if not files:
            self.pending_audio_files = []
            self.audio_src_var.set(self.t("audio_src_none"))
            self.btn_start_conv.configure(state="disabled")
            messagebox.showinfo("PicoPlayer", "Nenhum arquivo de áudio encontrado.")
            return
        self.pending_audio_files = files
        unit = "faixa" if len(files) == 1 else "faixas"
        self.audio_src_var.set(f"{d}  ({len(files)} {unit})")
        self.btn_start_conv.configure(state="normal")

    def _start_selected_audio(self) -> None:
        if not self.pending_audio_files:
            messagebox.showwarning("PicoPlayer", self.t("warn_audio_src"))
            return
        self._start_audio_batch(list(self.pending_audio_files))

    def _start_audio_batch(self, files: List[Path]) -> None:
        if self.busy:
            return
        sd = self._require_sd()
        if not sd:
            return

        self.busy = True
        self.btn_conv_file.configure(state="disabled")
        self.btn_conv_dir.configure(state="disabled")
        self.btn_build_toc.configure(state="disabled")
        self.btn_start_conv.configure(state="disabled")
        self.audio_log.delete("1.0", "end")
        self.audio_prog.configure(maximum=len(files), value=0)
        profile = self._selected_audio_profile()
        auto_id3 = self.auto_id3_var.get()

        def worker() -> None:
            ok_cnt = 0
            err_cnt = 0
            self._log_audio(f"Convertendo {len(files)} faixa(s) [{profile}] → {sd / 'PicoPlayer'}")
            for i, src in enumerate(files, 1):
                self._log_audio(f"[{i}/{len(files)}] {src.name}...")
                try:
                    dst = convert_audio_for_picoplayer(
                        src, sd, profile=profile, auto_tag_online=auto_id3,
                        log=self._log_audio,
                    )
                    mb = dst.stat().st_size / (1024 * 1024)
                    self._log_audio(f"  [OK] {dst.name} ({mb:.2f} MB)")
                    ok_cnt += 1
                except Exception as e:
                    self._log_audio(f"  [ERRO] {e}")
                    err_cnt += 1
                self.after(0, lambda v=i: self.audio_prog.configure(value=v))
            try:
                build_picoplayer_index(sd, log=self._log_audio)
            except Exception as e:
                self._log_audio(f"  [AVISO TOC] {e}")
            self._log_audio(f"\nConcluído! Convertidos: {ok_cnt} | Erros: {err_cnt}")
            self.busy = False
            self.after(0, lambda: (
                self.btn_conv_file.configure(state="normal"),
                self.btn_conv_dir.configure(state="normal"),
                self.btn_build_toc.configure(state="normal"),
                self.btn_start_conv.configure(state="normal" if self.pending_audio_files else "disabled"),
            ))

        threading.Thread(target=worker, daemon=True).start()

    def _rebuild_picoplayer_toc(self) -> None:
        if self.busy:
            return
        sd = self._require_sd()
        if not sd:
            return
        self.busy = True
        self.btn_conv_file.configure(state="disabled")
        self.btn_conv_dir.configure(state="disabled")
        self.btn_build_toc.configure(state="disabled")
        self.btn_start_conv.configure(state="disabled")
        self.audio_log.delete("1.0", "end")
        self._log_audio(f"Indexando {sd / 'PicoPlayer'} → index.pp...")

        def worker() -> None:
            try:
                _, count = build_picoplayer_index(sd, log=self._log_audio)
                self._log_audio(f"\nPronto! {count} faixa(s) no TOC rápido do PicoPlayer.")
            except Exception as e:
                self._log_audio(f"[ERRO] {e}")
            finally:
                self.busy = False
                self.after(0, lambda: (
                    self.btn_conv_file.configure(state="normal"),
                    self.btn_conv_dir.configure(state="normal"),
                    self.btn_build_toc.configure(state="normal"),
                    self.btn_start_conv.configure(state="normal" if self.pending_audio_files else "disabled"),
                ))

        threading.Thread(target=worker, daemon=True).start()


def main() -> None:
    app = PicoManagerApp()
    app.mainloop()


if __name__ == "__main__":
    main()
