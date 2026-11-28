#include "rc5_lang.h"

// Note: strings use Latin-1 hex escapes so accented characters match rc5FontSprites exactly:
//   \xe0=à  \xe1=á  \xe2=â  \xe3=ã  \xe7=ç  \xe9=é  \xea=ê  \xed=í  \xf3=ó  \xf4=ô  \xf5=õ  \xfa=ú
//   \xd6=Ö  \xfc=ü  \x8c=SELECT  \x96=START  \x97=L2  \x98=R2

static const MenuStrings s_langEn = {
    .loading         = "Please Wait Loading...",
    .of              = "of",
    .emptyFolder     = "Empty Folder",
    .noSdCard        = "No SD Card",
    .open            = "Open",
    .back            = "Back",
    .credits         = "Credits",
    .fastBoot        = "Fast Boot",
    .regularBoot     = "Regular Boot",
    .hugeThanks      = "Huge thanks to",
    .enterBootloader = "Enter rp2040 to bootloader",
    .cheats          = "Cheats",
    .overclock       = "Overclock",
    .edcCorrection   = "EDC correction",
    .bootsectorFix   = "Bootsector fix",
    .cheatNotFound   = "Cheat file not found",
    .cheatHelpFmt    = "%c Play %c %s \x96 ON/OFF\n\x97/\x98 ON/OFF all",
    .lidOpened       = "Lid opened, close for continue",
    .mountFmt        = "%s\nMount %s.cue\n",
    .updateToc       = "Update TOC\n",
    .checkCd         = "Check CD\n",
    .cddaImage       = "CDDA image\n",
    .loadSystemCnf   = "load SYSTEM.CNF\n",
    .sendId          = "Send ID ",
    .rebootPs1       = "Reboot PS1",
    .loadAndExecute  = "Load and execute",
    .exitToBios      = "Exit to BIOS"
};

static const MenuStrings s_langDe = {
    .loading         = "Bitte Warten, wird geladen...",
    .of              = "von",
    .emptyFolder     = "Leerer Ordner",
    .noSdCard        = "Keine SD-Karte",
    .open            = "\xd6""ffnen",
    .back            = "Zur\xfc""ck",
    .credits         = "Credits",
    .fastBoot        = "Schnellstart",
    .regularBoot     = "Normaler Start",
    .hugeThanks      = "Vielen Dank an",
    .enterBootloader = "Starte RP2040 Bootloader",
    .cheats          = "Cheats",
    .overclock       = "Overclock",
    .edcCorrection   = "EDC correction",
    .bootsectorFix   = "Bootsector fix",
    .cheatNotFound   = "Cheat file not found",
    .cheatHelpFmt    = "%c Play %c %s \x96 ON/OFF\n\x97/\x98 ON/OFF all",
    .lidOpened       = "Lid opened, close for continue",
    .mountFmt        = "%s\nMount %s.cue\n",
    .updateToc       = "Update TOC\n",
    .checkCd         = "Check CD\n",
    .cddaImage       = "CDDA image\n",
    .loadSystemCnf   = "load SYSTEM.CNF\n",
    .sendId          = "Send ID ",
    .rebootPs1       = "Reboot PS1",
    .loadAndExecute  = "Load and execute",
    .exitToBios      = "Exit to BIOS"
};

static const MenuStrings s_langEs = {
    .loading         = "Por favor espere Cargando...",
    .of              = "de",
    .emptyFolder     = "Carpeta Vac\xed""a",
    .noSdCard        = "Sin Tarjeta SD",
    .open            = "Abrir",
    .back            = "Atr\xe1""s",
    .credits         = "Cr\xe9""ditos",
    .fastBoot        = "Arranque R\xe1""pido",
    .regularBoot     = "Arranque Normal",
    .hugeThanks      = "Muchas gracias",
    .enterBootloader = "Entrar a bootloader rp2040",
    .cheats          = "Trucos",
    .overclock       = "Overclock",
    .edcCorrection   = "Correcci\xf3""n EDC",
    .bootsectorFix   = "Correcci\xf3""n Bootsector",
    .cheatNotFound   = "Archivo de trucos no encontrado",
    .cheatHelpFmt    = "%c Jugar %c %s \x96 ON/OFF\n\x97/\x98 ON/OFF todo",
    .lidOpened       = "Tapa abierta, cierre para continuar",
    .mountFmt        = "%s\nMontando %s.cue\n",
    .updateToc       = "Actualizando TOC\n",
    .checkCd         = "Verificando CD\n",
    .cddaImage       = "Imagen CDDA\n",
    .loadSystemCnf   = "Leyendo SYSTEM.CNF\n",
    .sendId          = "Enviando ID ",
    .rebootPs1       = "Reiniciando PS1",
    .loadAndExecute  = "Cargando y ejecutando",
    .exitToBios      = "Saliendo a la BIOS"
};

