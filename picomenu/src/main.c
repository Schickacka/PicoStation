#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps1/cdrom.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"
#include "psxproject/cdrom.h"
#include "psxproject/delay.h"
#include "psxproject/filesystem.h"
#include "psxproject/irq.h"
#include "psxproject/spu.h"
#include "psxproject/system.h"

#include "controller.h"
#include "counters.h"
#include "file_manager.h"
#include "gpu.h"
#include "logging.h"

#include "rc5_boot.h"
#include "rc5_cheats.h"
#include "rc5_config.h"
#include "rc5_lang.h"
#include "rc5_proto.h"
#include "rc5_ui.h"
#include "picoplayer.h"
#include "pu20_skin.h"

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define LISTING_SIZE  2324
#define MAX_FILES     4096
#define SFX_VOL       10922

#define PICOSTATION_RELEASE_STR "Release 2.0"
#define PICOMENU_RELEASE_STR    "Release 2.0"

#define COVER_PART_BYTES 2114
#define COVER_PARTS      8

extern const uint8_t rc5FontTexture[], rc5FontPalette[];
extern const uint8_t rc5LogoTexture[], rc5LogoPalette[];
extern const uint8_t click_sfx[], slide_sfx[];

typedef enum {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_GOTO_ROOT = 1,
    MENU_ACTION_GOTO_PARENT = 2,
    MENU_ACTION_GOTO_DIRECTORY = 3,
    MENU_ACTION_MOUNT_FAST = 4,
    MENU_ACTION_MOUNT_REGULAR = 5,
    MENU_ACTION_BOOTLOADER = 6,
    MENU_ACTION_OPEN_OPTIONS = 7,
    MENU_ACTION_MOUNT_CHEATS = 8,
    MENU_ACTION_OPEN_PLAYER = 9
} MenuAction;

typedef enum {
    OVERLAY_NONE = 0,
    OVERLAY_OPTIONS,
    OVERLAY_CHEATS,
    OVERLAY_BOOTING
} OverlayMode;

// Heap-allocated large buffers (keeps .psexe < 100 KB and ISO well under LBA 100!)
static DMAChain *s_dmaChains = NULL;
static uint8_t  *s_coverRaw  = NULL;
static uint8_t   s_listSecBuf[2352] __attribute__((aligned(4)));
static uint8_t   s_coverSecBuf[2352] __attribute__((aligned(4)));

static TextureInfo s_fontTex;
static TextureInfo s_logoTex;
static TextureInfo s_coverTex;
static Sound       s_sfxClick;
static Sound       s_sfxSlide;

// Navigation & UI state
static uint16_t    s_dirStack[16];
static uint8_t     s_dirDepth = 0;
static bool        s_creditsOpen = false;
static OverlayMode s_overlay = OVERLAY_NONE;
static int         s_optSelected = 0;
static bool        s_playerOpen = false;
static uint16_t    s_playerPrev = 0;
static uint32_t    s_frame = 0;

// Smooth text scroll & async cover loader state (matching RC5 FUN_8001295c)
static uint8_t s_coverDelay = 10;
static uint8_t s_scrollDelay = 50;
static int     s_scrollOffset = 0;
static int8_t  s_scrollDir = 1;
static int     s_coverState = 0; // 0=idle, 1=read part, 2=wait part, 3=none/dead, 4=ready
static int     s_coverPart = 0;
static int     s_coverRetries = 0;
static uint8_t s_holdRepeat = 0;

static void resetSelectionEffects(void) {
    s_coverDelay   = 10;
    s_scrollDelay  = 50;
    s_scrollOffset = 0;
    s_scrollDir    = 1;
    s_coverState   = 0;
    s_coverPart    = 0;
    s_coverRetries = 0;
}

// Parse one sector of "LST1" directory listing (exact RC5 FUN_80012580 protocol)
static bool parseListingPage(uint16_t *itemCount, const uint8_t *payload) {
    uint16_t offset = 0;
    while (offset < LISTING_SIZE && *itemCount < MAX_FILES) {
        uint8_t len = payload[offset];
        if (len == 0) {
            // Terminator: [0, hasNext, countHi, countLo]; 0xFE,0xFE = hard end
            if (payload[offset + 2] == 0xFE && payload[offset + 3] == 0xFE) {
                return false;
            }
            return (payload[offset + 1] == 1) ||
                   (payload[offset + 2] == 0 && payload[offset + 3] == 0);
        }
        file_manager_init_file_data(*itemCount, payload[offset + 1],
                                    (char *)&payload[offset + 2], len);
        offset = (uint16_t)(offset + len + 2);
        (*itemCount)++;
    }
    return false;
}

