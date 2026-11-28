#pragma once

#include <stdint.h>

// All UI strings (encoded in Windows-1252 / Latin-1 to match rc5FontSprites 0x21..0xFF)
typedef struct {
    const char *loading;
    const char *of;
    const char *emptyFolder;
    const char *noSdCard;
    const char *open;
    const char *back;
    const char *credits;
    const char *fastBoot;
    const char *regularBoot;
    const char *hugeThanks;
    const char *enterBootloader;
    const char *cheats;
    const char *overclock;
    const char *edcCorrection;
    const char *bootsectorFix;
    const char *cheatNotFound;
    const char *cheatHelpFmt;    // "%c Play %c %s \x96 ON/OFF\n\x97/\x98 ON/OFF all"
    const char *lidOpened;
    const char *mountFmt;        // "%s\nMount %s.cue\n"
    const char *updateToc;
    const char *checkCd;
    const char *cddaImage;
    const char *loadSystemCnf;
    const char *sendId;
    const char *rebootPs1;
    const char *loadAndExecute;
    const char *exitToBios;
} MenuStrings;

extern const MenuStrings *g_lang;

void rc5LangSelect(int langId);
