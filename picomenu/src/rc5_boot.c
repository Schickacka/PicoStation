#include "rc5_boot.h"
#include "rc5_cheats.h"
#include "rc5_config.h"
#include "rc5_lang.h"
#include "rc5_proto.h"
#include "rc5_ui.h"
#include "controller.h"
#include "file_manager.h"
#include "ps1/cdrom.h"
#include "ps1/cop0.h"
#include "ps1/registers.h"
#include "psxproject/cdrom.h"
#include "psxproject/filesystem.h"
#include "psxproject/system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Exact RC5 8-game Overclock exclusion list (FUN_800123f8)
static const char *const s_noOverclockGames[] = {
    "cdrom:\\SLES_005.98;1",
    "cdrom:\\SCUS_949.07;1",
    "cdrom:\\SLPM_860.75;1",
    "cdrom:\\SLES_012.98;1",
    "cdrom:\\SLUS_007.15;1",
    "cdrom:\\SCES_007.00;1",
    "cdrom:\\SLUS_004.83;1",
    "cdrom:\\SLPS_006.50;1"
};

static Rc5BootMode s_mode = BOOT_MODE_FAST;
static uint16_t    s_visualIdx = 0;
static uint8_t     s_mcpPresent = 0;
static int         s_step = 0;
static int         s_frameCounter = 0;
static int         s_cdCheckRetries = 0;
static bool        s_bootOverclock = true;
static char        s_logText[512];
static char        s_bootPath[64];
static char        s_sendIdStr[64];
static uint32_t    s_tcb = 4;
static uint32_t    s_event = 16;
static uint32_t    s_stack = 0x801FFF00u;
static char        s_cnfBuf[2048] __attribute__((aligned(4)));

extern const uint8_t rc5SetConfRetStub[];
extern const uint8_t rc5DummyReturn[];
extern const uint8_t rc5LoadExecTrampoline[];
extern const uint8_t rc5LoadExecTrampolineEnd[];

static void appendLog(const char *txt) {
    size_t cur = strlen(s_logText);
    if (cur + 1 >= sizeof(s_logText)) return;
    strncat(s_logText, txt, sizeof(s_logText) - cur - 1);
}

static bool parseCnfString(const char *cnf, const char *key, char *out, size_t outSz) {
    size_t klen = strlen(key);
    const char *p = cnf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        const char *eol = p;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;
        if (strncmp(p, key, klen) == 0) {
            char next = p[klen];
            if (next == ' ' || next == '\t' || next == '=' || next == '\r' || next == '\n' || next == '\0') {
                const char *v = p + klen;
                while (v < eol && (*v == ' ' || *v == '\t' || *v == '=')) v++;
                size_t n = 0;
                while (v < eol && *v != ' ' && *v != '\t' && n + 1 < outSz) {
                    out[n++] = *v++;
                }
                out[n] = '\0';
                return n > 0;
            }
        }
        p = eol;
    }
    return false;
}

static bool parseCnfHex(const char *cnf, const char *key, uint32_t *outVal) {
    char tmp[24];
    if (!parseCnfString(cnf, key, tmp, sizeof(tmp))) return false;
    const char *s = tmp;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    uint32_t val = 0;
    bool anyDigit = false;
    for (; *s; s++) {
        char c = *s;
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        val = (val << 4) | (uint32_t)d;
        anyDigit = true;
    }
    if (!anyDigit) return false;
    *outVal = val;
    return true;
}

static void checkNoOverclockList(const char *gameId) {
    if (!s_bootOverclock) return;
    for (size_t i = 0; i < sizeof(s_noOverclockGames) / sizeof(s_noOverclockGames[0]); i++) {
        if (strcmp(gameId, s_noOverclockGames[i]) == 0) {
            s_bootOverclock = false;
            return;
        }
    }
}