// Exact RC5 directory loader (FUN_800127dc): waits for "LST1" header before accepting page.
// Sends each command ONCE and polls PICO_LBA_LISTING until Core 1 finishes SD I/O
// (never sends GET_NEXT_CONTENTS prematurely, which would abort an in-flight GOTO_DIRECTORY!).
static uint16_t loadDirectoryListing(uint8_t cmd, uint16_t arg) {
    uint16_t count = 0;
    bool hasNext = true;
    bool needSend = true;
    int missRetries = 0;
    int int5Errors = 0;

    while (hasNext && missRetries < 200) {
        if (needSend) {
            picoSendCommand(cmd, arg);
            needSend = false;
        }
        memset(s_listSecBuf, 0, sizeof(s_listSecBuf));
        startCDROMRead(PICO_LBA_LISTING, s_listSecBuf, 1, 2340, true, true);

        if (!waitingForInt5) {
            // INT5 (lead-out / read error): never happens on real PicoStation virtual disc;
            // bail quickly on emulators without ODE hardware so UI opens immediately.
            if (++int5Errors >= 2) {
                break;
            }
            continue;
        }
        int5Errors = 0;

        // RC5 sector layout: 12-byte CD subheader + [len=4, flags=1, 'L','S','T','1']
        const uint8_t *payload = s_listSecBuf + 12;
        if (memcmp(payload + 2, "LST1", 4) == 0) {
            missRetries = 0;
            hasNext = parseListingPage(&count, payload + 6);
            cmd = PICO_CMD_GET_NEXT_CONTENTS;
            arg = count;
            needSend = true;
        } else {
            missRetries++;
        }
    }

    if (count > 0) {
        file_manager_sort(count);
        file_manager_clean_list(&count);
    }
    picoSendCommand(PICO_CMD_NONE, 0);
    return count;
}

// Exact RC5 startup lid-closed check (FUN_80018698)
static void waitUntilLidClosed(bool isPal) {
    bool secondFrame = false;
    issueCDROMCommand(CDROM_CMD_NOP, NULL, 0); // 0x01 Getstat
    waitForINT3();

    for (int okFrames = 0; okFrames < 3; okFrames++) {
        int bufX = secondFrame ? SCREEN_WIDTH : 0;
        DMAChain *chain = &s_dmaChains[secondFrame];
        secondFrame = !secondFrame;

        GPU_GP1 = gp1_fbOffset(bufX, 0);
        chain->nextPacket = chain->data;
        uint32_t *ptr = allocatePacket(chain, 4);
        ptr[0] = gp0_texpage(0, true, false);
        ptr[1] = gp0_fbOffset1(bufX, 0);
        ptr[2] = gp0_fbOffset2(bufX + SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
        ptr[3] = gp0_fbOrigin(bufX, 0);

        if (isPal) {
            ptr = allocatePacket(chain, 3);
            ptr[0] = gp0_vramFill();
            ptr[1] = gp0_xy(bufX, 0);
            ptr[2] = gp0_xy(SCREEN_WIDTH, 256);
        }

        rc5DrawBox(chain, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, false, g_cfg.bg, false);
        rc5DrawTexture(chain, &s_logoTex, 96, 110);

        if (cdromStatus & 0x10u) {
            rc5PrintString(chain, &s_fontTex, 60, 170, g_lang->lidOpened, 310);
            okFrames = 0;
        } else if ((cdromStatus & 0x02u) == 0) {
            okFrames = 0;
        }

        *(chain->nextPacket) = gp0_endTag(0);
        waitForGP0Ready();
        waitForVblank();
        sendLinkedList(chain->data);

        issueCDROMCommand(CDROM_CMD_NOP, NULL, 0); // 0x01 Getstat
        waitForINT3();
    }
    updateCDROM_TOC();
}

#ifdef PICO_MOCK_UI
static void mockTtyPuts(const char *s) {
    while (*s) {
        register uint32_t a0 __asm__("a0") = (uint8_t)*s++;
        register uint32_t t1 __asm__("t1") = 0x3Cu; // BIOS A(3Ch) putchar -> DuckStation I/TTY
        register uint32_t t2 __asm__("t2") = 0xA0u;
        __asm__ volatile("addiu $sp, $sp, -16\n\tjalr %2\n\tnop\n\taddiu $sp, $sp, 16\n"
                         : "+r"(a0), "+r"(t1), "+r"(t2) :
                         : "v0", "v1", "a1", "a2", "a3", "t0", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "ra", "memory");
    }
}
#endif

// Non-blocking async 128x128 8bpp cover loader (matches RC5 FUN_8001295c + cdromBusy safety)
static void stepAsyncCoverLoader(uint16_t selectedIdx) {
    if (s_coverDelay > 0 || cdromBusy) return;

#ifdef PICO_MOCK_UI
    if (s_coverState == 0) {
        // Exercise the REAL async CD-ROM READ_N + cdromINT1 + PAUSE INT2 pipeline on sector 16
        memset(s_coverSecBuf, 0, 32);
        startCDROMRead(16, s_coverSecBuf, 1, 2340, true, false);
        s_coverState = 2;
        return;
    } else if (s_coverState == 2) {
        // Synthesize a distinct 128x128 8bpp test cover per item once async READ_N + PAUSE finishes
        uint16_t *clut = (uint16_t *)s_coverRaw;
        for (int i = 0; i < 256; i++) {
            uint16_t r = (uint16_t)(((i + selectedIdx * 23) & 0x1Fu));
            uint16_t g = (uint16_t)((((i >> 2) + selectedIdx * 7) & 0x1Fu));
            uint16_t b = (uint16_t)(((31 - (i & 0x1Fu)) & 0x1Fu));
            clut[i] = (uint16_t)(r | (g << 5) | (b << 10));
        }
        uint8_t *px = s_coverRaw + 512;
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                bool border = (x < 4 || x >= 124 || y < 4 || y >= 124);
                px[y * 128 + x] = border ? 255 : (uint8_t)((x + y + selectedIdx * 17) & 0xFF);
            }
        }
        uploadIndexedTexture(&s_coverTex, s_coverRaw + 512, s_coverRaw,
                             0, 256, 0, 384, 128, 128, GP0_COLOR_8BPP);
        s_coverState = 4;
        mockTtyPuts("MOCK_COVER_UPLOADED\n");
    }
    return;
