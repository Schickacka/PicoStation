#include "rc5_cheats.h"
#include "rc5_config.h"
#include "rc5_lang.h"
#include "rc5_proto.h"
#include "rc5_ui.h"
#include "controller.h"
#include "psxproject/cdrom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t rc5CheatEngineBlob[];

#define MAX_CHEATS      128
#define MAX_CHEAT_CODES 256
#define MAX_CHEAT_TEXT  (3 * 2048)
#define SFX_VOL         10922

typedef struct {
    uint32_t addr;
    uint16_t val;
    uint8_t  type;
    uint8_t  pad;
} CheatCodeEntry;

typedef struct {
    char    name[48];
    uint8_t firstCode;
    uint8_t numCodes;
    uint8_t enabled;
} CheatItem;

// Heap-allocated on first use so .psexe stays small (gives ~8 sectors margin before LBA 100!)
static CheatItem      *s_items   = NULL;
static CheatCodeEntry *s_codes   = NULL;
static char           *s_chtText = NULL;
static int             s_itemCount = 0;
static int             s_codeCount = 0;
static uint16_t        s_d5Trigger = 0;
static bool            s_engineInstalled = false;

static int             s_loadState = 0; // 0=send cmd, 1=read parts, 2=ready, 3=not found
static int             s_curPart = 0;
static int             s_selected = 0;
static uint8_t         s_holdCnt = 0;
static uint8_t         s_secBuf[2352] __attribute__((aligned(4)));

static bool ensureCheatBuffers(void) {
    if (!s_items)   s_items   = (CheatItem *)malloc(sizeof(CheatItem) * MAX_CHEATS);
    if (!s_codes)   s_codes   = (CheatCodeEntry *)malloc(sizeof(CheatCodeEntry) * MAX_CHEAT_CODES);
    if (!s_chtText) s_chtText = (char *)malloc(MAX_CHEAT_TEXT + 4);
    return (s_items != NULL && s_codes != NULL && s_chtText != NULL);
}

static uint32_t parseHexStr(const char *s, int maxDigits) {
    uint32_t v = 0;
    for (int i = 0; i < maxDigits && s[i]; i++) {
        char c = s[i];
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = (v << 4) | (uint32_t)d;
    }
    return v;
}

static void parseCheatFile(char *text) {
    s_itemCount = 0;
    s_codeCount = 0;
    s_d5Trigger = 0;

    char *p = text;
    while (*p) {
        char *eol = p;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;
        char saved = *eol;
        *eol = '\0';

        // Trim leading spaces
        char *line = p;
        while (*line == ' ' || *line == '\t') line++;

        if (line[0] && line[0] != '#' && line[0] != ';' && line[0] != '/' && line[0] != '"') {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
                line[--len] = '\0';
            }
            if (line[0] == '[' && len > 2 && line[len - 1] == ']') {
                if (s_itemCount >= MAX_CHEATS) break;
                line[len - 1] = '\0';
                const char *title = line + 1;
                if (*title == '*') {
                    title++;
                }
                CheatItem *it = &s_items[s_itemCount++];
                strncpy(it->name, title, sizeof(it->name) - 1);
                it->name[sizeof(it->name) - 1] = '\0';
                it->firstCode = (uint8_t)s_codeCount;
                it->numCodes  = 0;
                it->enabled   = 1; // Match official RC5: all cheats start enabled by default
            } else if (s_itemCount > 0 && s_codeCount < MAX_CHEAT_CODES && len >= 12) {
                uint32_t wAddr = parseHexStr(line, 8);
                const char *valStr = line + 8;
                while (*valStr == ' ' || *valStr == '\t') valStr++;
                uint16_t wVal = (uint16_t)parseHexStr(valStr, 4);

                uint8_t op = (uint8_t)(wAddr >> 24);
                uint32_t phys = (wAddr & 0x00FFFFFFu) | 0x80000000u;
                uint8_t ctype = 0xFF;
                switch (op) {
                    case 0x80: ctype = 7;  break; // 16-bit constant write
                    case 0x30: ctype = 5;  break; // 8-bit constant write
                    case 0x50: ctype = 6;  phys = wAddr & 0x00FFFFFFu; break; // Slide/repeater
                    case 0x10: ctype = 1;  break; // 16-bit increment
                    case 0x11: ctype = 2;  break; // 16-bit decrement
                    case 0x20: ctype = 3;  break; // 8-bit increment
                    case 0x21: ctype = 4;  break; // 8-bit decrement
                    case 0xC0: ctype = 8;  break;
                    case 0xC1: ctype = 9;  break;
                    case 0xC2: ctype = 10; break;
                    case 0xD0: ctype = 11; break; // 16-bit equal
                    case 0xD1: ctype = 12; break; // 16-bit not equal
                    case 0xD2: ctype = 13; break; // 16-bit less than
                    case 0xD3: ctype = 14; break; // 16-bit greater than
                    case 0xD4: ctype = 15; phys = wAddr; break;
                    case 0xD5: ctype = 16; s_d5Trigger = wVal; break;
                    case 0xD6: ctype = 17; phys = wAddr; break;
                    case 0xE0: ctype = 18; break; // 8-bit equal
                    case 0xE1: ctype = 19; break; // 8-bit not equal
                    case 0xE2: ctype = 20; break; // 8-bit less than
                    case 0xE3: ctype = 21; break; // 8-bit greater than
                    default: break;
                }
                if (ctype != 0xFF) {
                    CheatCodeEntry *ce = &s_codes[s_codeCount++];
                    ce->addr = phys;
                    ce->val  = wVal;
                    ce->type = ctype;
                    ce->pad  = 0;
                    s_items[s_itemCount - 1].numCodes++;
                }
            }
        }

        *eol = saved;
        p = eol;
        while (*p == '\n' || *p == '\r') p++;
    }
}