// BIOS syscall helpers with full MIPS caller-saved clobber lists
static void biosEnterCriticalSection(void) {
    register uint32_t a0 __asm__("a0") = 1;
    __asm__ volatile("syscall 0\n\tnop\n"
                     : "+r"(a0) :
                     : "v0", "v1", "a1", "a2", "a3", "t0", "t1", "t2", "t3",
                       "t4", "t5", "t6", "t7", "t8", "t9", "ra", "memory");
}

static void biosExitCriticalSection(void) {
    register uint32_t a0 __asm__("a0") = 2;
    __asm__ volatile("syscall 0\n\tnop\n"
                     : "+r"(a0) :
                     : "v0", "v1", "a1", "a2", "a3", "t0", "t1", "t2", "t3",
                       "t4", "t5", "t6", "t7", "t8", "t9", "ra", "memory");
}

static void biosCall0(uint32_t vec, uint32_t fn) {
    register uint32_t t1 __asm__("t1") = fn;
    register uint32_t t2 __asm__("t2") = vec;
    __asm__ volatile(
        ".set push\n\t"
        ".set noreorder\n\t"
        "addiu $sp, $sp, -16\n\t"
        "jalr %1\n\t"
        "nop\n\t"
        "addiu $sp, $sp, 16\n\t"
        ".set pop\n"
        : "+r"(t1), "+r"(t2) :
        : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t3",
          "t4", "t5", "t6", "t7", "t8", "t9", "ra", "memory");
}

static void biosCall1(uint32_t vec, uint32_t fn, uint32_t arg0) {
    register uint32_t a0 __asm__("a0") = arg0;
    register uint32_t t1 __asm__("t1") = fn;
    register uint32_t t2 __asm__("t2") = vec;
    __asm__ volatile(
        ".set push\n\t"
        ".set noreorder\n\t"
        "addiu $sp, $sp, -16\n\t"
        "jalr %2\n\t"
        "nop\n\t"
        "addiu $sp, $sp, 16\n\t"
        ".set pop\n"
        : "+r"(a0), "+r"(t1), "+r"(t2) :
        : "v0", "v1", "a1", "a2", "a3", "t0", "t3",
          "t4", "t5", "t6", "t7", "t8", "t9", "ra", "memory");
}

static void biosSetConf(uint32_t event, uint32_t tcb, uint32_t stack) {
    register uint32_t a0 __asm__("a0") = event;
    register uint32_t a1 __asm__("a1") = tcb;
    register uint32_t a2 __asm__("a2") = stack;
    register uint32_t t1 __asm__("t1") = 0x9Cu;
    register uint32_t t2 __asm__("t2") = 0xA0u;
    __asm__ volatile(
        ".set push\n\t"
        ".set noreorder\n\t"
        "addiu $sp, $sp, -16\n\t"
        "jalr %4\n\t"
        "nop\n\t"
        "addiu $sp, $sp, 16\n\t"
        ".set pop\n"
        : "+r"(a0), "+r"(a1), "+r"(a2), "+r"(t1), "+r"(t2) :
        : "v0", "v1", "a3", "t0", "t3", "t4", "t5", "t6",
          "t7", "t8", "t9", "ra", "memory");
}

// Resolve BIOS kernel table jump at *(0x80000480) + idx*4 (exact RC5 FUN_8001fe64)
static void biosCallKernelTable(int idx) {
    uint32_t base = *(volatile uint32_t *)0x80000480u;
    uint32_t instr = *(volatile uint32_t *)(base + (uint32_t)idx * 4u);
    uint32_t target = (base & 0xF0000000u) | ((instr & 0x03FFFFFFu) << 2);
    ((void (*)(void))target)();
}