#endif

    if (s_coverState == 0) {
        // Match official RC5 & firmware DirectoryListing::openCover():
        // BOTH folders (GameFolder.cov) and .cue files (Game.cov) support cover art!
        uint16_t rawIdx = file_manager_get_file_index(selectedIdx);
        picoSendCommand(PICO_CMD_GET_COVER, rawIdx);
        s_coverPart    = 0;
        s_coverRetries = 0;
        s_coverState   = 1;
    } else if (s_coverState == 1) {
        memset(s_coverSecBuf, 0, 32);
        startCDROMRead(PICO_LBA_COVER_BASE + (uint32_t)s_coverPart, s_coverSecBuf, 1, 2340, true, false);
        s_coverState = 2;
    } else if (s_coverState == 2) {
        if (!waitingForInt5) {
            picoSendCommand(PICO_CMD_NONE, 0);
            s_coverState = 3;
            return;
        }
        if (!waitingForInt1) {
            const uint8_t *hdr = s_coverSecBuf + 12;
            if (memcmp(hdr, "DEAD", 4) == 0) {
                picoSendCommand(PICO_CMD_NONE, 0);
                s_coverState = 3;
                return;
            }
            if (memcmp(hdr, "PART", 4) != 0) {
                // Core 1 openCover() may still be scanning FAT32 (>16 ms); retry before giving up
                if (++s_coverRetries < 20) {
                    s_coverState = 1;
                } else {
                    picoSendCommand(PICO_CMD_NONE, 0);
                    s_coverState = 3;
                }
                return;
            }
            uint16_t gotPart = (uint16_t)(hdr[4] | ((uint16_t)hdr[5] << 8));
            if (gotPart == (uint16_t)s_coverPart) {
                s_coverRetries = 0;
                memcpy(s_coverRaw + s_coverPart * COVER_PART_BYTES, hdr + 6, COVER_PART_BYTES);
                if (s_coverPart == COVER_PARTS - 1) {
                    // First 512 bytes = 8bpp CLUT, next 16384 bytes = 128x128 pixels
                    uploadIndexedTexture(&s_coverTex, s_coverRaw + 512, s_coverRaw,
                                         0, 256, 0, 384, 128, 128, GP0_COLOR_8BPP);
                    picoSendCommand(PICO_CMD_NONE, 0);
                    s_coverState = 4;
                } else {
                    s_coverPart++;
                    s_coverState = 1;
                }
            } else {
                if (++s_coverRetries < 20) {
                    s_coverState = 1;
                } else {
                    picoSendCommand(PICO_CMD_NONE, 0);
                    s_coverState = 3;
                }
            }
        }
    }
}

static void drawOptionsOverlay(DMAChain *chain, uint16_t buttons, uint16_t prevButtons,
                               MenuAction *outAction) {
    uint16_t pressed = (uint16_t)(~prevButtons & buttons);

    rc5DrawBox(chain, 15, 25, 150, 105, true, g_cfg.bg, false);
    rc5DrawBox(chain, 20, 30, 140, 78, true, g_cfg.filelist, false);

    // Back closes options modal
    if ((!(g_cfg.backMask & buttons)) && (g_cfg.backMask & prevButtons)) {
        sound_playOnChannel(&s_sfxSlide, SFX_VOL, SFX_VOL, 1);
        s_overlay = OVERLAY_NONE;
        s_optSelected = 0;
        return;
    }

    if (pressed & BUTTON_MASK_DOWN) {
        s_optSelected = (s_optSelected + 1) % 6;
        sound_playOnChannel(&s_sfxClick, SFX_VOL, SFX_VOL, 0);
    } else if (pressed & BUTTON_MASK_UP) {
        s_optSelected = (s_optSelected > 0) ? (s_optSelected - 1) : 5;
        sound_playOnChannel(&s_sfxClick, SFX_VOL, SFX_VOL, 0);
    } else if (pressed & g_cfg.openMask) {
        sound_playOnChannel(&s_sfxSlide, SFX_VOL, SFX_VOL, 1);
        switch (s_optSelected) {
            case 0: *outAction = MENU_ACTION_MOUNT_FAST;    s_overlay = OVERLAY_NONE; return;
            case 1: *outAction = MENU_ACTION_MOUNT_REGULAR; s_overlay = OVERLAY_NONE; return;
            case 2: rc5CheatsReset(); s_overlay = OVERLAY_CHEATS; return;
            case 3: g_cfg.optOverclock  = !g_cfg.optOverclock;  break;
            case 4: g_cfg.optEdc        = !g_cfg.optEdc;        break;
            case 5: g_cfg.optBootsector = !g_cfg.optBootsector; break;
        }
    }

    const char *labels[6] = {
        g_lang->fastBoot,
        g_lang->regularBoot,
        g_lang->cheats,
        g_lang->overclock,
        g_lang->edcCorrection,
        g_lang->bootsectorFix
    };
    const bool toggles[6] = {
        false, false, false,
        g_cfg.optOverclock, g_cfg.optEdc, g_cfg.optBootsector
    };

    for (int i = 0; i < 6; i++) {
        int y = 30 + i * 13;
        if (i == s_optSelected) {
            rc5DrawSolidRect(chain, 20, y, 140, 13, g_cfg.cursor);
        }
        char line[64];
        if (i < 3) {
            snprintf(line, sizeof(line), "   %s", labels[i]);
        } else {
            snprintf(line, sizeof(line), "%c %s",
                     toggles[i] ? (char)0x8A : (char)0x8B, labels[i]);
        }
        rc5PrintString(chain, &s_fontTex, 22, y, line, 158);
    }

    char footer[64];
    snprintf(footer, sizeof(footer), "%c OK      %c %s",
             (char)g_cfg.openGlyph, (char)g_cfg.backGlyph, g_lang->back);
    rc5PrintString(chain, &s_fontTex, 30, 113, footer, 165);
}