void rc5CheatsReset(void) {
    s_itemCount = 0;
    s_codeCount = 0;
    s_d5Trigger = 0;
    s_loadState = 0;
    s_curPart   = 0;
    s_selected  = 0;
    s_holdCnt   = 0;
}

bool rc5CheatsHasEnabled(void) {
    if (!s_items) return false;
    for (int i = 0; i < s_itemCount; i++) {
        if (s_items[i].enabled && s_items[i].numCodes > 0) return true;
    }
    return false;
}

// Exact RC5 cheat engine installer (FUN_8001697c + FUN_8001fb50)
void rc5CheatsInstallToRam(void) {
    if (!rc5CheatsHasEnabled() || s_engineInstalled) return;

    // Clear 0x8000C400..0x8000CFFF table
    uint8_t *tableBase = (uint8_t *)0x8000C400u;
    memset(tableBase, 0, 0xC00);
    uint8_t *wr = tableBase;

    for (int i = 0; i < s_itemCount; i++) {
        if (!s_items[i].enabled) continue;
        for (int j = 0; j < s_items[i].numCodes; j++) {
            const CheatCodeEntry *ce = &s_codes[s_items[i].firstCode + j];
            if (wr + 8 >= (uint8_t *)0x8000CFF0u) break;
            uint32_t addr = ce->addr;
            uint16_t val  = ce->val;
            uint8_t  type = ce->type;
            if (type == 9) val = (uint16_t)(val / 3);
            if ((type == 6 || type == 10) && (j + 1 < s_items[i].numCodes)) {
                const CheatCodeEntry *nextCe = &s_codes[s_items[i].firstCode + j + 1];
                if (nextCe->type != 5) addr |= 0x00010000u;
                memcpy(wr, &addr, 4);
                memcpy(wr + 4, &val, 2);
                wr[6] = type;
                wr += 8;
                memcpy(wr, &nextCe->addr, 4);
                memcpy(wr + 4, &nextCe->val, 2);
                wr[6] = 0x7F;
                wr += 8;
                j++;
            } else {
                memcpy(wr, &addr, 4);
                memcpy(wr + 4, &val, 2);
                wr[6] = type;
                wr += 8;
            }
        }
    }

    // Copy 924-byte RC5 GameShark hook blob to 0x8000C000 and chain into BIOS VBlank hook
    memcpy((void *)0x8000C000u, rc5CheatEngineBlob, 0x39C);
    uint32_t b4 = *(volatile uint32_t *)0x800000B4u;
    uint32_t tablePtr = (*(volatile uint32_t *)(0x80000000u | ((b4 & 0xFFFFu) + 4u))) & 0xFFFFu;
    volatile uint32_t *hookSlot = (volatile uint32_t *)(0x80000000u | (tablePtr + 0x5Cu));
    uint32_t origHook = *hookSlot;
    *(volatile uint32_t *)0x8000C3FCu = origHook;
    *(volatile uint16_t *)hookSlot    = 0xC000u;
    *(volatile uint32_t *)0x8000C3F8u = 0;
    *(volatile uint16_t *)0x8000C3F4u = s_d5Trigger;
    s_engineInstalled = true;
}