static const MenuStrings s_langFr = {
    .loading         = "Chargement...",
    .of              = "sur",
    .emptyFolder     = "Dossier vide",
    .noSdCard        = "Aucune carte SD",
    .open            = "Ouvrir",
    .back            = "Retour",
    .credits         = "Cr\xe9""dits",
    .fastBoot        = "Demarrage rapide",
    .regularBoot     = "Demarrage normal",
    .hugeThanks      = "Merci a",
    .enterBootloader = "Entrer en bootloader",
    .cheats          = "Cheats",
    .overclock       = "Overclock",
    .edcCorrection   = "Correction EDC",
    .bootsectorFix   = "Correction Bootsector",
    .cheatNotFound   = "Fichier cheat introuvable",
    .cheatHelpFmt    = "%c Jouer %c %s \x96 ON/OFF\n\x97/\x98 ON/OFF tout",
    .lidOpened       = "Couvercle ouvert, fermez pour continuer",
    .mountFmt        = "%s\nMount %s.cue\n",
    .updateToc       = "Update TOC\n",
    .checkCd         = "Check CD\n",
    .cddaImage       = "CDDA image\n",
    .loadSystemCnf   = "load SYSTEM.CNF\n",
    .sendId          = "Send ID ",
    .rebootPs1       = "Reboot PS1",
    .loadAndExecute  = "Load and execute",
    .exitToBios      = "Exit to BIOS"
};

static const MenuStrings s_langIt = {
    .loading         = "Caricamento in corso...",
    .of              = "di",
    .emptyFolder     = "Cartella Vuota",
    .noSdCard        = "Nessuna scheda SD",
    .open            = "Apri",
    .back            = "Indietro",
    .credits         = "Crediti",
    .fastBoot        = "Avvio Veloce",
    .regularBoot     = "Avvio Normale",
    .hugeThanks      = "Un enorme grazie a",
    .enterBootloader = "Entra in bootloader rp2040",
    .cheats          = "Trucchi",
    .overclock       = "Overclock",
    .edcCorrection   = "Correzione EDC",
    .bootsectorFix   = "Correzione Bootsector",
    .cheatNotFound   = "File trucchi non trovato",
    .cheatHelpFmt    = "%c Gioca %c %s \x96 ON/OFF\n\x97/\x98 ON/OFF tutto",
    .lidOpened       = "Coperchio aperto, chiudi per continuare",
    .mountFmt        = "%s\nMount %s.cue\n",
    .updateToc       = "Update TOC\n",
    .checkCd         = "Check CD\n",
    .cddaImage       = "CDDA image\n",
    .loadSystemCnf   = "load SYSTEM.CNF\n",
    .sendId          = "Send ID ",
    .rebootPs1       = "Reboot PS1",
    .loadAndExecute  = "Load and execute",
    .exitToBios      = "Exit to BIOS"
};

// 100% translated PT-BR (lang=8 in official RC5 config.ini) — translates every string RC5 left in English!
static const MenuStrings s_langPtBr = {
    .loading         = "Aguarde, carregando...",
    .of              = "de",
    .emptyFolder     = "Pasta vazia",
    .noSdCard        = "Nenhum Cart\xe3""o SD",
    .open            = "Abrir",
    .back            = "Voltar",
    .credits         = "Cr\xe9""ditos",
    .fastBoot        = "Inicializa\xe7\xe3""o r\xe1""pida",
    .regularBoot     = "Inicializa\xe7\xe3""o normal",
    .hugeThanks      = "Agradecimentos",
    .enterBootloader = "Entrar no bootloader do rp2040",
    .cheats          = "C\xf3""digos de trapa\xe7""a",
    .overclock       = "Overclock",
    .edcCorrection   = "Corre\xe7\xe3""o EDC",
    .bootsectorFix   = "Corre\xe7\xe3""o Bootsector",
    .cheatNotFound   = "Sem arquivo de trapa\xe7""a",
    .cheatHelpFmt    = "%c Jogar %c %s \x96 Ligar/Desl.\n\x97/\x98 Ligar/Desl. tudo",
    .lidOpened       = "Tampa aberta, feche para continuar",
    .mountFmt        = "%s\nMontando %s.cue\n",
    .updateToc       = "Atualizando TOC\n",
    .checkCd         = "Verificando CD\n",
    .cddaImage       = "Imagem CDDA\n",
    .loadSystemCnf   = "Lendo SYSTEM.CNF\n",
    .sendId          = "Enviando ID ",
    .rebootPs1       = "Reiniciando PS1",
    .loadAndExecute  = "Carregando e executando",
    .exitToBios      = "Saindo para a BIOS"
};

const MenuStrings *g_lang = &s_langPtBr; // Default PT-BR (overridden by config.ini lang=0..10 if set)

void rc5LangSelect(int langId) {
    switch (langId) {
        case 0:  g_lang = &s_langEn;   break;
        case 1:  g_lang = &s_langDe;   break;
        case 2:  g_lang = &s_langEs;   break;
        case 3:  g_lang = &s_langFr;   break;
        case 5:  g_lang = &s_langIt;   break;
        case 8:  g_lang = &s_langPtBr; break; // Official RC5 Portuguese (BR) ID!
        default: g_lang = &s_langPtBr; break;
    }
}