static void drawMainScreen(DMAChain *chain, uint16_t buttons, uint16_t prevButtons,
                           MenuAction *outAction, uint16_t fileCount, uint16_t *selectedIdx) {
    const bool styleFull = (g_cfg.style == 1);
    const int boxX = 9;
    const int boxY = styleFull ? 36 : 15;
    const int boxW = styleFull ? 302 : 172;
    const int boxH = styleFull ? 169 : 195;
    const int pageSize = styleFull ? 13 : 15;
    const int maxTextX = boxX + boxW - 2;

    rc5DrawBox(chain, boxX, boxY, boxW, boxH, true, g_cfg.filelist, true);
    rc5DrawTexture(chain, &s_logoTex, styleFull ? 96 : 185, styleFull ? 8 : 14);

    uint16_t pressed = (uint16_t)(~prevButtons & buttons);

    // Track idle vs input for cover delay & long-title scroll
    if (buttons == 0) {
        if (!s_creditsOpen && *outAction == MENU_ACTION_NONE && s_overlay == OVERLAY_NONE) {
            if (s_coverDelay > 0) s_coverDelay--;
            else if (s_scrollDelay > 0) s_scrollDelay--;
        }
    } else {
        if (buttons & (BUTTON_MASK_UP | BUTTON_MASK_DOWN | BUTTON_MASK_LEFT |
                       BUTTON_MASK_RIGHT | BUTTON_MASK_L1 | BUTTON_MASK_R1)) {
            resetSelectionEffects();
        }
    }

    // Hold auto-repeat for UP / DOWN
    if ((buttons & BUTTON_MASK_UP) && (prevButtons & BUTTON_MASK_UP)) {
        if (++s_holdRepeat > 30) { pressed ^= BUTTON_MASK_UP; s_holdRepeat = 25; }
    } else if ((buttons & BUTTON_MASK_DOWN) && (prevButtons & BUTTON_MASK_DOWN)) {
        if (++s_holdRepeat > 30) { pressed ^= BUTTON_MASK_DOWN; s_holdRepeat = 25; }
    } else {
        s_holdRepeat = 0;
    }

    // SELECT toggles credits screen (root directory only, or to close)
    if ((pressed & BUTTON_MASK_SELECT) && (s_dirDepth == 0 || s_creditsOpen)) {
        s_creditsOpen = !s_creditsOpen;
    }

    if (*outAction != MENU_ACTION_NONE) {
        rc5PrintString(chain, &s_fontTex, 40, 40, g_lang->loading, 310);
        return;
    }

    if (s_creditsOpen) {
        if (pressed & BUTTON_MASK_TRIANGLE) {
            *outAction = MENU_ACTION_BOOTLOADER;
        }
        int tx = boxX + 8;
        int ty = boxY + 10;
        char header[96];
        snprintf(header, sizeof(header),
                 "PicoStation (%s)\nPicoMenu (%s)\n\nPor: Schickacka",
                 PICOSTATION_RELEASE_STR, PICOMENU_RELEASE_STR);
        rc5PrintString(chain, &s_fontTex, tx, ty, header, maxTextX);

        char thanks[192];
        snprintf(thanks, sizeof(thanks),
                 "%s:\nGemmi, megavolt85,\nRama, Skitchin, Raijin,\nspicyjpeg, Danhans42,\nNicolasNoble, Maniac Vera",
                 g_lang->hugeThanks);
        rc5PrintString(chain, &s_fontTex, tx, ty + 78, thanks, maxTextX);

        char foot[96];
        snprintf(foot, sizeof(foot), "\x8c %s   \x95 %s", g_lang->back, g_lang->enterBootloader);
        rc5PrintString(chain, &s_fontTex, 12, 215, foot, 312);
        return;
    }

    // Navigation input (only when no overlay is open)
    if (s_overlay == OVERLAY_NONE) {
        if (fileCount > 0) {
            if (pressed & BUTTON_MASK_UP) {
                *selectedIdx = (*selectedIdx > 0) ? (uint16_t)(*selectedIdx - 1) : (uint16_t)(fileCount - 1);
            } else if (pressed & BUTTON_MASK_DOWN) {
                *selectedIdx = (*selectedIdx + 1 < fileCount) ? (uint16_t)(*selectedIdx + 1) : 0;
            }
            if (pressed & (BUTTON_MASK_LEFT | BUTTON_MASK_L1)) {
                *selectedIdx = (*selectedIdx >= pageSize) ? (uint16_t)(*selectedIdx - pageSize) : 0;
            } else if (pressed & (BUTTON_MASK_RIGHT | BUTTON_MASK_R1)) {
                *selectedIdx = (*selectedIdx + pageSize < fileCount) ? (uint16_t)(*selectedIdx + pageSize)
                                                                     : (uint16_t)(fileCount - 1);
            }
            if (pressed & (BUTTON_MASK_UP | BUTTON_MASK_DOWN | BUTTON_MASK_LEFT |
                           BUTTON_MASK_RIGHT | BUTTON_MASK_L1 | BUTTON_MASK_R1)) {
                sound_playOnChannel(&s_sfxClick, SFX_VOL, SFX_VOL, 0);
            }
            if (pressed & g_cfg.openMask) {
                fileData *fd = file_manager_get_file_data(*selectedIdx);
                *outAction = (fd->flag == 0) ? MENU_ACTION_OPEN_OPTIONS : MENU_ACTION_GOTO_DIRECTORY;
                sound_playOnChannel(&s_sfxSlide, SFX_VOL, SFX_VOL, 1);
            }
        }
        if ((pressed & g_cfg.backMask) && s_dirDepth > 0) {
            *outAction = MENU_ACTION_GOTO_PARENT;
            sound_playOnChannel(&s_sfxSlide, SFX_VOL, SFX_VOL, 1);
        }
        if ((pressed & BUTTON_MASK_SQUARE) && s_dirDepth == 0 && *outAction == MENU_ACTION_NONE) {
            *outAction = MENU_ACTION_OPEN_PLAYER;
            sound_playOnChannel(&s_sfxSlide, SFX_VOL, SFX_VOL, 1);
        }
    }

    // Counter "X de Y"
    char cntBuf[32];
    snprintf(cntBuf, sizeof(cntBuf), "%u %s %u",
             fileCount ? (unsigned)(*selectedIdx + 1) : 0u, g_lang->of, (unsigned)fileCount);
    rc5PrintString(chain, &s_fontTex, styleFull ? 16 : 186, styleFull ? 16 : 56, cntBuf, 315);

    if (fileCount == 0) {
        rc5PrintString(chain, &s_fontTex, 40, 40, g_lang->emptyFolder, maxTextX);
    } else {
        // Style 0: Draw & step async 128x128 cover art on the right
        if (!styleFull) {
            if (s_coverState == 4) {
                rc5DrawTexture(chain, &s_coverTex, 185, 82);
            }
            if (s_overlay == OVERLAY_NONE && *outAction == MENU_ACTION_NONE) {
                stepAsyncCoverLoader(*selectedIdx);
            }
        }

        int start = 0;
        if (fileCount > pageSize) {
            start = (int)*selectedIdx - (pageSize / 2);
            if (start < 0) start = 0;
            if (start > (int)fileCount - pageSize) start = (int)fileCount - pageSize;
        }
        int visible = (int)fileCount - start;
        if (visible > pageSize) visible = pageSize;

        for (int i = 0; i < visible; i++) {
            uint16_t idx = (uint16_t)(start + i);
            fileData *fd = file_manager_get_file_data(idx);
            char icon = (fd->flag == 0) ? (char)0x8F : (char)0x92;
            char line[300];
            snprintf(line, sizeof(line), "%c %s", icon, fd->filename);

            int rowY = boxY + i * 13;
            if (idx == *selectedIdx) {
                int w = rc5MeasureString(line) + 2;
                if (w > boxW) {
                    if (s_scrollDelay == 0) {
                        s_scrollDelay = 15;
                        if (s_scrollOffset == 0 && s_scrollDir < 0) {
                            s_scrollDir = 1;
                            s_scrollDelay = 30;
                        } else {
                            s_scrollOffset += s_scrollDir;
                        }
                    }
                    int fnLen = (int)strlen(fd->filename);
                    if (s_scrollOffset < 0) s_scrollOffset = 0;
                    if (s_scrollOffset >= fnLen) s_scrollOffset = (fnLen > 0) ? (fnLen - 1) : 0;
                    snprintf(line, sizeof(line), "%c %s", icon, fd->filename + s_scrollOffset);
                    if ((rc5MeasureString(line) < boxW - 6 || s_scrollOffset >= fnLen - 1) && s_scrollDir > 0) {
                        s_scrollDir = -1;
                        s_scrollDelay = 30;
                    }
                    w = boxW;
                }
                rc5StepCursorAnimation();
                rc5DrawSolidRect(chain, boxX, rowY, w, 13, g_cfg.cursor);
            }

            rc5PrintString(chain, &s_fontTex, boxX + 1, rowY, line, maxTextX);
        }
    }

    // Bottom help bar (hidden while an overlay modal is open)
    if (s_overlay == OVERLAY_NONE) {
        char footer[128];
        if (fileCount > 0 && s_dirDepth > 0) {
            snprintf(footer, sizeof(footer), "%c %s   %c %s",
                     (char)g_cfg.openGlyph, g_lang->open,
                     (char)g_cfg.backGlyph, g_lang->back);
        } else if (fileCount > 0) {
            snprintf(footer, sizeof(footer), "%c %s   \x8c %s",
                     (char)g_cfg.openGlyph, g_lang->open, g_lang->credits);
        } else if (s_dirDepth > 0) {
            snprintf(footer, sizeof(footer), "%c %s",
                     (char)g_cfg.backGlyph, g_lang->back);
        } else {
            snprintf(footer, sizeof(footer), "\x8c %s", g_lang->credits);
        }
        rc5PrintString(chain, &s_fontTex, 10, 215, footer, s_dirDepth == 0 ? 232 : 312);
        if (s_dirDepth == 0) {
            rc5PrintString(chain, &s_fontTex, 236, 215, "\x90 PicoPlayer", 316);
        }
    }
}