int rc5CheatsDrawAndUpdate(DMAChain *chain, const TextureInfo *font,
                           uint16_t buttons, uint16_t prevButtons,
                           uint16_t fileIndex, Sound *sfxClick, Sound *sfxSlide) {
    uint16_t pressed = (uint16_t)(~prevButtons & buttons);

    // Auto-repeat UP / DOWN
    if ((buttons & BUTTON_MASK_UP) && (prevButtons & BUTTON_MASK_UP)) {
        if (++s_holdCnt > 30) { pressed ^= BUTTON_MASK_UP; s_holdCnt = 25; }
    } else if ((buttons & BUTTON_MASK_DOWN) && (prevButtons & BUTTON_MASK_DOWN)) {
        if (++s_holdCnt > 30) { pressed ^= BUTTON_MASK_DOWN; s_holdCnt = 25; }
    } else {
        s_holdCnt = 0;
    }

    // Modal frame (exact RC5 geometry: 15,25 150x176 outer; 20,30 140x143 inner)
    rc5DrawBox(chain, 15, 25, 150, 176, true, g_cfg.bg, false);
    rc5DrawBox(chain, 20, 30, 140, 143, true, g_cfg.filelist, false);

    // Back button closes cheats modal
    if ((!(g_cfg.backMask & buttons)) && (g_cfg.backMask & prevButtons)) {
        sound_playOnChannel(sfxSlide, SFX_VOL, SFX_VOL, 1);
        picoSendCommand(PICO_CMD_NONE, 0);
        rc5CheatsReset();
        return 1;
    }

    if (s_loadState == 0) {
        rc5PrintString(chain, font, 30, 40, g_lang->loading, 158);
        if (!ensureCheatBuffers()) {
            s_loadState = 3;
            return 0;
        }
        picoSendCommand(PICO_CMD_GET_CHEAT, fileIndex);
        memset(s_chtText, 0, MAX_CHEAT_TEXT + 4);
        s_curPart = 0;
        s_loadState = 1;
        return 0;
    }

    if (s_loadState == 1) {
        rc5PrintString(chain, font, 30, 40, g_lang->loading, 158);
        memset(s_secBuf, 0, sizeof(s_secBuf));
        startCDROMRead(PICO_LBA_CHEAT_BASE + (uint32_t)s_curPart, s_secBuf, 1, 2340, true, true);
        if (!waitingForInt5) {
            picoSendCommand(PICO_CMD_NONE, 0);
            s_loadState = 3;
            return 0;
        }
        const uint8_t *hdr = s_secBuf + 12;
        if (memcmp(hdr, "CHT0", 4) == 0 || memcmp(hdr, "CHT1", 4) != 0) {
            picoSendCommand(PICO_CMD_NONE, 0);
            s_loadState = 3;
            return 0;
        }
        uint16_t bytesRead = (uint16_t)(hdr[4] | ((uint16_t)hdr[5] << 8));
        if (bytesRead > 2048) bytesRead = 2048;
        if (bytesRead > 0) {
            memcpy(s_chtText + s_curPart * 2048, hdr + 6, bytesRead);
        }
        s_curPart++;
        if (bytesRead < 2048 || s_curPart >= 3) {
            s_chtText[s_curPart * 2048] = '\0';
            picoSendCommand(PICO_CMD_NONE, 0);
            parseCheatFile(s_chtText);
            s_selected = 0;
            s_loadState = (s_itemCount > 0) ? 2 : 3;
        }
        return 0;
    }

    if (s_loadState == 3) {
        rc5PrintString(chain, font, 24, 40, g_lang->cheatNotFound, 158);
        char foot[64];
        snprintf(foot, sizeof(foot), "\t\t %c %s", (char)g_cfg.backGlyph, g_lang->back);
        rc5PrintString(chain, font, 20, 178, foot, 160);
        return 0;
    }

    // s_loadState == 2: Interactive cheat list
    if (pressed & BUTTON_MASK_UP) {
        s_selected = (s_selected > 0) ? (s_selected - 1) : (s_itemCount - 1);
        sound_playOnChannel(sfxClick, SFX_VOL, SFX_VOL, 0);
    } else if (pressed & BUTTON_MASK_DOWN) {
        s_selected = (s_selected + 1 < s_itemCount) ? (s_selected + 1) : 0;
        sound_playOnChannel(sfxClick, SFX_VOL, SFX_VOL, 0);
    } else if (pressed & BUTTON_MASK_START) {
        s_items[s_selected].enabled = !s_items[s_selected].enabled;
        sound_playOnChannel(sfxClick, SFX_VOL, SFX_VOL, 0);
    } else if (pressed & BUTTON_MASK_L2) {
        for (int i = 0; i < s_itemCount; i++) s_items[i].enabled = 1;
        sound_playOnChannel(sfxClick, SFX_VOL, SFX_VOL, 0);
    } else if (pressed & BUTTON_MASK_R2) {
        for (int i = 0; i < s_itemCount; i++) s_items[i].enabled = 0;
        sound_playOnChannel(sfxClick, SFX_VOL, SFX_VOL, 0);
    } else if (pressed & g_cfg.openMask) {
        sound_playOnChannel(sfxSlide, SFX_VOL, SFX_VOL, 1);
        return 2; // Launch game with cheats!
    }

    const int visible = 11;
    int start = 0;
    if (s_itemCount > visible) {
        start = s_selected - visible / 2;
        if (start < 0) start = 0;
        if (start > s_itemCount - visible) start = s_itemCount - visible;
    }
    int count = s_itemCount - start;
    if (count > visible) count = visible;

    for (int i = 0; i < count; i++) {
        int idx = start + i;
        int rowY = 30 + i * 13;
        if (idx == s_selected) {
            rc5DrawSolidRect(chain, 20, rowY, 140, 13, g_cfg.cursor);
        }
        char line[64];
        snprintf(line, sizeof(line), "%c %s",
                 s_items[idx].enabled ? (char)0x8A : (char)0x8B,
                 s_items[idx].name);
        rc5PrintString(chain, font, 22, rowY, line, 158);
    }

    char help[96];
    snprintf(help, sizeof(help), g_lang->cheatHelpFmt,
             (char)g_cfg.openGlyph, (char)g_cfg.backGlyph, g_lang->back);
    rc5PrintString(chain, font, 20, 175, help, 165);
    return 0;
}