// Exact official RC5 Fast Boot kernel reset + LoadExec (matches FUN_8001fd0c + case 0xb byte-for-byte!)
// CRITICAL: Do NOT call uninstallExceptionHandler() here! uninstallExceptionHandler() zeroes
// DMA_DPCR (disabling all 7 PS1 DMA channels!) and clears COP0_STATUS_CU2 (disabling GTE!),
// which causes retail games like RE2/RE3 to hang on their first GPU/CDROM DMA!
// Official RC5 leaves DMA_DPCR = 0x0bbbbbbb and COP0_STATUS_CU2 = 1, and lets BIOS C(07h)
// (InstallExceptionHandlers) reinstall the fresh BIOS exception vectors.
static void rc5FastExecViaTrampoline(bool installCheats) {
    // Ensure SIO0 /DTR is released and no stale DSR IRQ remains before BIOS kernel reset
    SIO_CTRL(0) &= ~SIO_CTRL_DTR;
    SIO_CTRL(0) |= SIO_CTRL_ACKNOWLEDGE;
    IRQ_STAT     = ~(1 << IRQ_SIO0);

    biosEnterCriticalSection();
    cop0_setReg(COP0_STATUS, cop0_getReg(COP0_STATUS) & ~0x0401u);

    // Exact RC5 FUN_8001fd0c(): full BIOS kernel reset
    biosEnterCriticalSection();
    memset((void *)0xA000E000u, 0, 0x2000u);
    biosCallKernelTable(0x0C); // ResetEntryInt / internal BIOS init
    biosCallKernelTable(0x0E); // BIOS internal heap/device init
    biosCall0(0xA0u, 0x45u);   // InitA0B0C0Vectors
    biosCall0(0xC0u, 0x1Cu);   // AdjustA0Table
    biosCall0(0xC0u, 0x07u);   // InstallExceptionHandlers
    biosCall0(0xB0u, 0x18u);   // SetDefaultExitFromException
    IRQ_STAT = 0;
    IRQ_MASK = 0;
    biosCall1(0xC0u, 0x12u, 0u); // InstallDevices(0)

    // Stub 0x80000488 so BIOS SetConf returns to us instead of jumping into shell!
    volatile uint32_t *vec488 = (volatile uint32_t *)0x80000488u;
    uint32_t saved488 = *vec488;
    *vec488 = (uint32_t)rc5SetConfRetStub;
    biosSetConf(0x10u, 4u, 0x801FFF00u);
    *vec488 = saved488;

    biosCall1(0xA0u, 0x9Fu, 8u); // SetMem(8) -> configure 2MB RAM
    biosExitCriticalSection();

    volatile uint32_t *vec484 = (volatile uint32_t *)0x80000484u;
    uint32_t saved484 = *vec484;

    // Save boot path & stack in kernel scratchpad below 0x8000C000
    strcpy((char *)0xA000B8B0u, s_bootPath);
    *(volatile uint32_t *)0xA000B948u = s_stack;

    // Stub 0x80000484 around _96_init() + final SetConf() (exact RC5 case 0xb order!)
    *vec484 = (uint32_t)rc5DummyReturn;
    biosCall0(0xA0u, 0x54u); // _96_init()
    biosEnterCriticalSection();
    biosSetConf(s_event, s_tcb, s_stack);
    *vec484 = saved484;

    if (installCheats) {
        rc5CheatsInstallToRam();
    }

    // Relocate exact RC5 0xD4-byte trampoline to 0x8000FF2C and jump!
    size_t trampSize = (size_t)(rc5LoadExecTrampolineEnd - rc5LoadExecTrampoline);
    memcpy((void *)0x8000FF2Cu, rc5LoadExecTrampoline, trampSize);
    biosCall0(0xA0u, 0x44u); // FlushCache

    ((void (*)(void))0x8000FF2Cu)();
    __builtin_unreachable();
}

void rc5BootStart(Rc5BootMode mode, uint16_t selectedVisualIdx, uint8_t mcpPresent) {
    s_mode           = mode;
    s_visualIdx      = selectedVisualIdx;
    s_mcpPresent     = mcpPresent;
    s_step           = 0;
    s_frameCounter   = 0;
    s_cdCheckRetries = 0;
    s_bootOverclock  = g_cfg.optOverclock;
    s_logText[0]     = '\0';
    s_sendIdStr[0]   = '\0';
    strcpy(s_bootPath, "cdrom:\\PSX.EXE;1");
    s_tcb   = 4;
    s_event = 16;
    s_stack = 0x801FFF00u;
}