static void drawLoadingModal(const char *title, bool useWallpaper) {
    waitForDMADone();
    waitForGP0Ready();
    DMAChain *lchain = &s_dmaChains[0];
    lchain->nextPacket = lchain->data;
    uint32_t *lp = allocatePacket(lchain, 4);
    lp[0] = gp0_texpage(0, true, false);
    lp[1] = gp0_fbOffset1(0, 0);
    lp[2] = gp0_fbOffset2(SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    lp[3] = gp0_fbOrigin(0, 0);
    if (useWallpaper && g_cfg.hasWallpaperTex) {
        rc5DrawTexture(lchain, &g_cfg.bgTexLeft, 0, 0);
        rc5DrawTexture(lchain, &g_cfg.bgTexRight, 256, 0);
    } else {
        pu20DrawShellBackground(lchain, SCREEN_WIDTH, SCREEN_HEIGHT, s_frame);
    }
    rc5DrawBox(lchain, 48, 92, 224, 54, true, g_cfg.filelist, false);
    {
        int tw = rc5MeasureString(title);
        int lw = rc5MeasureString(g_lang->loading);
        rc5PrintString(lchain, &s_fontTex, 160 - tw / 2, 102, title, 268);
        rc5PrintString(lchain, &s_fontTex, 160 - lw / 2, 122, g_lang->loading, 268);
    }
    *(lchain->nextPacket) = gp0_endTag(0);
    waitForGP0Ready();
    waitForVblank();
    sendLinkedList(lchain->data);
    waitForDMADone();
    waitForGP0Ready();
    GPU_GP1 = gp1_fbOffset(0, 0);
    waitForVblank();
}

int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;
#ifdef PICO_MOCK_UI
    mockTtyPuts("MOCK_ENTRY_MAIN\n");
#endif

    COUNTERS[1].mode = 0x0100;
    s_dmaChains = (DMAChain *)malloc(sizeof(DMAChain) * 2);
    s_coverRaw  = (uint8_t *)malloc(COVER_PART_BYTES * COVER_PARTS);
    if (!s_dmaChains || !s_coverRaw) {
        for (;;) { __asm__ volatile(""); }
    }
    rc5ConfigInitDefaults();

    initIRQ();
#if DEBUG_LOGGING_ENABLED
    initSerialIO(115200);
#endif
    bool isPal = ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL);
    setupGPU(isPal ? GP1_MODE_PAL : GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);

    DMA_DPCR |= DMA_DPCR_CH_ENABLE(DMA_GPU);
    GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
    GPU_GP1 = gp1_dispBlank(false);

    // Upload RC5 8bpp Latin-1 13px font (96x196 @ 640,0; CLUT @ 640,196) and 4bpp logo (128x20 @ 736,0)
    uploadIndexedTexture(&s_fontTex, rc5FontTexture, rc5FontPalette,
                         640, 0, 640, 196, 96, 196, GP0_COLOR_8BPP);
    uploadIndexedTexture(&s_logoTex, rc5LogoTexture, rc5LogoPalette,
                         736, 0, 736, 20, 128, 20, GP0_COLOR_4BPP);

    initCDROM();
    initSPU();
    initControllerBus();

    sound_loadSoundFromBinary(click_sfx, &s_sfxClick);
    sound_loadSoundFromBinary(slide_sfx, &s_sfxSlide);

    // Check lid closed & sync TOC before reading config.ini / wallpaper / root listing
    waitUntilLidClosed(isPal);

    // Match official RC5: do not probe memory cards with 0x81 0x53 / 0x81 0x20
    // on startup (sendGameID always sends 0x81 0x21 unconditionally to both ports).
    rc5ConfigLoad();
    rc5WallpaperLoad();
    file_manager_init();

    bool secondFrame = false;
    MenuAction pendingAction = MENU_ACTION_GOTO_ROOT;
    uint16_t fileCount = 0;
    uint16_t selectedIdx = 0;
    uint16_t prevButtons = getButtons(g_cfg.openMask, g_cfg.backMask);

    for (;;) {
        s_frame++;
        // Drain any previous frame's GPU DMA before polling SIO0 so the PS1
        // bus is 100% idle during controller/BlueRetro exchange (eliminates
        // transient mid-scroll rumble blips).
        waitForDMADone();
        waitForGP0Ready();
        uint16_t buttons = getButtons(g_cfg.openMask, g_cfg.backMask);

        if (s_playerOpen) {
            DMAChain *chain = &s_dmaChains[0];
            chain->nextPacket = chain->data;

            bool leave = picoplayer_update_and_draw(chain, &s_fontTex, buttons, s_playerPrev);
            s_playerPrev = buttons;
            prevButtons  = buttons;

            if (leave) {
                picoplayer_begin_exit();
                drawLoadingModal("PicoMenu", false);
                picoplayer_finish_exit();
                if (g_cfg.wallpaper) {
                    rc5WallpaperLoad();
                }
                fileCount     = loadDirectoryListing(PICO_CMD_GOTO_ROOT, 0);
                selectedIdx   = 0;
                s_dirDepth    = 0;
                s_playerOpen  = false;
                secondFrame   = false;
                pendingAction = MENU_ACTION_NONE;
                resetSelectionEffects();
                continue;
            }

            if (chain->nextPacket != chain->data) {
                *(chain->nextPacket) = gp0_endTag(0);
                waitForGP0Ready();
                waitForVblank();
                picoplayer_present_vblank();
                sendLinkedList(chain->data);
            } else {
                waitForVblank();
                picoplayer_present_vblank();
            }
            continue;
        }

        int drawX = secondFrame ? SCREEN_WIDTH : 0;
        int dispX = secondFrame ? 0 : SCREEN_WIDTH;
        DMAChain *chain = &s_dmaChains[secondFrame];
        secondFrame = !secondFrame;

        // True double-buffering: display the already-completed frontbuffer (dispX)
        // while building and rendering the next frame into the hidden backbuffer (drawX).
        GPU_GP1 = gp1_fbOffset(dispX, 0);
        chain->nextPacket = chain->data;

        uint32_t *ptr = allocatePacket(chain, 4);
        ptr[0] = gp0_texpage(0, true, false);
        ptr[1] = gp0_fbOffset1(drawX, 0);
        ptr[2] = gp0_fbOffset2(drawX + SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
        ptr[3] = gp0_fbOrigin(drawX, 0);

        if (g_cfg.hasWallpaperTex) {
            rc5DrawTexture(chain, &g_cfg.bgTexLeft, 0, 0);
            rc5DrawTexture(chain, &g_cfg.bgTexRight, 256, 0);
        } else {
            pu20DrawShellBackground(chain, SCREEN_WIDTH, SCREEN_HEIGHT, s_frame);
        }
#ifdef PICO_MOCK_UI
        // Automated D-pad + cover-interruption + Options + Credits stress script
        static uint32_t s_mockFrame = 0;
        s_mockFrame++;
        if (s_mockFrame == 1) {
            mockTtyPuts("MOCK_F1_START\n");
        } else if (s_mockFrame == 2) {
            mockTtyPuts("MOCK_F2_START\n");
        } else if (s_mockFrame == 11) {
            mockTtyPuts("MOCK_F11_COVER\n");
        } else if (s_mockFrame == 12 || s_mockFrame == 13 || s_mockFrame == 30 || s_mockFrame == 45 ||
            s_mockFrame == 60 || s_mockFrame == 75 || s_mockFrame == 90) {
            // Deliberately press DOWN right on frame 12 (while async cover READ_N is in flight!)
            buttons |= BUTTON_MASK_DOWN;
        } else if (s_mockFrame == 120) {
            buttons |= BUTTON_MASK_UP;
            mockTtyPuts("MOCK_DPAD_SURVIVED\n");
        } else if (s_mockFrame == 150) {
            buttons |= g_cfg.openMask; // Open Options on .cue item (selectedIdx=6)
        } else if (s_mockFrame == 165) {
            buttons |= BUTTON_MASK_DOWN;
            mockTtyPuts("MOCK_OPTIONS_OK\n");
        } else if (s_mockFrame == 180) {
            buttons |= g_cfg.backMask; // Close Options
        } else if (s_mockFrame == 200) {
            buttons |= BUTTON_MASK_SELECT; // Open Credits
            mockTtyPuts("MOCK_CREDITS_OK\n");
        } else if (s_mockFrame == 230) {
            buttons |= BUTTON_MASK_SELECT; // Close Credits
        } else if (s_mockFrame == 250) {
            mockTtyPuts("MOCK_FRAME_250_PASS\n");
            // Also verify PT-BR lang=8 selection + REAL Fast Boot (FUN_8001fd0c + 0x8000FF2C LoadExec!)
            rc5LangSelect(8);
            mockTtyPuts(g_lang->loading);
            mockTtyPuts("\n");
            // Launch BOOT_MODE_FAST on our own ISO (reads SYSTEM.CNF -> SCES_313.37 -> sends GameID 0x21 -> runs trampoline LoadExec!)
            pendingAction = MENU_ACTION_MOUNT_FAST;
        }
#endif

        if (s_overlay == OVERLAY_NONE) {
            drawMainScreen(chain, buttons, prevButtons, &pendingAction, fileCount, &selectedIdx);
        } else {
            // Render main list dimmed/static behind active modal
            drawMainScreen(chain, 0, 0, &pendingAction, fileCount, &selectedIdx);
            if (s_overlay == OVERLAY_OPTIONS) {
                drawOptionsOverlay(chain, buttons, prevButtons, &pendingAction);
            } else if (s_overlay == OVERLAY_CHEATS) {
                uint16_t rawIdx = file_manager_get_file_index(selectedIdx);
                int r = rc5CheatsDrawAndUpdate(chain, &s_fontTex, buttons, prevButtons,
                                               rawIdx, &s_sfxClick, &s_sfxSlide);
                if (r == 1) {
                    s_overlay = OVERLAY_OPTIONS;
                } else if (r == 2) {
                    pendingAction = MENU_ACTION_MOUNT_CHEATS;
                    s_overlay = OVERLAY_NONE;
                }
            } else if (s_overlay == OVERLAY_BOOTING) {
                rc5BootStepAndDraw(chain, &s_fontTex);
            }
        }

        *(chain->nextPacket) = gp0_endTag(0);
#ifdef PICO_MOCK_UI
        if (s_mockFrame <= 3) mockTtyPuts("MOCK_PRE_GP0\n");
#endif
        waitForGP0Ready();
#ifdef PICO_MOCK_UI
        if (s_mockFrame <= 3) mockTtyPuts("MOCK_PRE_VBLANK\n");
#endif
        waitForVblank();
#ifdef PICO_MOCK_UI
        if (s_mockFrame <= 3) mockTtyPuts("MOCK_POST_VBLANK\n");
#endif
        sendLinkedList(chain->data);
        prevButtons = buttons;

        if (pendingAction == MENU_ACTION_NONE || cdromBusy) continue;

        if (pendingAction == MENU_ACTION_GOTO_ROOT) {
#ifdef PICO_MOCK_UI
            mockTtyPuts("MOCK_GOTO_ROOT\n");
            static const char *const kMockItems[] = {
                "Resident Evil 2 - Dual Shock Ver. (USA)",
                "Resident Evil 3 - Nemesis (USA)",
                "Tomb Raider (USA) (v1.0)",
                "PaRappa the Rapper (USA)",
                "MediEvil (Europe)",
                "Crash Bandicoot (Europe) (No EDC)",
                "Castlevania - Symphony of the Night.cue",
                "Final Fantasy VII (Disc 1).cue",
                "Metal Gear Solid (Disc 1).cue",
                "Gran Turismo 2 (Simulation).cue"
            };
            fileCount = 0;
            for (int i = 0; i < 10; i++) {
                uint8_t isDir = (i < 6) ? 1 : 0;
                file_manager_init_file_data(fileCount, isDir, (char *)kMockItems[i],
                                            (uint16_t)strlen(kMockItems[i]));
                fileCount++;
            }
            file_manager_sort(fileCount);
            mockTtyPuts("MOCK_SORT_DONE\n");
#else
            fileCount = loadDirectoryListing(PICO_CMD_GOTO_ROOT, 0);
#endif
            selectedIdx = 0;
            s_dirDepth = 0;
            resetSelectionEffects();
        } else if (pendingAction == MENU_ACTION_GOTO_PARENT) {
            fileCount = loadDirectoryListing(PICO_CMD_GOTO_PARENT, 0);
            if (s_dirDepth > 0) s_dirDepth--;
            selectedIdx = s_dirStack[s_dirDepth];
            if (selectedIdx >= fileCount && fileCount > 0) selectedIdx = 0;
            resetSelectionEffects();
        } else if (pendingAction == MENU_ACTION_GOTO_DIRECTORY) {
            uint16_t rawIdx = file_manager_get_file_index(selectedIdx);
            if (s_dirDepth < 16) s_dirStack[s_dirDepth++] = selectedIdx;
            fileCount = loadDirectoryListing(PICO_CMD_GOTO_DIRECTORY, rawIdx);
            selectedIdx = 0;
            resetSelectionEffects();
        } else if (pendingAction == MENU_ACTION_BOOTLOADER) {
            picoSendCommand(PICO_CMD_BOOTLOADER, 0xBEEF);
        } else if (pendingAction == MENU_ACTION_OPEN_OPTIONS) {
            s_optSelected = 0;
            s_overlay = OVERLAY_OPTIONS;
        } else if (pendingAction == MENU_ACTION_OPEN_PLAYER) {
            /* Show a clean "PicoPlayer / Aguarde, carregando..." modal on screen
             * while picoplayer_enter() scans SD:/PicoPlayer/ and preloads covers. */
            drawLoadingModal("PicoPlayer", true);

            picoplayer_enter();
            s_playerOpen = true;
            s_playerPrev = buttons;
        } else if (pendingAction == MENU_ACTION_MOUNT_FAST ||
                   pendingAction == MENU_ACTION_MOUNT_REGULAR ||
                   pendingAction == MENU_ACTION_MOUNT_CHEATS) {
            Rc5BootMode mode = (pendingAction == MENU_ACTION_MOUNT_REGULAR) ? BOOT_MODE_REGULAR :
                               (pendingAction == MENU_ACTION_MOUNT_CHEATS)  ? BOOT_MODE_CHEATS  :
                                                                              BOOT_MODE_FAST;
            rc5BootStart(mode, selectedIdx, 0x03u);
            s_overlay = OVERLAY_BOOTING;
        }

        pendingAction = MENU_ACTION_NONE;
    }

    return 0;
}