void rc5BootStepAndDraw(DMAChain *chain, const TextureInfo *font) {
    // Modal box (exact RC5 geometry: 15,25 190x114 outer; 20,30 180x104 inner)
    rc5DrawBox(chain, 15, 25, 190, 114, true, g_cfg.bg, false);
    rc5DrawBox(chain, 20, 30, 180, 104, true, g_cfg.filelist, false);

    switch (s_step) {
        case 0: {
            fileData *fd = file_manager_get_file_data(s_visualIdx);
            uint16_t rawIdx = file_manager_get_file_index(s_visualIdx);
            snprintf(s_logText, sizeof(s_logText), g_lang->mountFmt, g_lang->loading, fd->filename);
            picoSendCommand(PICO_CMD_MOUNT_FILE, rawIdx);
            s_frameCounter = 0;
            s_step = 1;
            break;
        }

        case 1: {
            // Wait ~60 frames (~1s) for initial mount, then poll Getstat until lid closed & motor ready
            s_frameCounter++;
            if (s_frameCounter > 60) {
                issueCDROMCommand(CDROM_CMD_NOP, NULL, 0); // 0x01 Getstat
                waitForINT3();
                if ((((cdromStatus & 0x10u) == 0) && ((cdromStatus & 0x02u) != 0)) || s_frameCounter > 180) {
                    s_frameCounter = 0;
                    appendLog(g_lang->updateToc);
                    s_step = 2;
                }
            }
            break;
        }

        case 2: {
            if (s_frameCounter == 0) {
                updateCDROM_TOC();
            }
            s_frameCounter++;
            if (s_frameCounter > 26) {
                s_frameCounter = 0;
                s_cdCheckRetries = 0;
                appendLog(g_lang->checkCd);
                s_step = 3;
            }
            break;
        }

        case 3: {
            // Multi-track CDDA resilience (e.g. Tomb Raider 57 tracks):
            // Retry up to 16 times spaced 20 frames (~330 ms) apart, issuing a full
            // CDROM_CMD_INIT on retries 4 & 8 if HC05 ever needs a clean TOC rescan.
            if (s_frameCounter > 0) {
                s_frameCounter--;
                break;
            }
            if (is_playstation_cd()) {
                s_step = 5; // Valid PlayStation data disc!
            } else if (s_cdCheckRetries < 16) {
                s_cdCheckRetries++;
                if (!waitingForInt5) {
                    if (s_cdCheckRetries == 4 || s_cdCheckRetries == 8) {
                        issueCDROMCommand(CDROM_CMD_INIT, NULL, 0);
                        waitForINT3();
                        waitForINT2();
                    }
                    updateCDROM_TOC();
                }
                s_frameCounter = 20;
            } else {
                s_step = 4; // Genuine audio-only CDDA disc
            }
            break;
        }

        case 4: {
            appendLog(g_lang->cddaImage);
            s_step = 100;
            break;
        }

        case 5: {
            if (initFilesystem() == 0) {
                appendLog(g_lang->loadSystemCnf);
                s_step = 6;
            } else {
                s_step = 100;
            }
            break;
        }

        case 6: {
            memset(s_cnfBuf, 0, sizeof(s_cnfBuf));
            if (file_load("SYSTEM.CNF;1", s_cnfBuf) == 0) {
                char parsed[64];
                if (parseCnfString(s_cnfBuf, "BOOT", parsed, sizeof(parsed))) {
                    strncpy(s_bootPath, parsed, sizeof(s_bootPath) - 1);
                    s_bootPath[sizeof(s_bootPath) - 1] = '\0';
                }
                uint32_t v = 0;
                if (parseCnfHex(s_cnfBuf, "TCB", &v) && v >= 1u && v <= 32u) {
                    s_tcb = v;
                }
                if (parseCnfHex(s_cnfBuf, "EVENT", &v) && v >= 1u && v <= 64u) {
                    s_event = v;
                }
                if (parseCnfHex(s_cnfBuf, "STACK", &v) && v >= 0x80010000u && v <= 0x801FFFF0u) {
                    s_stack = v;
                }
            }
            appendLog("BOOT = ");
            appendLog(s_bootPath);
            appendLog("\n");
            s_step = 7;
            break;
        }

        case 7: {
            // Format game ID string for MemCard PRO & Overclock exclusion check
            size_t len = strlen(s_bootPath);
            if (len < 20) {
                if (strstr(s_bootPath, ";1") == NULL) {
                    snprintf(s_sendIdStr, sizeof(s_sendIdStr), "%s;1", s_bootPath);
                } else {
                    strncpy(s_sendIdStr, s_bootPath, sizeof(s_sendIdStr) - 1);
                    s_sendIdStr[sizeof(s_sendIdStr) - 1] = '\0';
                }
            } else {
                snprintf(s_sendIdStr, sizeof(s_sendIdStr), "cdrom:\\%s", s_bootPath + (len - 13));
            }
            appendLog(g_lang->sendId);
            appendLog(s_sendIdStr);
            appendLog("\n");
            s_step = 8;
            break;
        }

        case 8: {
            if (s_sendIdStr[0] != '\0') {
                // Match official RC5: ALWAYS send GameID (0x21) to Port 0 & Port 1
                // unconditionally (no 0x20 ping gate -> works with OWMC, MemCard PRO, SD2PSX!)
                sendGameID(s_sendIdStr, 0x03u);
                checkNoOverclockList(s_sendIdStr);
            }
            s_step = 9;
            break;
        }

        case 9: {
            // Space extended Mechacon commands 1 per frame so CXD/HC05 never drops a burst
            if (!s_bootOverclock) {
                picoSendCommand(PICO_CMD_EXTENDED, (uint16_t)(PICO_EXT_OVERCLOCK << 1)); // arg 10 -> OC OFF
            }
            s_step = 91;
            break;
        }

        case 91: {
            if (!g_cfg.optEdc) {
                picoSendCommand(PICO_CMD_EXTENDED, (uint16_t)((PICO_EXT_SKIP_EDC << 1) | 1)); // arg 5 -> skip EDC
            }
            s_step = 92;
            break;
        }

        case 92: {
            if (!g_cfg.optBootsector) {
                picoSendCommand(PICO_CMD_EXTENDED, (uint16_t)((PICO_EXT_SKIP_BOOTSECTOR << 1) | 1)); // arg 3 -> skip bootsector
            }
            if (s_mode == BOOT_MODE_REGULAR) {
                appendLog(g_lang->rebootPs1);
                s_step = 101;
            } else {
                appendLog(g_lang->loadAndExecute);
                s_step = 10;
            }
            break;
        }

        case 10: {
            // Stop CD motor cleanly before handing off to BIOS LoadExec
            issueCDROMCommand(CDROM_CMD_STOP, NULL, 0);
            s_step = 11;
            break;
        }

        case 11: {
            // Instant RC5-style Fast Boot (both normal and with cheats) via 0x8000FF00 trampoline
            bool useCheats = (s_mode == BOOT_MODE_CHEATS && rc5CheatsHasEnabled());
            rc5FastExecViaTrampoline(useCheats);
            break;
        }

        case 100: {
            appendLog(g_lang->exitToBios);
            s_step = 101;
            break;
        }

        case 101: {
            // Draw one final frame showing the status message before rebooting
            s_step = 102;
            break;
        }

        case 102: {
            softReset();
            break;
        }
    }

    rc5PrintString(chain, font, 22, 30, s_logText, 198);
}
