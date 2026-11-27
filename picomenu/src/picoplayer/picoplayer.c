/*
 * PicoStation 2.0 - PicoPlayer (PU-20 CD-DA player + SoundScope)
 *
 * The CD transport is driven through the real PS1 CD-ROM controller:
 *   GETTN (0x13) / GETTD (0x14) to build the TOC
 *   SETMODE (0x0E) with CD-DA + SETLOC (0x02) + PLAY (0x03)
 *   PAUSE (0x09) / STOP (0x08) / GETLOCP (0x11) for position reporting
 *
 * The SoundScope reads the two SPU CD capture windows (0x0000 CD-Left and
 * 0x0400 CD-Right, 512 bytes each) selecting the half that is not being
 * written using SPU_STAT bit 11.
 */

#include "picoplayer.h"
#include "rc5_proto.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pu20_skin.h"

#include "controller.h"
#include "gpu.h"
#include "rc5_config.h"
#include "rc5_ui.h"

#include "ps1/cdrom.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"
#include "psxproject/cdrom.h"
#include "psxproject/delay.h"
#include "psxproject/irq.h"

/* Cleared after blocking CD-ROM transport calls so the next waitForVblank()
 * always aligns to a fresh VBlank instead of firing mid-scanout (no flash!). */
extern volatile bool vblank;

/* spu.h uses a pre-existing `const` return qualifier; keep -Werror clean. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "psxproject/spu.h"
#pragma GCC diagnostic pop

#define PP_MAX_TRACKS     99
#define PP_LIST_VISIBLE   8
#define PP_GRID_COLS      5
#define PP_GRID_ROWS      4
#define PP_GRID_PAGE      (PP_GRID_COLS * PP_GRID_ROWS) /* 20 cells */

#define PP_SCOPE_SAMPLES  256
#define PP_SPECTRUM_BANDS 24
#define PP_KEYPAD_DIGITS  10

#define PP_CD_FRAMES_PER_SEC 75u
#define PP_CD_VOLUME         0x7FFFu

#define PP_SCOPE_MODES    22

typedef enum {
    PP_FOCUS_GRID = 0,
    PP_FOCUS_MENU,
    PP_FOCUS_KEYPAD,
    PP_FOCUS_TRANSPORT
} PpFocus;

typedef enum {
    PP_REPEAT_OFF = 0,
    PP_REPEAT_ONE,
    PP_REPEAT_ALL
} PpRepeat;

enum {
    PP_MENU_CONTINUE = 0,
    PP_MENU_SHUFFLE,
    PP_MENU_PROGRAM,
    PP_MENU_REPEAT,
    PP_MENU_TIME,
    PP_MENU_EXIT,
    PP_MENU_COUNT
};

static struct {
    bool     active;
    bool     tocValid;
    bool     dataDisc;

    uint8_t  firstTrack;
    uint8_t  lastTrack;
    uint32_t startFrame[PP_MAX_TRACKS + 1];
    uint32_t lengthFrame[PP_MAX_TRACKS + 1];
    uint32_t discEndFrame;
    bool     dataTrack[PP_MAX_TRACKS + 1];

    uint8_t  curTrack;
    uint8_t  selTrack;
    uint8_t  page;
    uint8_t  focus;
    uint8_t  gridIndex;
    uint8_t  listCursor;
    uint8_t  listScroll;
    uint8_t  catalogCount;
    uint8_t  menuIndex;
    uint8_t  transportIndex;
    uint8_t  keypadIndex;
    bool     keypad;
    uint8_t  entry;
    uint8_t  entryCount;
    uint8_t  timeMode;
    uint8_t  repeatMode;
    bool     shuffle;
    bool     program;
    bool     programmed[PP_MAX_TRACKS + 1];
    uint8_t  programOrder[PP_MAX_TRACKS + 1];
    uint8_t  programCount;

    bool     playing;
    bool     paused;
    bool     requestExit;
    uint8_t  exitFade;
    uint8_t  menuClickTimer;
    int8_t   menuClickIdx;
    uint8_t  trackClickTimer;
    int8_t   trackClickIdx;

    uint32_t posFrame;
    uint32_t tickAcc;
    uint32_t frame;
    uint32_t pollCounter;
    uint32_t rng;
    int      discAngle;

    uint8_t  fps;
    uint8_t  scopeMode;
    uint8_t  scopePalette;
    uint8_t  scopeSpeed;
    uint8_t  scopeTrailLevel;
    uint8_t  scopeBuf;
    int16_t  pendingDispX;
    bool     hasPendingDisp;
    bool     scope;
    bool     scopeTrails;
    uint32_t scopeFrame;
    uint32_t scopePhase;
    int      turtleX;
    int      turtleY;
    uint8_t  turtleDir;

    int16_t  scopeL[PP_SCOPE_SAMPLES];
    int16_t  scopeR[PP_SCOPE_SAMPLES];
    uint8_t  spectrum[PP_SPECTRUM_BANDS];
    uint8_t  peak[PP_SPECTRUM_BANDS];
    uint8_t  levelL;
    uint8_t  levelR;

    char     toast[24];
    int16_t  toastTimer;
    uint16_t toastGen;
} s_pp;

/* 16-bit PCM scratch for the capture buffers (fixed in BSS, never on stack). */
static uint8_t s_captureBuf[1024] __attribute__((aligned(4)));
static uint8_t s_probeBuf[2048] __attribute__((aligned(4)));
static PpTrackMeta s_catalog[PP_MAX_TRACKS];

/* Album art cache in PS1 RAM (preloaded during SD scan before audio mount so
 * track switching never interrupts CD-DA / I2S playback). Deduplicates identical
 * covers (e.g. album-shared cover.cov) across all 99 tracks. */
#define PP_ART_PARTS       8
#define PP_ART_PART_BYTES  2114u
#define PP_ART_RAW_BYTES   (PP_ART_PARTS * PP_ART_PART_BYTES)
#define PP_ART_MAX_SLOTS   24

static uint8_t    *s_artCache = NULL;
static uint8_t     s_artSlotCount = 0;
static int8_t      s_trackArtSlot[PP_MAX_TRACKS];
static int8_t      s_uploadedArtSlot = -1;
static TextureInfo s_artTex;

/* Dirty-rect tracker for single-buffered 640x480i: avoids wiping and redrawing
 * the full 640x480 background every frame (which caused severe 480i flicker &
 * combing on real PS1 hardware). Only updates changed widgets (< 0.4 ms, inside
 * VBlank) so the screen stays 100% rock-solid and stripe-free! */
static struct {
    bool     bgDirty;
    uint32_t dispSec;
    uint32_t marqueeStep;
    uint8_t  curTrack;
    uint8_t  timeMode;
    uint8_t  repeatMode;
    uint8_t  focus;
    uint8_t  listCursor;
    uint8_t  listScroll;
    uint8_t  menuIndex;
    uint8_t  transportIndex;
    uint8_t  keypadIndex;
    uint8_t  entry;
    uint8_t  entryCount;
    uint8_t  menuClickTimer;
    uint8_t  trackClickTimer;
    bool     playing;
    bool     paused;
    bool     shuffle;
    bool     program;
    bool     keypad;
    bool     hadToast;
    uint16_t toastGen;
    bool     cursorSaved;
    int16_t  cursorSaveX;
    int16_t  cursorSaveY;
} s_vis;

/* Hardware VRAM-to-VRAM blit (GP0 0x80) takes ~5 us — lets us save/restore the
 * concentric-rings background under the cursor, transport strip and minus sign
 * in free VRAM column x=960..1023 without EVER wiping the 640x480i screen! */
static void ppVramBlit(DMAChain *chain, int srcX, int srcY, int dstX, int dstY, int w, int h) {
    uint32_t *p = allocatePacket(chain, 4);
    p[0] = gp0_vramBlit();
    p[1] = gp0_xy(srcX, srcY);
    p[2] = gp0_xy(dstX, dstY);
    p[3] = gp0_xy(w, h);
}

static void ppArtFree(void) {
    if (s_artCache) {
        free(s_artCache);
        s_artCache = NULL;
    }
    s_artSlotCount = 0;
    s_uploadedArtSlot = -1;
    for (int i = 0; i < PP_MAX_TRACKS; i++) {
        s_trackArtSlot[i] = -1;
    }
}

static void ppEnsureArtUploaded(int catalogIdx) {
    int8_t slot = (catalogIdx >= 0 && catalogIdx < PP_MAX_TRACKS)
                      ? s_trackArtSlot[catalogIdx] : -1;
    if (slot < 0 || !s_artCache || (uint8_t) slot >= s_artSlotCount) {
        s_uploadedArtSlot = -1;
        return;
    }
    if (slot == s_uploadedArtSlot) return;

    const uint8_t *raw = s_artCache + (size_t) slot * PP_ART_RAW_BYTES;
    /* Set bit 15 (STP=1) on every CLUT entry so pure black (0x0000 -> 0x8000)
     * renders as crisp opaque black instead of GPU-transparent! */
    static uint16_t s_opaqueClut[256] __attribute__((aligned(4)));
    for (int i = 0; i < 256; i++) {
        uint16_t w = (uint16_t) raw[i * 2] | ((uint16_t) raw[i * 2 + 1] << 8);
        s_opaqueClut[i] = (uint16_t)(w | 0x8000u);
    }

    waitForDMADone();
    waitForGP0Ready();
    /* VRAM (640, 360) 128x128 8bpp + CLUT (640, 505) — zero overlap with PU-20 assets */
    uploadIndexedTexture(&s_artTex, raw + 512, s_opaqueClut,
                         640, 360, 640, 505, 128, 128, GP0_COLOR_8BPP);
    s_uploadedArtSlot = slot;
}


/* ------------------------------------------------------------------------- */
/* Small drawing helpers                                                     */
/* ------------------------------------------------------------------------- */

static void ppToast(const char *text) {
    size_t i = 0;

    if (!text) return;
    while (text[i] && i < sizeof(s_pp.toast) - 1) {
        s_pp.toast[i] = text[i];
        i++;
    }
    s_pp.toast[i] = '\0';
    s_pp.toastTimer = 90;
    s_pp.toastGen++;
}

static bool ppHasAudioTracks(void) {
    if (!s_pp.tocValid) return false;
    for (int t = s_pp.firstTrack; t <= s_pp.lastTrack; t++) {
        if (!s_pp.dataTrack[t]) return true;
    }
    return false;
}

static bool ppAudioReady(void) {
    return ppHasAudioTracks() && s_pp.curTrack >= s_pp.firstTrack &&
           s_pp.curTrack <= s_pp.lastTrack && !s_pp.dataTrack[s_pp.curTrack];
}

static uint32_t ppTrackLength(uint8_t track) {
    if (track < s_pp.firstTrack || track > s_pp.lastTrack) return 0;
    return s_pp.lengthFrame[track];
}

static uint32_t ppDiscRemaining(void) {
    if (!ppAudioReady()) return 0;
    uint32_t end = s_pp.discEndFrame;
    if (end == 0) {
        end = s_pp.startFrame[s_pp.lastTrack] + s_pp.lengthFrame[s_pp.lastTrack];
    }
    uint32_t cur = s_pp.startFrame[s_pp.curTrack] + s_pp.posFrame;
    return (end > cur) ? (end - cur) : 0;
}

static void ppCopyStr(char *dst, size_t dstSize, const char *src) {
    size_t i = 0;

    if (!dst || dstSize == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    while (src[i] && i + 1 < dstSize) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int ppFormatClut(uint8_t fmt) {
    switch (fmt) {
        case PP_FMT_MP3:  return PU20_CLUT_GREEN;
        case PP_FMT_WAV:  return PU20_CLUT_INDIGO;
        case PP_FMT_FLAC: return PU20_CLUT_PURPLE;
        case PP_FMT_OGG:  return PU20_CLUT_YELLOW;
        case PP_FMT_XA:   return PU20_CLUT_CYAN;
        case PP_FMT_DIR:  return PU20_CLUT_GOLD;
        default:          return PU20_CLUT_RED; /* CD-DA */
    }
}

static const char *ppFormatTag(uint8_t fmt) {
    switch (fmt) {
        case PP_FMT_MP3:  return "MP3";
        case PP_FMT_WAV:  return "WAV";
        case PP_FMT_FLAC: return "FLC";
        case PP_FMT_OGG:  return "OGG";
        case PP_FMT_XA:   return "XA";
        case PP_FMT_DIR:  return "DIR";
        default:          return "CDA";
    }
}

static int ppCatalogIndexForTrack(uint8_t track) {
    for (int i = 0; i < (int) s_pp.catalogCount; i++) {
        if (s_catalog[i].trackNum == track) return i;
    }
    return -1;
}

static void ppSyncListToTrack(uint8_t track) {
    int idx = ppCatalogIndexForTrack(track);

    if (idx < 0) return;
    s_pp.listCursor = (uint8_t) idx;
    if (s_pp.listCursor < s_pp.listScroll) {
        s_pp.listScroll = s_pp.listCursor;
    } else if (s_pp.listCursor >= s_pp.listScroll + PP_LIST_VISIBLE) {
        s_pp.listScroll = (uint8_t) (s_pp.listCursor - PP_LIST_VISIBLE + 1);
    }
}

/* Fallback catalogue when SD:/PicoPlayer/ has no tracks (e.g. physical CD-DA). */
static void ppBuildCatalog(void) {
    s_pp.catalogCount = 0;
    s_pp.listCursor   = 0;
    s_pp.listScroll   = 0;

    if (!ppHasAudioTracks()) return;

    for (int t = s_pp.firstTrack; t <= s_pp.lastTrack && s_pp.catalogCount < PP_MAX_TRACKS; t++) {
        PpTrackMeta *m;
        char fallback[16];
        if (s_pp.dataTrack[t]) continue;

        m = &s_catalog[s_pp.catalogCount++];
        memset(m, 0, sizeof(*m));
        m->trackNum = (uint8_t) t;

        fallback[0] = 'F'; fallback[1] = 'A'; fallback[2] = 'I';
        fallback[3] = 'X'; fallback[4] = 'A'; fallback[5] = ' ';
        fallback[6] = (char) ('0' + (t / 10));
        fallback[7] = (char) ('0' + (t % 10));
        fallback[8] = '\0';
        ppCopyStr(m->title,  sizeof(m->title),  fallback);
        ppCopyStr(m->artist, sizeof(m->artist), "CD DE \xC1UDIO");
        ppCopyStr(m->album,  sizeof(m->album),  "DISCO \xD3PTICO");
        m->format       = PP_FMT_CDDA;
        m->bitrateKbps  = 1411;
        m->sampleRateHz = 44100;
    }
}

static void ppSetSelection(uint8_t track) {
    if (track < s_pp.firstTrack) track = s_pp.firstTrack;
    if (track > s_pp.lastTrack)  track = s_pp.lastTrack;

    s_pp.selTrack = track;
    s_pp.page = (uint8_t)(((int)track - (int)s_pp.firstTrack) / PP_GRID_PAGE);
    s_pp.gridIndex = (uint8_t)(((int)track - (int)s_pp.firstTrack) % PP_GRID_PAGE);
    ppSyncListToTrack(track);
}

/* ------------------------------------------------------------------------- */
/* CD-ROM command layer                                                      */
/* ------------------------------------------------------------------------- */

static bool ppWait3(void) {
    waitForINT3();
    return waitingForInt5;
}

static bool ppCommand(uint8_t cmd, const uint8_t *arg, size_t argLength) {
    bool ok;

    /* Finish any in-flight GPU frame before blocking on a CD-ROM command so
     * the single-buffered 640x480i framebuffer never blinks mid-transfer. */
    waitForDMADone();
    waitForGP0Ready();
    issueCDROMCommand(cmd, arg, argLength);
    ok = ppWait3();
    vblank = false;
    return ok;
}

/* Wait for a command's second response (INT2) with a bounded timeout so real
 * PS1 hardware never sees a late PAUSE/STOP INT2 collide with the next command. */
static void ppWaitInt2(int maxMs) {
    int loops = maxMs * 10; /* 100 us per step */
    for (int i = 0; i < loops; i++) {
        if (!waitingForInt2 || !waitingForInt5) break;
        delayMicroseconds(100);
    }
    vblank = false;
}

static void ppWaitSecond(void) {
    ppWaitInt2(50);
}

static bool ppReadToc(void) {
    if (cdromBusy) return false;

    issueCDROMCommand(CDROM_CMD_GET_TN, NULL, 0);
    if (!ppWait3()) return false;

    if (cdromRespLength < 3) ppWaitSecond();

    if (cdromRespLength < 3) return false;

    uint8_t first = cdrom_decodeBCD(cdromResponse[1]);
    uint8_t last  = cdrom_decodeBCD(cdromResponse[2]);

    if (first < 1 || last < first || last > PP_MAX_TRACKS) return false;

    s_pp.firstTrack = first;
    s_pp.lastTrack  = last;

    for (int t = first; t <= last; t++) {
        uint8_t bcd = cdrom_encodeBCD((uint8_t)t);

        issueCDROMCommand(CDROM_CMD_GET_TD, &bcd, 1);
        if (!ppWait3()) return false;

        if (cdromRespLength < 3) ppWaitSecond();
        if (cdromRespLength < 3) return false;

        uint32_t mm = cdrom_decodeBCD(cdromResponse[1]);
        uint32_t ss = cdrom_decodeBCD(cdromResponse[2]);

        s_pp.startFrame[t] = (mm * 60u + ss) * PP_CD_FRAMES_PER_SEC;
        s_pp.dataTrack[t]  = false;
    }

    /* Lead-out: GETTD 00 returns the disc end on all retail drives. */
    {
        uint8_t zero = 0;
        issueCDROMCommand(CDROM_CMD_GET_TD, &zero, 1);
        if (ppWait3()) {
            if (cdromRespLength < 3) ppWaitSecond();
        }
        if (waitingForInt5 && cdromRespLength >= 3) {
            uint32_t mm = cdrom_decodeBCD(cdromResponse[1]);
            uint32_t ss = cdrom_decodeBCD(cdromResponse[2]);
            s_pp.discEndFrame = (mm * 60u + ss) * PP_CD_FRAMES_PER_SEC;
        } else {
            s_pp.discEndFrame = 0;
        }
    }

    for (int t = first; t <= last; t++) {
        uint32_t end;

        if (t < last) {
            end = s_pp.startFrame[t + 1];
        } else if (s_pp.discEndFrame > s_pp.startFrame[t]) {
            end = s_pp.discEndFrame;
        } else {
            end = s_pp.startFrame[t] + 4u * 60u * PP_CD_FRAMES_PER_SEC;
        }

        s_pp.lengthFrame[t] = (end > s_pp.startFrame[t])
                                  ? end - s_pp.startFrame[t] : 0;
    }

    return true;
}

/* Reads sector 16 of track 1 and checks for an ISO9660 / PS1 signature.
 * Covers both mixed-mode discs (track 1 data + CD-DA) and data-only discs
 * (track 1 data, 0 audio tracks). */
static bool ppProbeDataTrack1(void) {
    if (s_pp.lastTrack < 1) return false;

    uint32_t abs = s_pp.startFrame[s_pp.firstTrack];
    uint32_t lba = (abs > 150u) ? abs - 150u : 0u;
    lba += 16u;

    memset(s_probeBuf, 0, 32);
    startCDROMRead(lba, s_probeBuf, 1, 2048, true, true);
    if (!waitingForInt5) return false;

    if (memcmp(s_probeBuf + 1, "CD001", 5) == 0) return true;
    if (memcmp(s_probeBuf + 8, "PLAYSTATION", 11) == 0) return true;

    return false;
}

static void ppAudioEnable(bool enable) {
    if (enable) {
        SPU_CTRL = (uint16_t)(SPU_CTRL | SPU_CTRL_CDDA);
        SPU_CDDA_VOL_L = PP_CD_VOLUME;
        SPU_CDDA_VOL_R = PP_CD_VOLUME;
    } else {
        SPU_CDDA_VOL_L = 0;
        SPU_CDDA_VOL_R = 0;
        SPU_CTRL = (uint16_t)(SPU_CTRL & (uint16_t)~SPU_CTRL_CDDA);
    }
}

static bool ppSetLocAndPlay(uint32_t absFrame) {
    uint32_t lba = (absFrame > 150u) ? absFrame - 150u : 0u;
    CDROMMSF msf;
    uint8_t  mode = CDROM_MODE_CDDA;

    cdrom_convertLBAToMSF(&msf, lba);

    if (!ppCommand(CDROM_CMD_SETMODE, &mode, 1)) return false;
    if (!ppCommand(CDROM_CMD_SETLOC, (const uint8_t *)&msf, sizeof(msf))) {
        return false;
    }
    if (!ppCommand(CDROM_CMD_PLAY, NULL, 0)) return false;

    ppAudioEnable(true);
    return true;
}

/* ------------------------------------------------------------------------- */
/* Transport logic                                                           */
/* ------------------------------------------------------------------------- */

static uint8_t ppNextAudio(uint8_t from, int dir) {
    if (!s_pp.tocValid) return 0;

    int t = (int)from;

    for (int i = 0; i < PP_MAX_TRACKS + 1; i++) {
        t += dir;

        if (t > (int)s_pp.lastTrack)  t = (int)s_pp.firstTrack;
        if (t < (int)s_pp.firstTrack) t = (int)s_pp.lastTrack;

        if (!s_pp.dataTrack[t]) return (uint8_t)t;
    }

    return from;
}

static uint8_t ppRandomAudio(void) {
    int candidates[PP_MAX_TRACKS + 1];
    int count = 0;

    for (int t = s_pp.firstTrack; t <= s_pp.lastTrack; t++) {
        if (!s_pp.dataTrack[t]) {
            candidates[count++] = t;
        }
    }

    if (count == 0) return 0;

    s_pp.rng = s_pp.rng * 1103515245u + 12345u;
    return (uint8_t)candidates[(s_pp.rng >> 16) % (uint32_t)count];
}

static void ppPlayTrack(uint8_t track) {
    if (!ppHasAudioTracks()) {
        ppToast("SEM CD DE \xC1" "UDIO");
        return;
    }

    if (track < s_pp.firstTrack || track > s_pp.lastTrack) return;
    if (s_pp.dataTrack[track]) {
        ppToast("FAIXA DE DADOS");
        return;
    }

    uint32_t abs = s_pp.startFrame[track];

    /* Cleanly pause any active CD-DA stream before issuing SETLOC+PLAY so
     * rapid track switching never collides with the PS1 HC05 Mechacon. */
    if (s_pp.playing && !s_pp.paused) {
        if (ppCommand(CDROM_CMD_PAUSE, NULL, 0)) {
            ppWaitInt2(250);
        }
    }

    if (!ppSetLocAndPlay(abs)) {
        /* One recovery attempt after a clean PAUSE in case a prior seek raced */
        if (ppCommand(CDROM_CMD_PAUSE, NULL, 0)) {
            ppWaitInt2(250);
        }
        delayMicroseconds(10000);
        if (!ppSetLocAndPlay(abs)) {
            s_pp.playing = false;
            ppToast("ERRO DE CD");
            return;
        }
    }

    s_pp.curTrack  = track;
    s_pp.posFrame  = 0;
    s_pp.playing   = true;
    s_pp.paused    = false;
    s_pp.tickAcc   = 0;
    s_pp.pollCounter = 0;
    ppSetSelection(track);
}

static void ppResume(void) {
    if (!ppAudioReady()) return;

    uint32_t abs = s_pp.startFrame[s_pp.curTrack] + s_pp.posFrame;

    if (!ppSetLocAndPlay(abs)) {
        ppToast("ERRO DE CD");
        return;
    }

    s_pp.playing = true;
    s_pp.paused  = false;
}

static void ppPause(void) {
    if (!s_pp.playing || s_pp.paused) return;

    if (ppCommand(CDROM_CMD_PAUSE, NULL, 0)) {
        ppWaitInt2(250);
        s_pp.paused = true;
    }
}

static void ppStop(void) {
    if (s_pp.playing || s_pp.paused) {
        if (ppCommand(CDROM_CMD_STOP, NULL, 0)) {
            ppWaitInt2(250);
        }
    }

    s_pp.playing = false;
    s_pp.paused  = false;
    s_pp.posFrame = 0;
    ppAudioEnable(false);

    if (s_pp.tocValid && s_pp.curTrack >= s_pp.firstTrack) {
        uint32_t abs = s_pp.startFrame[s_pp.curTrack];
        uint32_t lba = (abs > 150u) ? abs - 150u : 0u;
        CDROMMSF msf;
        cdrom_convertLBAToMSF(&msf, lba);
        ppCommand(CDROM_CMD_SETLOC, (const uint8_t *)&msf, sizeof(msf));
    }

    ppToast("PARAR");
}

static void ppSeekRelative(int seconds) {
    if (!ppAudioReady()) return;

    int32_t target = (int32_t)s_pp.posFrame + seconds * 75;
    uint32_t length = ppTrackLength(s_pp.curTrack);

    if (target < 0) target = 0;
    if (length && target > (int32_t)length - 75) {
        target = (int32_t)length - 75;
    }
    if (target < 0) target = 0;

    s_pp.posFrame = (uint32_t)target;

    uint32_t abs = s_pp.startFrame[s_pp.curTrack] + s_pp.posFrame;

    if (s_pp.playing && !s_pp.paused) {
        /* SETLOC is only reliable after PAUSE completes (INT3 + INT2). */
        if (ppCommand(CDROM_CMD_PAUSE, NULL, 0)) {
            ppWaitInt2(250);
        }
        if (ppSetLocAndPlay(abs)) {
            ppToast(seconds < 0 ? "RETROCEDER" : "AVAN\xC7" "AR");
        }
    } else {
        uint32_t lba = (abs > 150u) ? abs - 150u : 0u;
        CDROMMSF msf;
        cdrom_convertLBAToMSF(&msf, lba);
        ppCommand(CDROM_CMD_SETLOC, (const uint8_t *)&msf, sizeof(msf));
        ppToast(seconds < 0 ? "RETROCEDER" : "AVAN\xC7" "AR");
    }
}

static void ppAdvance(void) {
    if (!s_pp.tocValid) return;

    if (s_pp.repeatMode == PP_REPEAT_ONE) {
        ppPlayTrack(s_pp.curTrack);
        return;
    }

    uint8_t next;

    if (s_pp.shuffle) {
        next = ppRandomAudio();
    } else if (s_pp.program && s_pp.programCount > 0) {
        int index = -1;
        for (int i = 0; i < s_pp.programCount; i++) {
            if (s_pp.programOrder[i] == s_pp.curTrack) {
                index = i;
                break;
            }
        }

        if (index < 0) {
            index = 0;
        } else {
            index++;
        }

        if (index >= s_pp.programCount) {
            if (s_pp.repeatMode != PP_REPEAT_ALL) {
                ppStop();
                return;
            }
            index = 0;
        }
        next = s_pp.programOrder[index];
    } else {
        next = ppNextAudio(s_pp.curTrack, 1);

        if (next <= s_pp.curTrack) {
            if (s_pp.repeatMode != PP_REPEAT_ALL) {
                ppStop();
                return;
            }
        }
    }

    if (next != 0) ppPlayTrack(next);
}

static void ppManualNext(int dir) {
    if (!ppHasAudioTracks()) {
        ppToast("SEM CD DE \xC1" "UDIO");
        return;
    }

    uint8_t from = ppAudioReady() ? s_pp.curTrack : s_pp.selTrack;
    uint8_t next = ppNextAudio(from, dir);

    if (next != 0) ppPlayTrack(next);
}

static void ppSetTrack(uint8_t track) {
    if (s_pp.dataTrack[track]) return;
    if (s_pp.playing && !s_pp.paused && track == s_pp.curTrack) {
        ppPause();
    } else if (track == s_pp.curTrack && s_pp.paused) {
        ppResume();
        ppSetSelection(track);
    } else {
        ppPlayTrack(track);
    }
}

static void ppProgramRebuild(void) {
    s_pp.programCount = 0;

    for (int t = s_pp.firstTrack; t <= s_pp.lastTrack; t++) {
        if (!s_pp.dataTrack[t] && s_pp.programmed[t]) {
            s_pp.programOrder[s_pp.programCount++] = (uint8_t)t;
        }
    }
}

static void ppProgramEnable(void) {
    bool any = false;

    for (int t = s_pp.firstTrack; t <= s_pp.lastTrack; t++) {
        if (s_pp.programmed[t]) {
            any = true;
            break;
        }
    }

    if (!any) {
        for (int t = s_pp.firstTrack; t <= s_pp.lastTrack; t++) {
            s_pp.programmed[t] = !s_pp.dataTrack[t];
        }
    }

    ppProgramRebuild();
}

static void ppToggleProgramTrack(uint8_t track) {
    if (track < s_pp.firstTrack || track > s_pp.lastTrack) return;
    if (s_pp.dataTrack[track]) return;

    s_pp.programmed[track] = !s_pp.programmed[track];
    ppProgramRebuild();

    ppToast(s_pp.programmed[track] ? "ADICIONADA" : "REMOVIDA");
}

/* ------------------------------------------------------------------------- */
/* Position, clock and scope capture                                         */
/* ------------------------------------------------------------------------- */

static bool ppPollPosition(void) {
    if (!ppAudioReady()) return false;

    /* GETLOC_P returns only INT3 (8 bytes), NEVER INT2. Use ppCommand() so we
     * wait cleanly for INT3 (< 1 ms) without a 50 ms INT2 timeout stall! */
    if (!ppCommand(CDROM_CMD_GETLOC_P, NULL, 0)) return false;
    if (cdromRespLength < 8) return false;

    int base = (cdromRespLength >= 9) ? 1 : 0;

    uint32_t abs = ((uint32_t)cdrom_decodeBCD(cdromResponse[base + 5]) * 60u +
                    (uint32_t)cdrom_decodeBCD(cdromResponse[base + 6])) *
                       PP_CD_FRAMES_PER_SEC +
                   (uint32_t)cdrom_decodeBCD(cdromResponse[base + 7]);

    if (abs < s_pp.startFrame[s_pp.curTrack]) return false;

    uint32_t rel = abs - s_pp.startFrame[s_pp.curTrack];

    if (rel < ppTrackLength(s_pp.curTrack) + PP_CD_FRAMES_PER_SEC) {
        s_pp.posFrame = rel;
        return true;
    }

    return false;
}

static void ppTick(void) {
    s_pp.frame++;

    if (!s_pp.playing || s_pp.paused) return;

    uint32_t step = (PP_CD_FRAMES_PER_SEC << 16) / s_pp.fps;
    s_pp.tickAcc += step;
    s_pp.posFrame += s_pp.tickAcc >> 16;
    s_pp.tickAcc &= 0xFFFFu;

    uint32_t length = ppTrackLength(s_pp.curTrack);

    if (length && s_pp.posFrame >= length) {
        ppAdvance();
        return;
    }

    s_pp.pollCounter++;

    if (s_pp.pollCounter >= 45u) {
        s_pp.pollCounter = 0;
        ppPollPosition();
    }
}

static int16_t ppSampleAt(const uint8_t *data, int index) {
    int offset = index * 2;
    return (int16_t)((uint16_t)data[offset] |
                     ((uint16_t)data[offset + 1] << 8));
}

static void ppReadCapture(void) {
    if (!s_pp.playing || s_pp.paused) {
        for (int i = 0; i < PP_SCOPE_SAMPLES; i++) {
            s_pp.scopeL[i] = 0;
            s_pp.scopeR[i] = 0;
        }
        s_pp.levelL = (uint8_t)(s_pp.levelL > 1 ? s_pp.levelL - 1 : 0);
        s_pp.levelR = (uint8_t)(s_pp.levelR > 1 ? s_pp.levelR - 1 : 0);
        return;
    }

    /* SPU_STAT bit 11 (CAPTURE_BUF) tells which half the SPU is currently
     * writing: read the OPPOSITE (completed) half to avoid waveform tearing. */
    uint32_t half = (SPU_STAT & SPU_STAT_CAPTURE_BUF) ? 0x000u : 0x200u;

    download(0x0000u + half, s_captureBuf, 0x200, true);
    download(0x0400u + half, s_captureBuf + 0x200, 0x200, true);

    int peakL = 0;
    int peakR = 0;

    /* Store all 256 stereo samples from the SPU capture window while measuring
     * the peak over the whole window for reactive SoundScope modes. */
    for (int i = 0; i < 256; i++) {
        int16_t l = ppSampleAt(s_captureBuf, i);
        int16_t r = ppSampleAt(s_captureBuf + 0x200, i);

        int al = l < 0 ? -l : l;
        int ar = r < 0 ? -r : r;

        if (al > peakL) peakL = al;
        if (ar > peakR) peakR = ar;

        s_pp.scopeL[i] = l;
        s_pp.scopeR[i] = r;
    }

    int levelL = peakL >> 11;
    int levelR = peakR >> 11;

    if (levelL > 15) levelL = 15;
    if (levelR > 15) levelR = 15;

    s_pp.levelL = (uint8_t)levelL;
    s_pp.levelR = (uint8_t)levelR;
}

/* 24 distinct Goertzel coefficients: round(2 * cos(2*pi*k/128) * 4096) for
 * quasi-logarithmic bins k = 1..54 across 170 Hz .. 18.6 kHz (N=128 @ 22.05kHz). */
static const int16_t kBandCoeff[PP_SPECTRUM_BANDS] = {
    8182, 8153, 8103, 8034, 7945, 7837, 7710, 7564,
    7327, 7054, 6749, 6411, 5957, 5457, 4916, 4337,
    3618, 2855, 2057, 1235,  295, -650, -1978, -3270
};

static uint32_t ppISqrtU32(uint32_t n);

static void ppComputeSpectrum(void) {
    int mono[128];

    for (int i = 0; i < 128; i++) {
        mono[i] = ((int)s_pp.scopeL[i * 2] + (int)s_pp.scopeR[i * 2]) >> 5;
    }

    for (int b = 0; b < PP_SPECTRUM_BANDS; b++) {
        int32_t s0 = 0, s1 = 0, s2 = 0;
        int32_t coeff = kBandCoeff[b];

        for (int i = 0; i < 128; i++) {
            s0 = mono[i] + ((coeff * s1) >> 12) - s2;
            s2 = s1;
            s1 = s0;
        }

        /* Phase-independent Goertzel magnitude with pink-noise treble tilt
         * so all 24 frequency bands dance vibrantly on real music! */
        int32_t a1 = s1 >> 5;
        int32_t a2 = s2 >> 5;
        if (a1 > 18000) a1 = 18000; else if (a1 < -18000) a1 = -18000;
        if (a2 > 18000) a2 = 18000; else if (a2 < -18000) a2 = -18000;
        int32_t pwr = (a1 * a1) + (a2 * a2) - (((coeff * a1) >> 12) * a2);
        if (pwr < 0) pwr = 0;
        uint32_t mag = ppISqrtU32((uint32_t) pwr) >> 1;
        mag = (mag * (16u + (uint32_t) b)) >> 4;

        if (mag > 120u) mag = 120u;

        uint8_t value = (uint8_t)mag;
        uint8_t decay = (uint8_t) (s_pp.scopeSpeed >= 1 ? s_pp.scopeSpeed : 1);

        if (value > s_pp.spectrum[b]) {
            /* Faster attack when scopeSpeed is higher, smoother when lower */
            int diff = (int) value - (int) s_pp.spectrum[b];
            int step = (diff * (int) decay + 3) / 4;
            if (step < 1) step = 1;
            if (step > diff) step = diff;
            s_pp.spectrum[b] = (uint8_t) (s_pp.spectrum[b] + step);
        } else if (s_pp.spectrum[b] > 0) {
            int fall = (decay + 1) / 2;
            s_pp.spectrum[b] = (s_pp.spectrum[b] > fall)
                                   ? (uint8_t) (s_pp.spectrum[b] - fall) : 0;
        }

        if (s_pp.spectrum[b] > s_pp.peak[b]) {
            s_pp.peak[b] = s_pp.spectrum[b];
        } else if (s_pp.peak[b] > 0) {
            s_pp.peak[b]--;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* CD Player UI — real PU-20 BIOS layout, rendered 1:1 at 640x480i.          */
/* Video mode: the BIOS player runs at 640x480i; PicoMenu runs at 320x240p.  */

static void ppVramClear(int x, int y, int w, int h,
                        uint8_t r, uint8_t g, uint8_t b) {
    waitForDMADone();
    waitForGP0Ready();
    GPU_GP0 = gp0_vramFill() | gp0_rgb(r, g, b);
    GPU_GP0 = gp0_xy(x, y);
    GPU_GP0 = gp0_xy(w, h);
    waitForGP0Ready();
}

static void ppSetVideoMode(bool wide, bool unblank) {
    bool pal = ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL);
    int centerX = 0x760;
    int centerY = pal ? 0xA3 : 0x88;
    int resW = wide ? 640 : 320;
    int resH = wide ? 480 : 240;
    GP1HorizontalRes hr = wide ? GP1_HRES_640 : GP1_HRES_320;
    GP1VerticalRes   vr = wide ? GP1_VRES_512 : GP1_VRES_256;
    int offX = (resW * (int) gp1_clockMultiplierH(hr)) / 2;
    int offY = (resH / (int) gp1_clockDividerV(vr)) / 2;

    waitForDMADone();
    waitForGP0Ready();

    /* Keep display blanked while switching resolution and wiping old buffers
     * so previous 320x240 double-buffers never flash on the 640x480i screen. */
    GPU_GP1 = gp1_dispBlank(true);
    GPU_GP1 = gp1_resetGPU();
    GPU_GP1 = gp1_dispBlank(true);
    GPU_GP1 = gp1_fbRangeH(centerX - offX, centerX + offX);
    GPU_GP1 = gp1_fbRangeV(centerY - offY, centerY + offY);
    GPU_GP1 = gp1_fbMode(hr, vr, pal ? GP1_MODE_PAL : GP1_MODE_NTSC, wide,
                         GP1_COLOR_16BPP);
    GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
    GPU_GP1 = gp1_fbOffset(0, 0);

    ppVramClear(0, 0, 640, wide ? 480 : 240, 0, 0, 32);

    GPU_GP0 = (0xE3u << 24) | 0u;
    GPU_GP0 = (0xE4u << 24) | (uint32_t) (resW - 1) |
              ((uint32_t) (resH - 1) << 10);
    GPU_GP0 = (0xE5u << 24) | 0u;
    GPU_GP0 = gp0_texpage(0, false, true);

    if (unblank) {
        GPU_GP1 = gp1_dispBlank(false);
    }
}

/* True double-buffered 320x240p (non-interlaced, authentic Sony PU-20 BIOS
 * SoundScope video mode!):
 *   Buffer 0 = (  0, 0, 320, 240)
 *   Buffer 1 = (320, 0, 320, 240)
 * Gives 100% vivid full color on Composite/S-Video/RGB TVs (no 640px NTSC
 * chroma cancellation), 1:1 square pixels, zero top-quarter beam cutoffs,
 * 2x faster phosphor trail blits, and keeps VRAM x>=640 untouched! */
static void ppSetScopeVideoMode(void) {
    bool pal = ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL);
    int centerX = 0x760;
    int centerY = pal ? 0xA3 : 0x88;
    int offX = (320 * (int) gp1_clockMultiplierH(GP1_HRES_320)) / 2;
    int offY = (240 / (int) gp1_clockDividerV(GP1_VRES_256)) / 2;

    waitForDMADone();
    waitForGP0Ready();

    GPU_GP1 = gp1_dispBlank(true);
    GPU_GP1 = gp1_resetGPU();
    GPU_GP1 = gp1_dispBlank(true);
    GPU_GP1 = gp1_fbRangeH(centerX - offX, centerX + offX);
    GPU_GP1 = gp1_fbRangeV(centerY - offY, centerY + offY);
    GPU_GP1 = gp1_fbMode(GP1_HRES_320, GP1_VRES_256,
                         pal ? GP1_MODE_PAL : GP1_MODE_NTSC, false,
                         GP1_COLOR_16BPP);
    GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
    GPU_GP1 = gp1_fbOffset(0, 0);

    ppVramClear(0, 0, 640, 240, 0, 0, 0);

    GPU_GP0 = gp0_fbOffset1(0, 0);
    GPU_GP0 = gp0_fbOffset2(319, 239);
    GPU_GP0 = gp0_fbOrigin(0, 0);
    GPU_GP0 = gp0_texpage(0, false, true);
    GPU_GP1 = gp1_dispBlank(false);
}

/* ------------------------------------------------------------------------- */

#define PP_TRANSPORT_COUNT 7
#define PP_TRANSPORT_X     58

#define PP_GRID_X0         400
#define PP_GRID_Y0         134
#define PP_GRID_DX         48
#define PP_GRID_DY         48
#define PP_GRID_R          20

#define PP_PANEL_TITLE_X   452
#define PP_PANEL_TITLE_Y   44
#define PP_PANEL_TITLE_W   172
#define PP_PANEL_TITLE_H   47

static const uint16_t kTransportY[PP_TRANSPORT_COUNT] = {
    66, 123, 180, 237, 294, 351, 408
};

static const uint8_t kTransportR[PP_TRANSPORT_COUNT] = {
    22, 22, 28, 22, 22, 22, 22
};

static const uint8_t kTransportClut[PP_TRANSPORT_COUNT] = {
    PU20_CLUT_GREEN,
    PU20_CLUT_GREEN,
    PU20_CLUT_INDIGO,
    PU20_CLUT_RED,
    PU20_CLUT_GOLD,
    PU20_CLUT_YELLOW,
    PU20_CLUT_YELLOW
};

static const Pu20Rgb ppWhite = { 255, 255, 255 };

static void ppSolid(DMAChain *chain, int x, int y, int w, int h, Pu20Rgb c) {
    RgbColor rgb;

    rgb.r = c.r;
    rgb.g = c.g;
    rgb.b = c.b;
    rc5DrawSolidRect(chain, x, y, w, h, rgb);
}

static int ppTrackClut(int track) {
    /* Exact 20-cell PU-20 BIOS palette verified from bios_cd_vazio.sav &
     * 01_bios_real_cdplayer.png:
     *   1,2,6,7 = gold | 3,4,8,13 = yellow | 5,9,10,14 = green |
     *   11,12 = red | 16,17 = purple | 15,18,19,20 = indigo */
    static const uint8_t map20[20] = {
        PU20_CLUT_GOLD,   PU20_CLUT_GOLD,   PU20_CLUT_YELLOW, PU20_CLUT_YELLOW, PU20_CLUT_GREEN,
        PU20_CLUT_GOLD,   PU20_CLUT_GOLD,   PU20_CLUT_YELLOW, PU20_CLUT_GREEN,  PU20_CLUT_GREEN,
        PU20_CLUT_RED,    PU20_CLUT_RED,    PU20_CLUT_YELLOW, PU20_CLUT_GREEN,  PU20_CLUT_INDIGO,
        PU20_CLUT_PURPLE, PU20_CLUT_PURPLE, PU20_CLUT_INDIGO, PU20_CLUT_INDIGO, PU20_CLUT_INDIGO
    };

    if (track < 1) track = 1;
    return map20[(track - 1) % 20];
}

static int ppGridTrack(int index) {
    return (int) s_pp.firstTrack + index;
}

static void ppCentered(DMAChain *chain, const TextureInfo *font, int cx,
                       int y, const char *text, Pu20Rgb color) {
    int width = rc5MeasureString(text);
    int x     = cx - width / 2;

    pu20DrawTextShadow(chain, font, x, y, text, 640, color);
}

static void ppDrawDigits(DMAChain *chain, int cx, int y, const char *text) {
    int x = cx - pu20FontWidth(1, text) / 2;

    pu20DrawFont(chain, 1, x, y, text, ppWhite);
}

static void ppDrawCursor(DMAChain *chain, int tipX, int tipY) {
    pu20DrawShellCursor(chain, tipX, tipY);
}

static void ppCenteredFont(DMAChain *chain, int size, int cx, int y,
                           const char *text) {
    int w = pu20FontWidth(size, text);

    pu20DrawFont(chain, size, cx - w / 2, y, text, ppWhite);
}

static uint32_t ppDisplayFrames(void) {
    uint32_t length = ppTrackLength(s_pp.curTrack);
    uint32_t value  = s_pp.posFrame;

    if (s_pp.timeMode == 1) {
        value = (length > s_pp.posFrame) ? (length - s_pp.posFrame) : 0;
    } else if (s_pp.timeMode == 2) {
        value = ppDiscRemaining();
    }

    if (value > 99u * 60u * PP_CD_FRAMES_PER_SEC) {
        value = 99u * 60u * PP_CD_FRAMES_PER_SEC;
    }
    return value;
}

static void ppDrawDisplay(DMAChain *chain, const TextureInfo *font) {
    (void) font;
    uint32_t value = ppDisplayFrames();
    int seconds    = (int) ((value / PP_CD_FRAMES_PER_SEC) % 60u);
    int minutes    = (int) ((value / PP_CD_FRAMES_PER_SEC) / 60u);
    int track      = s_pp.curTrack;
    char buffer[4];

    if (s_pp.keypad) {
        if (s_pp.entryCount == 0) {
            buffer[0] = '-';
            buffer[1] = '-';
            buffer[2] = '\0';
        } else {
            buffer[0] = (char) ('0' + (s_pp.entry % 10));
            buffer[1] = '-';
            buffer[2] = '\0';
        }
    } else {
        int catIdx = ppCatalogIndexForTrack(s_pp.curTrack);
        track = (catIdx >= 0) ? (catIdx + 1) : 0;
        if (track > 99) track = 99;
        if (track >= 10) {
            buffer[0] = (char) ('0' + track / 10);
            buffer[1] = (char) ('0' + track % 10);
            buffer[2] = '\0';
        } else {
            buffer[0] = (char) ('0' + track);
            buffer[1] = '\0';
        }
    }

    bool hasAudio = ppHasAudioTracks();

    /* Track sphere (128, 95) r=26 */
    pu20DrawSphereClut(chain, 128, 95, 26, PU20_CLUT_INDIGO);
    ppDrawDigits(chain, 128, 86, buffer);

    /* Golden 3D minus sign: shown ONLY when counting remaining track or disc
     * time (timeMode 1 or 2), hidden during positive elapsed time (timeMode 0). */
    if (hasAudio && (s_pp.timeMode == 1 || s_pp.timeMode == 2)) {
        pu20DrawSphereQuad(chain, 171, 91, 195, 97, PU20_CLUT_GOLD);
    }

    /* Minutes sphere (225, 95) r=26 (blank when no audio disc, matching BIOS) */
    pu20DrawSphereClut(chain, 225, 95, 26, PU20_CLUT_INDIGO);
    if (hasAudio) {
        buffer[0] = (char) ('0' + minutes / 10);
        buffer[1] = (char) ('0' + minutes % 10);
        buffer[2] = '\0';
        ppDrawDigits(chain, 225, 86, (minutes < 10) ? buffer + 1 : buffer);
    }

    /* MIN small sphere overlapping bottom-left of Minutes sphere */
    pu20DrawSphereClut(chain, 196, 122, 17, PU20_CLUT_INDIGO);
    ppCenteredFont(chain, 0, 196, 117, "MIN");

    /* Golden 3D dot between Minutes and Seconds (262, 115) r=6 */
    pu20DrawSphereClut(chain, 262, 115, 6, PU20_CLUT_GOLD);

    /* Seconds sphere (300, 95) r=26 (blank when no audio disc, matching BIOS) */
    pu20DrawSphereClut(chain, 300, 95, 26, PU20_CLUT_INDIGO);
    if (hasAudio) {
        buffer[0] = (char) ('0' + seconds / 10);
        buffer[1] = (char) ('0' + seconds % 10);
        buffer[2] = '\0';
        ppDrawDigits(chain, 300, 86, buffer);
    }

    /* SEG small sphere overlapping bottom-right of Seconds sphere (pt-BR) */
    pu20DrawSphereClut(chain, 328, 122, 17, PU20_CLUT_INDIGO);
    ppCenteredFont(chain, 0, 328, 117, "SEG");
}

static void ppDrawTransportRow(DMAChain *chain) {
    /* BIOS column order: NEXT / FF / PLAY / STOP / PAUSE / REW / PREV.
     * The active transport state enlarges to r=28 (STOP when stopped, PLAY
     * when playing, PAUSE when paused), matching real PU-20 BIOS behaviour. */
    static const uint8_t iconMap[PP_TRANSPORT_COUNT] = {
        PU20_ICON_NEXT,
        PU20_ICON_FF,
        PU20_ICON_PLAY,
        PU20_ICON_STOP,
        PU20_ICON_PAUSE,
        PU20_ICON_REW,
        PU20_ICON_PREV
    };
    int activeIdx = s_pp.playing ? (s_pp.paused ? 4 : 2) : 3;

    for (int i = 0; i < PP_TRANSPORT_COUNT; i++) {
        int r = (i == activeIdx) ? 28 : 22;
        pu20DrawSphereClut(chain, PP_TRANSPORT_X, kTransportY[i],
                           r, kTransportClut[i]);
        pu20DrawTransportIconScaled(chain, iconMap[i], PP_TRANSPORT_X,
                                    kTransportY[i], i == activeIdx);
    }
    (void) kTransportR;
}

static bool ppTrackHasBigSphere(int track) {
    if (s_pp.dataTrack[track]) return false;
    if (s_pp.program) return s_pp.programmed[track];
    /* Keep every audio track's numbered 3D sphere visible in the SD list
     * (never shrink earlier tracks when jumping ahead; only the playing
     * track replaces its number with '>'). */
    return true;
}

static void ppDrawTrackBall(DMAChain *chain, int cx, int cy, int track,
                            bool ballVisible) {
    char num[3];

    if (track >= 10) {
        num[0] = (char) ('0' + (track / 10));
        num[1] = (char) ('0' + (track % 10));
        num[2] = '\0';
    } else {
        num[0] = (char) ('0' + track);
        num[1] = '\0';
    }

    if (ballVisible) {
        pu20DrawSphereClut(chain, cx, cy, PP_GRID_R, ppTrackClut(track));
    } else {
        /* Small 3D gold/red status dot behind un-sphered tracks (PU-20 style). */
        int dotClut = (s_pp.dataTrack[track] || track == (int) s_pp.curTrack - 1)
                          ? PU20_CLUT_RED : PU20_CLUT_GOLD;
        pu20DrawSphereClut(chain, cx, cy, 5, dotClut);
    }
    ppDrawDigits(chain, cx, cy - 9, num);
}

static void ppFormatDec2(char *out, int val) {
    if (val < 0) val = 0;
    if (val > 99) val = 99;
    out[0] = (char) ('0' + (val / 10));
    out[1] = (char) ('0' + (val % 10));
    out[2] = '\0';
}

static int ppProgramPos(uint8_t track) {
    for (int i = 0; i < (int) s_pp.programCount; i++) {
        if (s_pp.programOrder[i] == track) return i + 1;
    }
    return 0;
}

static void ppDrawGrid(DMAChain *chain, const TextureInfo *font) {
    /* Evolved PU-20 right-hand panel: scrollable SD:/PicoPlayer/ song list
     * with 3D Phong spheres ahead of every song name (coloured by format,
     * dynamic full/small sphere size matching PU-20 BIOS semantics) plus
     * compact format badges (MP3/WAV/FLC/OGG/XA/CDA) and the page sphere. */
    const int listX     = 364;
    const int listW     = 262;
    const int listY0    = 106;
    const int rowH      = 28;
    const int sphereCx  = 379;
    const int titleX    = 396;
    const int titleMaxX = 576;
    const int badgeX    = 582;
    const int badgeW    = 40;
    bool hasAudio       = (s_pp.catalogCount > 0);

    (void) font;
    (void) ppDrawTrackBall;

    /* Self-contained dark PU-20 panel for the 8-track list so scrolling/cursor
     * updates redraw ONLY this 268x224 box (< 0.4 ms, inside VBlank!) without
     * ever touching the outer concentric rings! */
    {
        Pu20Rgb boxBg  = { 10, 14, 52 };
        Pu20Rgb boxBrd = { 65, 65, 139 };
        ppSolid(chain, 360, 102, 268, 224, boxBg);
        ppSolid(chain, 360, 102, 268, 1,   boxBrd);
        ppSolid(chain, 360, 325, 268, 1,   boxBrd);
        ppSolid(chain, 360, 102, 1,   224, boxBrd);
        ppSolid(chain, 627, 102, 1,   224, boxBrd);
    }

    for (int row = 0; row < PP_LIST_VISIBLE; row++) {
        int rowY = listY0 + row * rowH;
        int cy   = rowY + 12;
        int idx  = (int) s_pp.listScroll + row;

        if (!hasAudio || idx >= (int) s_pp.catalogCount) {
            /* Authentic PU-20 empty slot dot (r=4) */
            pu20DrawSphereClut(chain, sphereCx, cy, 4, ppTrackClut(row + 1));
            continue;
        }

        const PpTrackMeta *m = &s_catalog[idx];
        bool focused   = (s_pp.focus == PP_FOCUS_GRID && (int) s_pp.listCursor == idx);
        bool isCurrent = (m->trackNum == s_pp.curTrack);
        bool isPlaying = (isCurrent && s_pp.playing && !s_pp.paused);
        bool bigBall   = isCurrent || focused || ppTrackHasBigSphere(m->trackNum);
        int  clut      = ppFormatClut(m->format);
        Pu20Rgb rowBg  = { 65, 65, 139 };
        Pu20Rgb bdgCol = isCurrent ? (Pu20Rgb){ 88, 92, 190 }
                                   : (Pu20Rgb){ 56, 58, 128 };
        const char *tag = ppFormatTag(m->format);

        if (focused) {
            pu20DrawSemiRect(chain, listX, rowY, listW, 24, rowBg);
        }

        /* 3D Phong sphere ahead of the track name (full r=11 or small dot r=4) */
        if (bigBall) {
            char num[3];
            int showNum = idx + 1;
            if (s_pp.program) {
                int qpos = ppProgramPos(m->trackNum);
                if (qpos > 0) showNum = qpos;
            }
            pu20DrawSphereClut(chain, sphereCx, cy, 11, clut);
            if (isPlaying) {
                ppCenteredFont(chain, 0, sphereCx + 1, cy - 5, ">");
            } else if (showNum >= 10) {
                num[0] = (char) ('0' + (showNum / 10));
                num[1] = (char) ('0' + (showNum % 10));
                num[2] = '\0';
                ppCenteredFont(chain, 0, sphereCx, cy - 5, num);
            } else {
                num[0] = (char) ('0' + showNum);
                num[1] = '\0';
                ppCenteredFont(chain, 0, sphereCx, cy - 5, num);
            }
        } else {
            pu20DrawSphereClut(chain, sphereCx, cy, 4, clut);
        }

        /* Track title / ID3 tag (with gentle horizontal marquee on focused row) */
        {
            const char *txt = m->title;
            int len = (int) strlen(txt);
            int fullW = pu20FontWidth(0, txt);
            int availW = titleMaxX - titleX;
            Pu20Rgb titleCol = isCurrent ? (Pu20Rgb){ 255, 244, 160 } : ppWhite;

            /* Quick BIOS-style fade-to-black -> back-to-white click pulse on
             * the song name (10 frames total: 10..6 darkens, 5..1 restores). */
            if (s_pp.trackClickTimer > 0 && s_pp.trackClickIdx == idx) {
                int t = (int) s_pp.trackClickTimer;
                int factor = (t > 5) ? ((t - 5) * 255) / 5
                                     : ((5 - t) * 255) / 5;
                titleCol.r = (uint8_t) (((int) titleCol.r * factor) / 255);
                titleCol.g = (uint8_t) (((int) titleCol.g * factor) / 255);
                titleCol.b = (uint8_t) (((int) titleCol.b * factor) / 255);
            }

            if (focused && fullW > availW && len > 12) {
                int step = (int) ((s_pp.frame / 14u) % (uint32_t) (len + 4));
                if (step > 3 && step < len - 6) {
                    txt += (step - 3);
                }
            }
            pu20DrawFontClippedClean(chain, 0, titleX, rowY + 7, titleMaxX,
                                     txt, titleCol);
        }

        /* Compact right-aligned format badge (MP3 / WAV / FLC / OGG / XA / CDA) */
        ppSolid(chain, badgeX, rowY + 4, badgeW, 16, bdgCol);
        pu20DrawFontClean(chain, 0, badgeX + (badgeW - pu20FontWidth(0, tag)) / 2, rowY + 6, tag, ppWhite);
    }

    /* List footer: classic PU-20 bottom-right indigo sphere showing current
     * page (e.g. "1/3"), blank when folder is empty. Keep SELECT Easter egg
     * hidden just like the original PS1 BIOS! */
    if (hasAudio) {
        int totalPages = ((int) s_pp.catalogCount + PP_LIST_VISIBLE - 1) / PP_LIST_VISIBLE;
        int curPage    = ((int) s_pp.listScroll / PP_LIST_VISIBLE) + 1;
        char pgStr[8];

        if (curPage > totalPages) curPage = totalPages;
        pgStr[0] = (char) ('0' + (curPage % 10));
        pgStr[1] = '/';
        pgStr[2] = (char) ('0' + (totalPages % 10));
        pgStr[3] = '\0';

        pu20DrawSphereClut(chain, 594, 344, 21, PU20_CLUT_INDIGO);
        ppCenteredFont(chain, 0, 594, 338, pgStr);
    } else {
        pu20DrawSphereClut(chain, 594, 344, 21, PU20_CLUT_INDIGO);
    }
}

static void ppDrawKeypad(DMAChain *chain, const TextureInfo *font) {
    static const int layout[PP_KEYPAD_DIGITS] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };
    static const uint8_t keyClut[PP_KEYPAD_DIGITS] = {
        PU20_CLUT_GOLD, PU20_CLUT_GOLD, PU20_CLUT_YELLOW, PU20_CLUT_YELLOW,
        PU20_CLUT_GREEN, PU20_CLUT_GOLD, PU20_CLUT_GOLD, PU20_CLUT_YELLOW,
        PU20_CLUT_GREEN, PU20_CLUT_GREEN
    };

    for (int i = 0; i < PP_KEYPAD_DIGITS; i++) {
        int col = i % 5;
        int row = i / 5;
        int x   = PP_GRID_X0 + col * PP_GRID_DX;
        int y   = PP_GRID_Y0 + row * PP_GRID_DY;
        char num[2];

        num[0] = (char) ('0' + layout[i]);
        num[1] = '\0';

        pu20DrawSphereClut(chain, x, y, PP_GRID_R, keyClut[i]);
        ppDrawDigits(chain, x, y - 9, num);
    }

    /* Show subtle status indicators for tracks >20 present on disc (up to 99). */
    for (int i = 0; i < 10; i++) {
        int track = 21 + i;
        int x = PP_GRID_X0 + (i % 5) * PP_GRID_DX;
        int y = 230 + (i / 5) * 48;

        if (track > (int) s_pp.lastTrack) break;
        pu20DrawSphereClut(chain, x, y, 6, ppTrackClut(track));
    }

    /* Active >20 badge */
    pu20DrawSphereClut(chain, 594, 332, 24, PU20_CLUT_PURPLE);
    ppCenteredFont(chain, 1, 594, 323, ">20");

    (void) font;
}

static void ppDrawMenu(DMAChain *chain, const TextureInfo *font) {
    (void) font;
    static const struct {
        int x, y, w, h;
        const char *label;
    } menu[PP_MENU_COUNT] = {
        { 108, 330, 96, 24, "CONTINUAR" },
        { 224, 330, 96, 24, "ALEAT\xD3RIO" },
        { 340, 330, 96, 24, "PROGRAMAR" },
        { 108, 390, 96, 24, "REPETIR"   },
        { 224, 390, 96, 24, "TEMPO"     },
        {   0,   0,  0,  0, NULL        }
    };

    for (int i = 0; i < PP_MENU_COUNT; i++) {
        bool active  = false;

        if (i == PP_MENU_SHUFFLE) active = s_pp.shuffle;
        if (i == PP_MENU_PROGRAM) active = s_pp.program;
        if (i == PP_MENU_REPEAT)  active = (s_pp.repeatMode != PP_REPEAT_OFF);

        if (i == PP_MENU_EXIT) {
            pu20DrawSplat(chain, 480, 410, 214, 58, 0);
            pu20DrawFont(chain, 1, 480 - pu20FontWidth(1, "SAIR") / 2, 401,
                         "SAIR", ppWhite);
            continue;
        }

        {
            Pu20Rgb txtCol = ppWhite;
            if (s_pp.menuClickTimer > 0 && s_pp.menuClickIdx == i) {
                uint8_t v = (uint8_t) (255 - ((int) s_pp.menuClickTimer * 255) / 10);
                txtCol = (Pu20Rgb){ v, v, v };
            }
            pu20DrawShellButton(chain, menu[i].x, menu[i].y, menu[i].w, menu[i].h,
                                active);
            pu20DrawFont(chain, 0,
                         menu[i].x + (menu[i].w - pu20FontWidth(0, menu[i].label)) / 2,
                         menu[i].y + 7, menu[i].label, txtCol);
        }
    }
}

static void ppGetCursorPos(int *outX, int *outY, bool *outVisible) {
    static const struct { int x, y, w; } kMenuPos[PP_MENU_COUNT] = {
        { 108, 330, 96 }, { 224, 330, 96 }, { 340, 330, 96 },
        { 108, 390, 96 }, { 224, 390, 96 }, { 479, 396,  0 }
    };
    *outVisible = false;
    if (s_pp.exitFade > 0) return;

    if (s_pp.focus == PP_FOCUS_GRID && !s_pp.keypad && s_pp.catalogCount > 0) {
        int row = (int) s_pp.listCursor - (int) s_pp.listScroll;
        if (row < 0) row = 0;
        if (row >= PP_LIST_VISIBLE) row = PP_LIST_VISIBLE - 1;
        *outX = 386;
        *outY = 106 + row * 28 + 15;
        *outVisible = true;
    } else if (s_pp.focus == PP_FOCUS_KEYPAD) {
        int col = s_pp.keypadIndex % 5;
        int row = s_pp.keypadIndex / 5;
        *outX = PP_GRID_X0 + col * PP_GRID_DX + 4;
        *outY = PP_GRID_Y0 + row * PP_GRID_DY + 6;
        *outVisible = true;
    } else if (s_pp.focus == PP_FOCUS_TRANSPORT) {
        int idx = s_pp.transportIndex % PP_TRANSPORT_COUNT;
        *outX = PP_TRANSPORT_X + 6;
        *outY = kTransportY[idx] + 4;
        *outVisible = true;
    } else if (s_pp.focus == PP_FOCUS_MENU) {
        int idx = s_pp.menuIndex % PP_MENU_COUNT;
        if (idx == PP_MENU_EXIT) {
            *outX = 484;
            *outY = 412;
        } else {
            *outX = kMenuPos[idx].x + kMenuPos[idx].w / 2 + 5;
            *outY = kMenuPos[idx].y + 16;
        }
        *outVisible = true;
    }
}

static void ppFormatSpecs(const PpTrackMeta *m, char *out, size_t size) {
    const char *fmt = ppFormatTag(m->format);
    char kbps[8] = {0};
    int k = (int) m->bitrateKbps;
    int pos = 0;
    size_t i = 0;

    if (size < 24) return;
    while (fmt[i] && pos < 6) out[pos++] = fmt[i++];
    out[pos++] = ' ';

    if (k >= 1000) {
        kbps[0] = (char) ('0' + (k / 1000) % 10);
        kbps[1] = (char) ('0' + (k / 100) % 10);
        kbps[2] = (char) ('0' + (k / 10) % 10);
        kbps[3] = (char) ('0' + k % 10);
        kbps[4] = 'K';
        kbps[5] = '\0';
    } else {
        kbps[0] = (char) ('0' + (k / 100) % 10);
        kbps[1] = (char) ('0' + (k / 10) % 10);
        kbps[2] = (char) ('0' + k % 10);
        kbps[3] = 'K';
        kbps[4] = '\0';
    }
    for (i = 0; kbps[i] && pos + 1 < (int) size; i++) out[pos++] = kbps[i];

    {
        const char *sr = (m->sampleRateHz == 37800) ? "  37.8KHZ" : "  44.1KHZ";
        for (i = 0; sr[i] && pos + 1 < (int) size; i++) out[pos++] = sr[i];
    }
    out[pos] = '\0';
}

static void ppDrawMarqueeText(DMAChain *chain, int x, int y, int maxX,
                              const char *text, Pu20Rgb color) {
    int availW = maxX - x;
    int fullW  = pu20FontWidth(0, text);

    if (fullW <= availW || availW <= 8) {
        pu20DrawFontClippedClean(chain, 0, x, y, maxX, text, color);
        return;
    }

    int len = (int) strlen(text);
    int maxOffset = 0;
    while (maxOffset < len - 1 && pu20FontWidth(0, text + maxOffset) > availW) {
        maxOffset++;
    }
    if (maxOffset <= 0) {
        pu20DrawFontClippedClean(chain, 0, x, y, maxX, text, color);
        return;
    }

    /* Smooth horizontal scroll: pause at start -> scroll -> pause at end -> wrap */
    uint32_t pauseStart = 5u;
    uint32_t pauseEnd   = 4u;
    uint32_t cycle      = pauseStart + (uint32_t) maxOffset + pauseEnd;
    uint32_t step       = (s_pp.frame / 14u) % cycle;
    int offset = 0;
    if (step <= pauseStart) {
        offset = 0;
    } else if (step < pauseStart + (uint32_t) maxOffset) {
        offset = (int) (step - pauseStart);
    } else {
        offset = maxOffset;
    }
    pu20DrawFontClippedClean(chain, 0, x, y, maxX, text + offset, color);
}

static void ppDrawAlbumArtQuad(DMAChain *chain, int x, int y, int size) {
    /* Solid black backing + crisp 1px PU-20 indigo frame */
    ppSolid(chain, x - 1, y - 1, size + 2, size + 2, (Pu20Rgb){ 108, 120, 210 });
    ppSolid(chain, x, y, size, size, (Pu20Rgb){ 0, 0, 0 });

    uint32_t *p = allocatePacket(chain, 9);
    uint8_t u0 = s_artTex.u;
    uint8_t v0 = s_artTex.v;
    uint8_t u1 = (uint8_t) (u0 + 127u);
    uint8_t v1 = (uint8_t) (v0 + 127u);

    p[0] = gp0_quad(true, false);
    p[1] = gp0_xy(x, y);
    p[2] = gp0_uv(u0, v0, s_artTex.clut);
    p[3] = gp0_xy(x + size, y);
    p[4] = gp0_uv(u1, v0, s_artTex.page);
    p[5] = gp0_xy(x, y + size);
    p[6] = gp0_uv(u0, v1, 0);
    p[7] = gp0_xy(x + size, y + size);
    p[8] = gp0_uv(u1, v1, 0);
}

static void ppDrawContinuePanel(DMAChain *chain, const TextureInfo *font) {
    (void) font;
    /* Authentic PU-20 info frame: transparent interior over concentric rings
     * with crisp 2px border in RGB(65, 65, 139) at (109, 159, 242, 130).
     * Displays SD:/PicoPlayer/ path + optional 88x88 Album Art + scrolling ID3 tags! */
    Pu20Rgb border   = { 65, 65, 139 };
    Pu20Rgb colPath  = { 184, 196, 255 };
    Pu20Rgb colArt   = { 156, 232, 255 };
    Pu20Rgb colAlb   = { 196, 200, 244 };
    Pu20Rgb colSpec  = { 255, 220,  96 };
    const char *modeLabel = "CONTINUAR";
    bool hasArt = false;

    ppSolid(chain, 111, 161, 238, 126, (Pu20Rgb){ 10, 14, 52 });
    ppSolid(chain, 109, 159, 242, 2, border);
    ppSolid(chain, 109, 287, 242, 2, border);
    ppSolid(chain, 109, 159, 2, 130, border);
    ppSolid(chain, 349, 159, 2, 130, border);

    /* Top header row: SD:/PICOPLAYER/ + [cur/total] */
    pu20DrawFontClippedClean(chain, 0, 118, 166, 282, "SD:/PICOPLAYER/", colPath);
    if (s_pp.catalogCount > 0) {
        /* Always display the currently playing/loaded track (never change when moving the list cursor). */
        int inspectIdx = ppCatalogIndexForTrack(s_pp.curTrack);
        char cnt[8];
        if (inspectIdx < 0 || inspectIdx >= (int) s_pp.catalogCount) inspectIdx = 0;
        ppFormatDec2(cnt, inspectIdx + 1);
        cnt[2] = '/';
        ppFormatDec2(cnt + 3, (int) s_pp.catalogCount);
        pu20DrawFontClean(chain, 0, 342 - pu20FontWidth(0, cnt), 166, cnt, colPath);

        ppSolid(chain, 115, 181, 230, 1, border);

        ppEnsureArtUploaded(inspectIdx);
        hasArt = (s_uploadedArtSlot >= 0);

        /* ID3 / RIFF tags of the currently playing track */
        {
            const PpTrackMeta *m = &s_catalog[inspectIdx];
            char specs[28];
            int textX = hasArt ? 217 : 118;
            int textMaxX = 345;

            if (hasArt) {
                /* 96x96 = exact 3:4 scale of 128x128 for minimal nearest-neighbor aliasing */
                ppDrawAlbumArtQuad(chain, 115, 186, 96);
            }

            ppDrawMarqueeText(chain, textX, hasArt ? 188 : 187, textMaxX, m->title, ppWhite);
            ppDrawMarqueeText(chain, textX, hasArt ? 206 : 205, textMaxX, m->artist, colArt);
            ppDrawMarqueeText(chain, textX, hasArt ? 224 : 223, textMaxX, m->album, colAlb);

            ppFormatSpecs(m, specs, sizeof(specs));
            ppDrawMarqueeText(chain, textX, hasArt ? 244 : 242, textMaxX, specs, colSpec);
        }
    } else {
        ppSolid(chain, 115, 181, 230, 1, border);
        pu20DrawFontClippedClean(chain, 0, 118, 196, 342, "NENHUM ARQUIVO", ppWhite);
        pu20DrawFontClippedClean(chain, 0, 118, 216, 342, "MP3 / WAV / FLAC / OGG", colArt);
        pu20DrawFontClippedClean(chain, 0, 118, 234, 342, "EM SD:/PICOPLAYER/", colAlb);
    }

    if (s_pp.keypad)       modeLabel = "FAIXA >20";
    else if (s_pp.program) modeLabel = "PROGRAMAR";
    else if (s_pp.shuffle) modeLabel = "ALEAT\xD3RIO";

    if (hasArt) {
        const char *rightTag = NULL;
        if (s_pp.repeatMode != PP_REPEAT_OFF) {
            rightTag = (s_pp.repeatMode == PP_REPEAT_ONE) ? "REP 1" : "REP TUDO";
        } else if (s_pp.timeMode != 0) {
            rightTag = (s_pp.timeMode == 1) ? "REST. FAIXA" : "REST. DISCO";
        }
        if (rightTag) {
            ppDrawMarqueeText(chain, 217, 264, 345, rightTag, colSpec);
        } else {
            ppDrawMarqueeText(chain, 217, 264, 345, modeLabel, ppWhite);
        }
    } else {
        pu20DrawFontClean(chain, 0, 118, 268, modeLabel, ppWhite);

        if (s_pp.repeatMode != PP_REPEAT_OFF) {
            const char *rep = (s_pp.repeatMode == PP_REPEAT_ONE)
                                  ? "REP 1" : "REP TUDO";
            pu20DrawFontClean(chain, 0, 342 - pu20FontWidth(0, rep), 268, rep, colSpec);
        } else if (s_pp.timeMode != 0) {
            const char *tm = (s_pp.timeMode == 1) ? "REST. FAIXA" : "REST. DISCO";
            pu20DrawFontClean(chain, 0, 342 - pu20FontWidth(0, tm), 268, tm, colSpec);
        }
    }
}

static void ppDrawPlayer(DMAChain *chain, const TextureInfo *font) {
    uint32_t curDispSec  = ppDisplayFrames() / PP_CD_FRAMES_PER_SEC;
    uint32_t curMarquee  = s_pp.frame / 14u;
    bool     hasToast    = (s_pp.toastTimer > 0);
    int      curX = 0, curY = 0;
    bool     curVis = false;

    ppGetCursorPos(&curX, &curY, &curVis);

    /* ONLY redraw the full concentric-rings background on initial entry, return
     * from SoundScope, or switching to/from Keypad — NEVER on cursor movement,
     * track switching, play/pause/stop, or exiting! */
    bool needFull = s_vis.bgDirty || (s_pp.keypad != s_vis.keypad);

    if (needFull) {
        int titleWidth;
        int titleX;

        pu20DrawShellBackground(chain, 640, 480, s_pp.frame);

        /* Snapshot clean background slices into free VRAM (x=960..1023 & y=480..503)
         * so transport buttons, minus sign, toast and cursor restore in 5 us! */
        ppVramBlit(chain, 26, 36, 960, 36, 64, 408);
        ppVramBlit(chain, 168, 86, 960, 448, 32, 20);
        ppVramBlit(chain, 122, 297, 0, 480, 216, 24);

        pu20DrawTitlePanelRect(chain, PP_PANEL_TITLE_X, PP_PANEL_TITLE_Y,
                               PP_PANEL_TITLE_W, PP_PANEL_TITLE_H);
        titleWidth = pu20FontWidth(1, "PICOPLAYER");
        titleX     = PP_PANEL_TITLE_X + (PP_PANEL_TITLE_W - titleWidth) / 2;
        pu20DrawFont(chain, 1, titleX, PP_PANEL_TITLE_Y + 15, "PICOPLAYER", ppWhite);

        ppDrawDisplay(chain, font);
        ppDrawTransportRow(chain);
        ppDrawContinuePanel(chain, font);

        if (s_pp.keypad) {
            ppDrawKeypad(chain, font);
        } else {
            ppDrawGrid(chain, font);
        }

        ppDrawMenu(chain, font);

        if (hasToast) {
            ppSolid(chain, 122, 297, 216, 24, (Pu20Rgb){ 18, 22, 62 });
            ppSolid(chain, 122, 297, 216, 1,  (Pu20Rgb){ 96, 108, 216 });
            ppSolid(chain, 122, 320, 216, 1,  (Pu20Rgb){ 96, 108, 216 });
            ppSolid(chain, 122, 297, 1,   24, (Pu20Rgb){ 96, 108, 216 });
            ppSolid(chain, 337, 297, 1,   24, (Pu20Rgb){ 96, 108, 216 });
            pu20DrawFontClean(chain, 0, 230 - pu20FontWidth(0, s_pp.toast) / 2, 304, s_pp.toast, ppWhite);
        }

        s_vis.cursorSaved = false;
        if (curVis) {
            int sx = (curX > 4) ? (curX - 4) : 0;
            int sy = (curY > 4) ? (curY - 4) : 0;
            ppVramBlit(chain, sx, sy, 960, 0, 36, 36);
            s_vis.cursorSaveX = (int16_t) sx;
            s_vis.cursorSaveY = (int16_t) sy;
            s_vis.cursorSaved = true;
            ppDrawCursor(chain, curX, curY);
        }
        s_vis.bgDirty = false;
    } else {
        /* 100% localized hardware-blit dirty updates (< 0.4 ms, inside VBlank!):
         * ZERO background wipes, ZERO 480i flicker even while moving cursor or
         * switching tracks! */
        bool needDisp  = (curDispSec != s_vis.dispSec ||
                          s_pp.curTrack != s_vis.curTrack ||
                          s_pp.timeMode != s_vis.timeMode ||
                          s_pp.entry != s_vis.entry ||
                          s_pp.entryCount != s_vis.entryCount);
        bool needTrans = (s_pp.playing != s_vis.playing ||
                          s_pp.paused != s_vis.paused);
        bool needMenu  = (s_pp.shuffle != s_vis.shuffle ||
                          s_pp.program != s_vis.program ||
                          s_pp.repeatMode != s_vis.repeatMode ||
                          s_pp.menuClickTimer != s_vis.menuClickTimer);
        bool needInfo  = (s_pp.curTrack != s_vis.curTrack ||
                          s_pp.repeatMode != s_vis.repeatMode ||
                          s_pp.timeMode != s_vis.timeMode ||
                          s_pp.shuffle != s_vis.shuffle ||
                          s_pp.program != s_vis.program ||
                          (s_pp.catalogCount > 0 && curMarquee != s_vis.marqueeStep));
        bool needList  = (!s_pp.keypad &&
                          (s_pp.listCursor != s_vis.listCursor ||
                           s_pp.listScroll != s_vis.listScroll ||
                           s_pp.curTrack != s_vis.curTrack ||
                           s_pp.playing != s_vis.playing ||
                           s_pp.paused != s_vis.paused ||
                           s_pp.program != s_vis.program ||
                           s_pp.trackClickTimer != s_vis.trackClickTimer ||
                           (s_pp.focus == PP_FOCUS_GRID && s_pp.catalogCount > 0 && curMarquee != s_vis.marqueeStep)));
        bool needToast = (hasToast != s_vis.hadToast) ||
                         (hasToast && s_pp.toastGen != s_vis.toastGen);
        bool needCur   = (s_pp.focus != s_vis.focus ||
                          s_pp.listCursor != s_vis.listCursor ||
                          s_pp.listScroll != s_vis.listScroll ||
                          s_pp.menuIndex != s_vis.menuIndex ||
                          s_pp.transportIndex != s_vis.transportIndex ||
                          s_pp.keypadIndex != s_vis.keypadIndex);
        bool anyChange = needDisp || needTrans || needMenu || needInfo ||
                         needList || needToast || needCur;

        if (anyChange) {
            /* Restore pixels under old cursor BEFORE updating any widgets */
            if (s_vis.cursorSaved) {
                ppVramBlit(chain, 960, 0, s_vis.cursorSaveX, s_vis.cursorSaveY, 36, 36);
                s_vis.cursorSaved = false;
            }

            if (needDisp) {
                ppVramBlit(chain, 960, 448, 168, 86, 32, 20);
                ppDrawDisplay(chain, font);
            }
            if (needTrans) {
                ppVramBlit(chain, 960, 36, 26, 36, 64, 408);
                ppDrawTransportRow(chain);
            }
            if (needInfo)  ppDrawContinuePanel(chain, font);
            if (needList)  ppDrawGrid(chain, font);
            if (needMenu)  ppDrawMenu(chain, font);
            if (needToast) {
                if (hasToast) {
                    ppSolid(chain, 122, 297, 216, 24, (Pu20Rgb){ 18, 22, 62 });
                    ppSolid(chain, 122, 297, 216, 1,  (Pu20Rgb){ 96, 108, 216 });
                    ppSolid(chain, 122, 320, 216, 1,  (Pu20Rgb){ 96, 108, 216 });
                    ppSolid(chain, 122, 297, 1,   24, (Pu20Rgb){ 96, 108, 216 });
                    ppSolid(chain, 337, 297, 1,   24, (Pu20Rgb){ 96, 108, 216 });
                    pu20DrawFontClean(chain, 0, 230 - pu20FontWidth(0, s_pp.toast) / 2, 304, s_pp.toast, ppWhite);
                } else {
                    /* Toast expired: restore concentric rings cleanly in 5 us via
                     * VRAM blit — ZERO full-screen redraw, ZERO 480i flash! */
                    ppVramBlit(chain, 0, 480, 122, 297, 216, 24);
                }
            }

            /* Save fresh background under new cursor & draw cursor on top */
            if (curVis) {
                int sx = (curX > 4) ? (curX - 4) : 0;
                int sy = (curY > 4) ? (curY - 4) : 0;
                ppVramBlit(chain, sx, sy, 960, 0, 36, 36);
                s_vis.cursorSaveX = (int16_t) sx;
                s_vis.cursorSaveY = (int16_t) sy;
                s_vis.cursorSaved = true;
                ppDrawCursor(chain, curX, curY);
            }
        }
    }

    s_vis.dispSec         = curDispSec;
    s_vis.marqueeStep     = curMarquee;
    s_vis.curTrack        = s_pp.curTrack;
    s_vis.timeMode        = s_pp.timeMode;
    s_vis.repeatMode      = s_pp.repeatMode;
    s_vis.focus           = s_pp.focus;
    s_vis.listCursor      = s_pp.listCursor;
    s_vis.listScroll      = s_pp.listScroll;
    s_vis.menuIndex       = s_pp.menuIndex;
    s_vis.transportIndex  = s_pp.transportIndex;
    s_vis.keypadIndex     = s_pp.keypadIndex;
    s_vis.entry           = s_pp.entry;
    s_vis.entryCount      = s_pp.entryCount;
    s_vis.menuClickTimer  = s_pp.menuClickTimer;
    s_vis.trackClickTimer = s_pp.trackClickTimer;
    s_vis.playing         = s_pp.playing;
    s_vis.paused          = s_pp.paused;
    s_vis.shuffle         = s_pp.shuffle;
    s_vis.program         = s_pp.program;
    s_vis.keypad          = s_pp.keypad;
    s_vis.hadToast        = hasToast;
    s_vis.toastGen        = s_pp.toastGen;

    (void) ppCentered;
    (void) ppGridTrack;
}

/* SoundScope (12 authentic PU-20 modes + trails + 5 palettes + speed) ------ */

#define PP_SCOPE_PALETTES 5

static Pu20Rgb ppScopePaletteColor(int idx) {
    /* 5 palettes cycled with CIRCLE (BIOS style):
     * 0=Fire/Spectrum, 1=Ice/Cyan, 2=Neon/Magenta, 3=Emerald, 4=Gold/Solar */
    static const Pu20Rgb pal[PP_SCOPE_PALETTES][6] = {
        { { 255,   8,   8 }, { 255, 128,   0 }, { 255, 238,   0 },
          { 148, 255,   0 }, {  24, 255,  32 }, {   0, 255, 110 } },
        { {  32,  96, 255 }, {  24, 168, 255 }, {  64, 232, 255 },
          { 160, 248, 255 }, { 224, 255, 255 }, {  80, 192, 255 } },
        { { 255,  16, 128 }, { 255,  48, 224 }, { 192,  48, 255 },
          { 112,  64, 255 }, {  48, 192, 255 }, { 255, 128, 224 } },
        { {   0, 255,  96 }, {  32, 255, 160 }, {  96, 255,  64 },
          { 180, 255,  32 }, {   0, 216, 128 }, {  64, 255, 200 } },
        { { 255, 240,  96 }, { 255, 200,  24 }, { 255, 144,  16 },
          { 255,  88,  16 }, { 255, 224,  48 }, { 255, 255, 180 } }
    };
    return pal[s_pp.scopePalette % PP_SCOPE_PALETTES][idx % 6];
}

static int ppClamp(int v, int lo, int hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static uint32_t ppISqrtU32(uint32_t n) {
    uint32_t r = 0;
    uint32_t bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else { r >>= 1; }
        bit >>= 2;
    }
    return r;
}

/* Exact PU-20 BIOS audio gain scaling (shell.bin 0x80045398):
 * UP/DOWN adjusts gain (scopeSpeed 1..8 -> 96..480, default 4 -> 256 = 1.0x). */
static int ppGainScale(int sample) {
    int gain = 48 + (int) s_pp.scopeSpeed * 52;
    return (sample * gain) >> 8;
}

static void ppDrawScope(DMAChain *chain) {
    int speed  = (s_pp.scopeSpeed >= 1) ? (int) s_pp.scopeSpeed : 4;
    int energy = (((int) s_pp.levelL + (int) s_pp.levelR + 1) * (speed + 2)) / 5;
    int phase  = (int) (s_pp.scopePhase >> 2);
    Pu20Rgb zero = { 0, 0, 0 };

    /* True double-buffered 320x240p (authentic Sony PU-20 BIOS SoundScope):
     * schedule front buffer (dispX) to flip inside VBlank via
     * picoplayer_present_vblank() while drawing into hidden back buffer
     * (drawX). Eliminates mid-scanout top-of-screen tearing/clipping! */
    int dispX = s_pp.scopeBuf ? 320 : 0;
    int drawX = s_pp.scopeBuf ? 0 : 320;
    s_pp.scopeBuf ^= 1u;

    s_pp.pendingDispX   = (int16_t) dispX;
    s_pp.hasPendingDisp = true;

    if (s_pp.scopeTrailLevel > 0 && s_pp.scopeFrame > 0) {
        /* Copy previous completed front buffer into back buffer before fading */
        ppVramBlit(chain, dispX, 0, drawX, 0, 320, 240);
    }

    {
        uint32_t *setup = allocatePacket(chain, 4);
        setup[0] = gp0_texpage(0, false, true);
        setup[1] = gp0_fbOffset1(drawX, 0);
        setup[2] = gp0_fbOffset2(drawX + 319, 239);
        setup[3] = gp0_fbOrigin(drawX, 0);
    }

    /* Exact PU-20 BIOS subtraction trail:
     * SQUARE cycles 4 trail levels: 0=OFF (pure clear), 1=SHORT (32),
     * 2=MEDIUM (14), 3=LONG (6). Background ALWAYS stays pure (0,0,0)! */
    if (s_pp.scopeTrailLevel > 0 && s_pp.scopeFrame > 0) {
        static const uint8_t kFade[4] = { 255, 32, 14, 6 };
        uint8_t fade = kFade[s_pp.scopeTrailLevel & 3u];
        pu20DrawSubtractRect(chain, 0, 0, 320, 240, (Pu20Rgb){ fade, fade, fade });
    } else {
        ppSolid(chain, 0, 0, 320, 240, zero);
    }
    /* Restore normal semi-trans blend so no mode can inherit GP0_BLEND_SUBTRACT */
    pu20SetChainBlend(chain, GP0_BLEND_SEMITRANS);

    switch (s_pp.scopeMode % PP_SCOPE_MODES) {
        case 0: {
            /* BIOS pos 0 / ID 0: 256 vertical Gouraud spikes from y=120 [c0] to y=120+dy [c1]. */
            Pu20Rgb c0 = ppScopePaletteColor(0);
            Pu20Rgb c1 = ppScopePaletteColor(2);
            for (int i = 0; i < 256; i++) {
                int x  = 32 + i;
                int sL = ppGainScale((int) s_pp.scopeL[i]);
                int sR = ppGainScale((int) s_pp.scopeR[i]);
                int dy = ppClamp((sL + sR) >> 8, -112, 112);
                pu20DrawGradientLine(chain, x, 120, c0, x, 120 + dy, c1, false);
            }
            break;
        }

        case 1: {
            /* BIOS pos 1 / ID 1: horizontal luminous ribbon modulated by ((L+R)>>7)+128. */
            Pu20Rgb base = ppScopePaletteColor(0);
            for (int i = 0; i < 256; i++) {
                int x   = 32 + i;
                int sL  = ppGainScale((int) s_pp.scopeL[i]);
                int sR  = ppGainScale((int) s_pp.scopeR[i]);
                int amp = ppClamp(((sL + sR) >> 7) + 128, 0, 255);
                Pu20Rgb topC = { base.r, (uint8_t) amp, base.b };
                Pu20Rgb botC = { (uint8_t) amp, base.g, (uint8_t)(base.b / 2) };
                pu20DrawGradientLine(chain, x, 98, topC, x, 142, botC, false);
            }
            break;
        }

        case 2: {
            /* BIOS pos 2 / ID 4: 256 stereo differential vertical ribbon (never collapses on mono). */
            Pu20Rgb c0 = ppScopePaletteColor(0);
            Pu20Rgb c1 = ppScopePaletteColor(2);
            for (int i = 0; i < 256; i++) {
                int x  = 32 + i;
                int y0 = ppClamp(120 + (ppGainScale((int) s_pp.scopeL[i]) >> 8), 8, 232);
                int y1 = ppClamp(120 - (ppGainScale((int) s_pp.scopeR[i]) >> 8), 8, 232);
                if (y0 == y1) { y0 -= 1; y1 += 1; }
                pu20DrawGradientLine(chain, x, y0, c0, x, y1, c1, false);
            }
            break;
        }

        case 3: {
            /* BIOS pos 3 / ID 9: glowing red horizontal baseline with bouncing spectrum peaks. */
            Pu20Rgb lineC = ppScopePaletteColor(0);
            Pu20Rgb tipC  = ppScopePaletteColor(2);
            pu20DrawGradientLine(chain, 28, 156, lineC, 292, 156, lineC, false);
            pu20DrawGradientLine(chain, 28, 157, lineC, 292, 157, lineC, false);
            for (int b = 0; b < 24; b++) {
                int x  = 32 + b * 11;
                int h  = ppClamp(((int) s_pp.spectrum[b] * (speed + 3)) / 5, 1, 136);
                if (h > 2) {
                    pu20DrawGradientTriangle(chain, x, 156 - h, tipC,
                                             x - 5, 156, lineC, x + 5, 156, lineC, false);
                }
            }
            break;
        }

        case 4: {
            /* BIOS pos 4 / ID 2: 256 radial Gouraud rays from center (160,120). */
            Pu20Rgb c0 = ppScopePaletteColor(0);
            Pu20Rgb c1 = ppScopePaletteColor(2);
            for (int i = 0; i < 256; i++) {
                int x1 = ppClamp(160 + (ppGainScale((int) s_pp.scopeL[i]) >> 7), 8, 312);
                int y1 = ppClamp(120 + (ppGainScale((int) s_pp.scopeR[i]) >> 7), 8, 232);
                pu20DrawGradientLine(chain, 160, 120, c0, x1, y1, c1, false);
            }
            break;
        }

        case 5: {
            /* BIOS pos 7 / ID 6: crisp connected oscilloscope waveform across x=32..287. */
            Pu20Rgb c = ppScopePaletteColor(2);
            int py = ppClamp(120 + ((ppGainScale((int) s_pp.scopeL[0]) +
                                     ppGainScale((int) s_pp.scopeR[0])) >> 8), 10, 230);
            for (int i = 1; i < 256; i++) {
                int x = 32 + i;
                int y = ppClamp(120 + ((ppGainScale((int) s_pp.scopeL[i]) +
                                        ppGainScale((int) s_pp.scopeR[i])) >> 8), 10, 230);
                pu20DrawGradientLine(chain, x - 1, py, c, x, y, c, false);
                py = y;
            }
            break;
        }

        case 6: {
            /* BIOS pos 8 / ID 7: horizontal symmetric glowing spectrum spindle (24 mirrored quads). */
            Pu20Rgb cTop = ppScopePaletteColor(2);
            Pu20Rgb cMid = ppScopePaletteColor(1);
            for (int b = 0; b < 24; b++) {
                int x0 = 28 + b * 11;
                int x1 = x0 + 11;
                int h  = ppClamp(4 + ((int) s_pp.spectrum[b] * (speed + 3)) / 6, 4, 106);
                pu20DrawGradientQuad(chain, x0, 120 - h, x1, 120,
                                     zero, zero, cTop, cTop, false);
                pu20DrawGradientQuad(chain, x0, 120, x1, 120 + h,
                                     cMid, cMid, zero, zero, false);
            }
            break;
        }

        case 7: {
            /* BIOS pos 9 / ID 8: sparkling central stereo point cloud. */
            Pu20Rgb base = ppScopePaletteColor(2);
            for (int i = 0; i < 256; i++) {
                int dx = ppGainScale((int) s_pp.scopeL[i]) >> 7;
                int dy = ppGainScale((int) s_pp.scopeR[i]) >> 7;
                int r  = (int) ppISqrtU32((uint32_t) (dx * dx + dy * dy));
                int br = ppClamp(255 - r, 64, 255);
                Pu20Rgb col = {
                    (uint8_t) (((int) base.r * br) >> 8),
                    (uint8_t) (((int) base.g * br) >> 8),
                    (uint8_t) (((int) base.b * br) >> 8)
                };
                int x = ppClamp(160 + dx, 8, 310);
                int y = ppClamp(120 + dy, 8, 230);
                ppSolid(chain, x, y, 2, 2, col);
            }
            break;
        }

        case 8: {
            /* BIOS pos 10 / ID 15: thick yellow semi-trans square wave + red core bar! */
            Pu20Rgb yel = ppScopePaletteColor(2);
            Pu20Rgb red = ppScopePaletteColor(0);
            pu20SetChainBlend(chain, GP0_BLEND_SEMITRANS);
            for (int i = 0; i < 128; i++) {
                int x = 28 + i * 2;
                int y = ppClamp(120 + ((ppGainScale((int) s_pp.scopeL[i * 2]) +
                                        ppGainScale((int) s_pp.scopeR[i * 2])) >> 8), 20, 220);
                pu20DrawSemiRect(chain, x - 3, y - 4, 6, 8, yel);
            }
            for (int b = 0; b < 32; b++) {
                int x = 28 + b * 8;
                int h = 4 + ((int) s_pp.spectrum[b % PP_SPECTRUM_BANDS] / 8);
                ppSolid(chain, x, 120 - h / 2, 7, h, red);
            }
            break;
        }

        case 9: {
            /* BIOS pos 11 / ID 11: full-height vertical glowing yellow wave (all 240 lines!) + white dots. */
            Pu20Rgb c = ppScopePaletteColor(2);
            pu20SetChainBlend(chain, GP0_BLEND_ADD);
            for (int y = 0; y < 240; y++) {
                int sL  = ppGainScale((int) s_pp.scopeL[y]);
                int sR  = ppGainScale((int) s_pp.scopeR[y]);
                int cx  = ppClamp(160 + (sL >> 8), 55, 265);
                int dot = ppClamp(160 + (sR >> 9), 10, 309);
                pu20DrawGradientLine(chain, cx - 54, y, zero, cx, y, c, true);
                pu20DrawGradientLine(chain, cx, y, c, cx + 54, y, zero, true);
                ppSolid(chain, dot, y, 2, 1, ppWhite);
            }
            break;
        }

        case 10: {
            /* BIOS pos 12 / ID 10: translucent golden origami/wing triangles connecting stereo path. */
            Pu20Rgb c0 = ppScopePaletteColor(2);
            Pu20Rgb c1 = ppScopePaletteColor(1);
            int xA = 160, yA = 120;
            int xB = ppClamp(160 + (ppGainScale((int) s_pp.scopeL[0]) >> 7), 12, 308);
            int yB = ppClamp(120 + (ppGainScale((int) s_pp.scopeR[0]) >> 7), 12, 228);

            pu20SetChainBlend(chain, GP0_BLEND_SEMITRANS);
            for (int k = 1; k < 16; k++) {
                int idx = k * 16;
                int xC  = ppClamp(160 + (ppGainScale((int) s_pp.scopeL[idx]) >> 7), 12, 308);
                int yC  = ppClamp(120 + (ppGainScale((int) s_pp.scopeR[idx]) >> 7), 12, 228);
                pu20DrawGradientTriangle(chain, xA, yA, c0, xB, yB, c1, xC, yC, c0, true);
                xA = xB; yA = yB;
                xB = xC; yB = yC;
            }
            break;
        }

        case 11: {
            /* BIOS pos 13 / ID 16: 6 full-height breathing glowing spectrum light pillars! */
            static const int centers[6] = { 41, 76, 120, 171, 222, 270 };
            static const int maxHalf[6] = { 17, 22,  27,  27,  26,  24 };
            for (int b = 0; b < 6; b++) {
                int lvl = ((int) s_pp.spectrum[b * 4] +
                           s_pp.spectrum[b * 4 + 1] +
                           s_pp.spectrum[b * 4 + 2] +
                           s_pp.spectrum[b * 4 + 3]) / 4;
                int halfW = ppClamp(5 + (lvl * (maxHalf[b] - 5)) / 75, 5, maxHalf[b]);
                int cx    = centers[b];
                Pu20Rgb c = ppScopePaletteColor(b);
                pu20DrawGradientQuad(chain, cx - halfW, 14, cx, 226,
                                     zero, c, zero, c, false);
                pu20DrawGradientQuad(chain, cx, 14, cx + halfW, 226,
                                     c, zero, c, zero, false);
            }
            break;
        }

        case 12: {
            /* BIOS pos 14 / ID 23: horizontal 30-block segmented red equalizer bar (amplitude histogram). */
            int bins[30];
            Pu20Rgb base = ppScopePaletteColor(0);

            memset(bins, 0, sizeof(bins));
            for (int i = 0; i < 256; i++) {
                int sum = ppGainScale((int) s_pp.scopeL[i]) +
                          ppGainScale((int) s_pp.scopeR[i]);
                int idx = ppClamp((sum >> 11) + 15, 0, 29);
                bins[idx]++;
            }
            for (int b = 0; b < 30; b++) {
                int cnt = bins[b] > 16 ? 16 : bins[b];
                int br  = 40 + cnt * 13;
                Pu20Rgb col = {
                    (uint8_t) (((int) base.r * br) >> 8),
                    (uint8_t) (((int) base.g * br) >> 8),
                    (uint8_t) (((int) base.b * br) >> 8)
                };
                ppSolid(chain, 25 + b * 9, 108, 8, 24, col);
            }
            break;
        }

        case 13: {
            /* BIOS pos 15 / ID 13: dual side vertical glowing yellow zig-zag waves (full height y=0..239!). */
            Pu20Rgb cL = ppScopePaletteColor(2);
            Pu20Rgb cR = ppScopePaletteColor(1);
            pu20SetChainBlend(chain, GP0_BLEND_ADD);
            for (int i = 0; i < 120; i++) {
                int y   = i * 2;
                int idx = (i * 255) / 119;
                int xL  = ppClamp(62 + (ppGainScale((int) s_pp.scopeL[idx]) >> 8), 8, 148);
                int xR  = ppClamp(258 - (ppGainScale((int) s_pp.scopeR[idx]) >> 8), 172, 312);
                pu20DrawGradientLine(chain, 8, y, zero, xL, y, cL, true);
                pu20DrawGradientLine(chain, 8, y + 1, zero, xL, y + 1, cL, true);
                pu20DrawGradientLine(chain, xR, y, cR, 312, y, zero, true);
                pu20DrawGradientLine(chain, xR, y + 1, cR, 312, y + 1, zero, true);
            }
            break;
        }

        case 14: {
            /* BIOS pos 16 / ID 12: closed glowing red Lissajous ribbon. */
            Pu20Rgb c = ppScopePaletteColor(0);
            int dx0 = ppGainScale((int) s_pp.scopeL[0]) >> 7;
            int dy0 = ppGainScale((int) s_pp.scopeR[0]) >> 7;
            int ox0 = ppClamp(160 + dx0, 12, 308);
            int oy0 = ppClamp(120 + dy0, 12, 228);
            int ix0 = ppClamp(160 + (dx0 * 210) / 256, 12, 308);
            int iy0 = ppClamp(120 + (dy0 * 210) / 256, 12, 228);

            pu20SetChainBlend(chain, GP0_BLEND_ADD);
            for (int i = 2; i < 256; i += 2) {
                int dx = ppGainScale((int) s_pp.scopeL[i]) >> 7;
                int dy = ppGainScale((int) s_pp.scopeR[i]) >> 7;
                int ox1 = ppClamp(160 + dx, 12, 308);
                int oy1 = ppClamp(120 + dy, 12, 228);
                int ix1 = ppClamp(160 + (dx * 210) / 256, 12, 308);
                int iy1 = ppClamp(120 + (dy * 210) / 256, 12, 228);
                pu20DrawGradientTriangle(chain, ix0, iy0, zero, ox0, oy0, c, ox1, oy1, c, true);
                pu20DrawGradientTriangle(chain, ix0, iy0, zero, ox1, oy1, c, ix1, iy1, zero, true);
                ox0 = ox1; oy0 = oy1; ix0 = ix1; iy0 = iy1;
            }
            break;
        }

        case 15: {
            /* BIOS pos 17 / ID 17: 3-layer translucent mirrored mountain spectrum peaks. */
            pu20SetChainBlend(chain, GP0_BLEND_SEMITRANS);
            for (int k = 0; k < 4; k++) {
                int h = ppClamp(14 + ((int) s_pp.spectrum[k * 3] * 94) / 100, 14, 106);
                int cx = 52 + k * 72;
                Pu20Rgb tip = ppScopePaletteColor(2);
                pu20DrawGradientTriangle(chain, cx, 120 - h, tip, cx - 32, 120, zero, cx + 32, 120, zero, true);
                pu20DrawGradientTriangle(chain, cx, 120 + h, tip, cx - 32, 120, zero, cx + 32, 120, zero, true);
            }
            for (int k = 0; k < 8; k++) {
                int h = ppClamp(10 + ((int) s_pp.spectrum[(k * 2 + 4) % PP_SPECTRUM_BANDS] * 60) / 100, 10, 68);
                int cx = 32 + k * 36;
                Pu20Rgb tip = ppScopePaletteColor(1);
                pu20DrawGradientTriangle(chain, cx, 120 - h, tip, cx - 19, 120, zero, cx + 19, 120, zero, true);
                pu20DrawGradientTriangle(chain, cx, 120 + h, tip, cx - 19, 120, zero, cx + 19, 120, zero, true);
            }
            for (int k = 0; k < 24; k++) {
                int h = ppClamp(8 + ((int) s_pp.spectrum[k] * 32) / 100, 8, 38);
                int cx = 12 + k * 12;
                Pu20Rgb tip = ppScopePaletteColor(4);
                pu20DrawGradientTriangle(chain, cx, 120 - h, tip, cx - 8, 120, zero, cx + 8, 120, zero, true);
                pu20DrawGradientTriangle(chain, cx, 120 + h, tip, cx - 8, 120, zero, cx + 8, 120, zero, true);
            }
            break;
        }

        case 16: {
            /* BIOS pos 18 / ID 18: 3 concentric pulsing circular rings (1:1 square pixels!). */
            static const int baseR[3] = { 72, 42, 20 };
            static const int thick[3] = { 8, 5, 4 };
            Pu20Rgb ring[3];
            ring[0] = (s_pp.scopePalette == 0) ? (Pu20Rgb){ 255, 168, 168 } : ppScopePaletteColor(0);
            ring[1] = (s_pp.scopePalette == 0) ? (Pu20Rgb){ 255, 255, 148 } : ppScopePaletteColor(2);
            ring[2] = (s_pp.scopePalette == 0) ? (Pu20Rgb){ 255, 255,  48 } : ppScopePaletteColor(4);
            for (int k = 0; k < 3; k++) {
                int band = (int) s_pp.spectrum[k * 6];
                int rOut = ppClamp(baseR[k] + (band * (3 - k)) / 8 + (energy * (k + 1)) / 7, 10, 108);
                int rIn  = rOut - thick[k];
                if (rIn < 3) rIn = 3;
                pu20DrawRing(chain, 160, 120, rIn, rOut, ring[k], 32);
            }
            break;
        }

        case 17: {
            /* BIOS pos 19 / ID 19: orthogonal pink neon walker (vivid solid tiles, never vanishes!). */
            Pu20Rgb pink = (s_pp.scopePalette == 0) ? (Pu20Rgb){ 255, 132, 132 }
                                                    : ppScopePaletteColor(2);
            if (s_pp.scopeFrame == 0) {
                s_pp.turtleX = 160;
                s_pp.turtleY = 120;
                s_pp.turtleDir = 0;
            }
            for (int s = 0; s < 24; s++) {
                int smp = ppGainScale((int) s_pp.scopeL[s * 10]) +
                          ppGainScale((int) s_pp.scopeR[s * 10]);
                int step = smp >> 11;
                if (step < -8) step = -8;
                if (step >  8) step =  8;
                if ((s & 7) == 0 && (step > 1 || step < -1)) {
                    s_pp.turtleDir = (uint8_t) ((s_pp.turtleDir + 1) & 3u);
                }
                int mag = (step >= 0 ? step + 3 : -step + 3);
                if (s_pp.turtleDir == 0)      s_pp.turtleX += mag;
                else if (s_pp.turtleDir == 1) s_pp.turtleY += mag;
                else if (s_pp.turtleDir == 2) s_pp.turtleX -= mag;
                else                          s_pp.turtleY -= mag;

                if (s_pp.turtleX < 14)  { s_pp.turtleX = 14;  s_pp.turtleDir = 0; }
                if (s_pp.turtleX > 306) { s_pp.turtleX = 306; s_pp.turtleDir = 2; }
                if (s_pp.turtleY < 14)  { s_pp.turtleY = 14;  s_pp.turtleDir = 1; }
                if (s_pp.turtleY > 226) { s_pp.turtleY = 226; s_pp.turtleDir = 3; }

                ppSolid(chain, s_pp.turtleX - 5, s_pp.turtleY - 5, 10, 10, pink);
            }
            break;
        }

        case 18: {
            /* BIOS pos 20 / ID 20: vertical aurora hourglass column (24 rows x 10px = 240px) + white contours. */
            Pu20Rgb topC = ppScopePaletteColor(0);
            Pu20Rgb botC = (s_pp.scopePalette == 1)
                               ? (Pu20Rgb){  48, 128, 255 }
                               : (Pu20Rgb){ 192,  24, 255 };
            int prevL = 160, prevR = 160, prevY = 0;
            for (int row = 0; row < 24; row++) {
                int y0    = row * 10;
                int y1    = y0 + 10;
                int smp   = ppGainScale((int) s_pp.scopeL[(row * 10) % 256]);
                int amp   = (smp < 0 ? -smp : smp) / 720;
                int dist  = (row < 12) ? (12 - row) : (row - 12);
                int coreW = ppClamp(4 + dist * 2 + amp / 2, 4, 38);
                int glowW = ppClamp(coreW + 30 + energy, 14, 108);
                Pu20Rgb mid;
                pu20Blend(&topC, &botC, (row * 255) / 23, &mid);
                pu20DrawGradientQuad(chain, 160 - glowW, y0, 160 - coreW, y1, zero, mid, zero, mid, false);
                pu20DrawGradientQuad(chain, 160 - coreW, y0, 160 + coreW, y1, mid, mid, mid, mid, false);
                pu20DrawGradientQuad(chain, 160 + coreW, y0, 160 + glowW, y1, mid, zero, mid, zero, false);
                if (row > 0) {
                    pu20DrawGradientLine(chain, prevL, prevY, ppWhite, 160 - coreW, y0, ppWhite, false);
                    pu20DrawGradientLine(chain, prevR, prevY, ppWhite, 160 + coreW, y0, ppWhite, false);
                }
                prevL = 160 - coreW; prevR = 160 + coreW; prevY = y0;
            }
            break;
        }

        case 19: {
            /* BIOS pos 21 / ID 14: 3D stereo block spiral (1:1 square pixels). */
            Pu20Rgb c0 = ppScopePaletteColor(0);
            Pu20Rgb c1 = ppScopePaletteColor(2);
            pu20SetChainBlend(chain, GP0_BLEND_SEMITRANS);
            for (int i = 0; i < 128; i++) {
                int dx = ppGainScale((int) s_pp.scopeL[i * 2]) >> 7;
                int dy = ppGainScale((int) s_pp.scopeR[i * 2]) >> 7;
                int sz = (int) (ppISqrtU32((uint32_t) (dx * dx + dy * dy)) >> 3);
                int cx = ppClamp(160 + dx, 12, 308);
                int cy = ppClamp(120 + dy, 12, 228);
                if (sz < 2) sz = 2;
                if (sz > 10) sz = 10;
                pu20DrawGradientQuad(chain, cx - sz, cy - sz, cx + sz, cy + sz,
                                     c0, c1, c0, c1, true);
            }
            break;
        }

        case 20: {
            /* BIOS pos 22 / ID 21: 6 rotating conical frequency discs (true 1:1 circles!). */
            for (int b = 0; b < 6; b++) {
                int lvl = ((int) s_pp.spectrum[b * 4] +
                           s_pp.spectrum[b * 4 + 1] +
                           s_pp.spectrum[b * 4 + 2] +
                           s_pp.spectrum[b * 4 + 3]) / 4;
                int cx  = 59 + b * 40;
                int r   = ppClamp(9 + (lvl * 22) / 80, 9, 32);
                int ang = (phase * 4 + b * 55) % 360;
                pu20DrawShadedDisc(chain, cx, 120, r, ppScopePaletteColor(b), ang, 20);
            }
            break;
        }

        default: {
            /* BIOS pos 23 / ID 22: circular red radar (true 1:1 circle!). */
            int angle0 = (phase * 4) % 360;
            Pu20Rgb c     = ppScopePaletteColor(0);
            Pu20Rgb sweep = { (uint8_t)(c.r / 4), (uint8_t)(c.g / 4), (uint8_t)(c.b / 4) };
            Pu20Rgb dim   = { (uint8_t)(c.r / 12), (uint8_t)(c.g / 12), (uint8_t)(c.b / 12) };

            pu20SetChainBlend(chain, GP0_BLEND_ADD);
            for (int w = 0; w < 4; w++) {
                int a0 = angle0 + w * 10;
                int a1 = a0 + 10;
                int x1 = 160 + ((108 * pu20CosQ12(a0)) >> 12);
                int y1 = 120 + ((108 * pu20SinQ12(a0)) >> 12);
                int x2 = 160 + ((108 * pu20CosQ12(a1)) >> 12);
                int y2 = 120 + ((108 * pu20SinQ12(a1)) >> 12);
                Pu20Rgb col = (w == 3) ? sweep : dim;
                pu20DrawGradientTriangle(chain, 160, 120, zero, x1, y1, col, x2, y2, col, true);
            }
            for (int s = 0; s < 128; s++) {
                int a   = (s * 360) / 128 + angle0;
                int sum = ppGainScale((int) s_pp.scopeL[s * 2]) +
                          ppGainScale((int) s_pp.scopeR[s * 2]);
                int r   = ppClamp(58 + (sum >> 10) + energy, 22, 106);
                int x   = 160 + ((r * pu20CosQ12(a)) >> 12);
                int y   = 120 + ((r * pu20SinQ12(a)) >> 12);
                ppSolid(chain, x - 2, y - 2, 4, 4, c);
            }
            pu20DrawFlatDisc(chain, 160, 120, 17, zero);
            break;
        }
    }

    s_pp.scopeFrame++;
    s_pp.scopePhase += (uint32_t) speed;
}

/* Input -------------------------------------------------------------------- */

static void ppOpenKeypad(void) {
    if (!ppHasAudioTracks()) {
        ppToast("SEM CD DE \xC1" "UDIO");
        return;
    }
    s_pp.keypad      = true;
    s_pp.entry       = 0;
    s_pp.entryCount  = 0;
    s_pp.keypadIndex = 0;
    s_pp.focus       = PP_FOCUS_KEYPAD;
}

static void ppActivateTransport(int idx) {
    switch (idx) {
        case 0: /* NEXT */
            ppManualNext(1);
            ppSetSelection(s_pp.curTrack);
            break;
        case 1: /* FF */
            ppSeekRelative(5);
            break;
        case 2: /* PLAY */
            if (s_pp.paused) {
                ppResume();
            } else {
                uint8_t t = ppAudioReady() ? s_pp.curTrack : s_pp.selTrack;
                if (s_pp.dataTrack[t]) t = ppNextAudio(t, 1);
                ppPlayTrack(t);
            }
            break;
        case 3: /* STOP */
            ppStop();
            break;
        case 4: /* PAUSE */
            if (s_pp.playing && !s_pp.paused) ppPause();
            else if (s_pp.paused) ppResume();
            break;
        case 5: /* REW */
            ppSeekRelative(-5);
            break;
        default: /* PREV */
            ppManualNext(-1);
            ppSetSelection(s_pp.curTrack);
            break;
    }
}

static void ppActivateMenu(void) {
    switch (s_pp.menuIndex) {
        case PP_MENU_CONTINUE:
            s_pp.shuffle = false;
            s_pp.program = false;
            ppSetTrack(s_pp.selTrack);
            break;

        case PP_MENU_SHUFFLE:
            s_pp.shuffle = !s_pp.shuffle;
            if (s_pp.shuffle) s_pp.program = false;
            ppToast(s_pp.shuffle ? "ALEAT\xD3RIO ON" : "ALEAT\xD3RIO OFF");
            break;

        case PP_MENU_PROGRAM:
            s_pp.program = !s_pp.program;
            if (s_pp.program) {
                s_pp.shuffle = false;
                ppProgramEnable();
            }
            ppToast(s_pp.program ? "PROGRAMAR ON" : "PROGRAMAR OFF");
            break;

        case PP_MENU_REPEAT:
            s_pp.repeatMode = (uint8_t) ((s_pp.repeatMode + 1) % 3);
            ppToast(s_pp.repeatMode == PP_REPEAT_OFF ? "REPETIR OFF" :
                    s_pp.repeatMode == PP_REPEAT_ONE ? "REPETIR 1" :
                                                       "REPETIR TUDO");
            break;

        case PP_MENU_TIME:
            s_pp.timeMode = (uint8_t) ((s_pp.timeMode + 1) % 3);
            ppToast(s_pp.timeMode == 0 ? "TEMPO" :
                    s_pp.timeMode == 1 ? "REST. FAIXA" : "REST. DISCO");
            break;

        case PP_MENU_EXIT:
            s_pp.exitFade = 14;
            return;
    }
    s_pp.menuClickIdx   = (int8_t) s_pp.menuIndex;
    s_pp.menuClickTimer = 10;
}

static void ppHandleKeypad(uint16_t pressed) {
    if (pressed & (BUTTON_MASK_CIRCLE | BUTTON_MASK_TRIANGLE)) {
        s_pp.keypad = false;
        s_pp.focus  = PP_FOCUS_GRID;
        return;
    }

    if (pressed & BUTTON_MASK_LEFT) {
        if ((s_pp.keypadIndex % 5) == 0) {
            s_pp.keypad = false;
            s_pp.focus  = PP_FOCUS_GRID;
            return;
        }
        s_pp.keypadIndex--;
    }
    if (pressed & BUTTON_MASK_RIGHT) {
        if ((s_pp.keypadIndex % 5) == 4) {
            s_pp.keypad = false;
            s_pp.focus  = PP_FOCUS_GRID;
            return;
        }
        s_pp.keypadIndex++;
    }
    if (pressed & BUTTON_MASK_UP) {
        if (s_pp.keypadIndex >= 5) s_pp.keypadIndex -= 5;
    }
    if (pressed & BUTTON_MASK_DOWN) {
        if (s_pp.keypadIndex < 5) s_pp.keypadIndex += 5;
    }

    if (pressed & BUTTON_MASK_X) {
        static const int layout[PP_KEYPAD_DIGITS] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };
        int digit = layout[s_pp.keypadIndex];

        if (s_pp.entryCount == 0 && digit == 0) return;

        s_pp.entry = (uint8_t) (s_pp.entry * 10 + digit);
        s_pp.entryCount++;

        if (s_pp.entryCount >= 2 || s_pp.entry * 10 > s_pp.lastTrack) {
            int track = s_pp.entry;

            if (track >= (int) s_pp.firstTrack &&
                track <= (int) s_pp.lastTrack && !s_pp.dataTrack[track]) {
                ppPlayTrack((uint8_t) track);
                ppSetSelection((uint8_t) track);
            } else {
                ppToast("FAIXA INV\xC1LIDA");
            }
            s_pp.keypad = false;
            s_pp.focus  = PP_FOCUS_GRID;
        }
    }
}

static void ppHandlePlayerInput(uint16_t pressed, uint16_t buttons) {
    static uint8_t holdL2 = 0;
    static uint8_t holdR2 = 0;

    if (s_pp.keypad) {
        ppHandleKeypad(pressed);
        return;
    }

    if (pressed & BUTTON_MASK_SELECT) {
        s_pp.scope      = true;
        s_pp.scopeFrame = 0;
        s_pp.scopeBuf   = 0;
        ppSetScopeVideoMode();
        return;
    }

    if (pressed & BUTTON_MASK_START) {
        if (s_pp.playing && !s_pp.paused) {
            ppPause();
        } else if (s_pp.paused) {
            ppResume();
        } else {
            uint8_t t = s_pp.selTrack;
            if (s_pp.dataTrack[t]) t = ppNextAudio(t, 1);
            ppPlayTrack(t);
        }
        return;
    }

    if (pressed & BUTTON_MASK_CIRCLE) {
        s_pp.exitFade = 14;
        return;
    }

    if (pressed & BUTTON_MASK_TRIANGLE) {
        ppOpenKeypad();
        return;
    }

    if (s_pp.focus == PP_FOCUS_GRID && s_pp.catalogCount > 0) {
        if (pressed & BUTTON_MASK_L1) {
            if (s_pp.listScroll >= PP_LIST_VISIBLE) {
                s_pp.listScroll = (uint8_t) (s_pp.listScroll - PP_LIST_VISIBLE);
            } else {
                s_pp.listScroll = 0;
            }
            s_pp.listCursor = s_pp.listScroll;
            s_pp.selTrack   = s_catalog[s_pp.listCursor].trackNum;
            return;
        }
        if (pressed & BUTTON_MASK_R1) {
            if ((int) s_pp.listScroll + PP_LIST_VISIBLE < (int) s_pp.catalogCount) {
                s_pp.listScroll = (uint8_t) (s_pp.listScroll + PP_LIST_VISIBLE);
            } else {
                s_pp.listScroll = 0;
            }
            s_pp.listCursor = s_pp.listScroll;
            s_pp.selTrack   = s_catalog[s_pp.listCursor].trackNum;
            return;
        }
    } else {
        if (pressed & BUTTON_MASK_L1) {
            ppManualNext(-1);
            ppSetSelection(s_pp.curTrack);
        }
        if (pressed & BUTTON_MASK_R1) {
            ppManualNext(1);
            ppSetSelection(s_pp.curTrack);
        }
    }

    if (pressed & BUTTON_MASK_L2) {
        ppSeekRelative(-5);
        holdL2 = 20;
    } else if ((buttons & BUTTON_MASK_L2) && holdL2 > 0) {
        if (--holdL2 == 0) {
            ppSeekRelative(-5);
            holdL2 = 12;
        }
    } else {
        holdL2 = 0;
    }

    if (pressed & BUTTON_MASK_R2) {
        ppSeekRelative(5);
        holdR2 = 20;
    } else if ((buttons & BUTTON_MASK_R2) && holdR2 > 0) {
        if (--holdR2 == 0) {
            ppSeekRelative(5);
            holdR2 = 12;
        }
    } else {
        holdR2 = 0;
    }

    if (s_pp.focus == PP_FOCUS_TRANSPORT) {
        if (pressed & BUTTON_MASK_UP) {
            s_pp.transportIndex = (uint8_t) ((s_pp.transportIndex + PP_TRANSPORT_COUNT - 1)
                                             % PP_TRANSPORT_COUNT);
        }
        if (pressed & BUTTON_MASK_DOWN) {
            s_pp.transportIndex = (uint8_t) ((s_pp.transportIndex + 1) % PP_TRANSPORT_COUNT);
        }
        if (pressed & BUTTON_MASK_RIGHT) {
            if (s_pp.transportIndex >= 4 || !ppHasAudioTracks()) {
                s_pp.focus     = PP_FOCUS_MENU;
                s_pp.menuIndex = (s_pp.transportIndex >= 5) ? PP_MENU_REPEAT
                                                            : PP_MENU_CONTINUE;
            } else {
                s_pp.focus     = PP_FOCUS_GRID;
                s_pp.gridIndex = (uint8_t) ((s_pp.transportIndex % PP_GRID_ROWS) * PP_GRID_COLS);
            }
            return;
        }
        if (pressed & BUTTON_MASK_X) {
            ppActivateTransport(s_pp.transportIndex);
        }
    } else if (s_pp.focus == PP_FOCUS_GRID) {
        if (s_pp.catalogCount == 0) {
            s_pp.focus     = PP_FOCUS_MENU;
            s_pp.menuIndex = PP_MENU_CONTINUE;
            return;
        }

        if (pressed & BUTTON_MASK_UP) {
            if (s_pp.listCursor > 0) {
                s_pp.listCursor--;
                if (s_pp.listCursor < s_pp.listScroll) {
                    s_pp.listScroll = s_pp.listCursor;
                }
            } else {
                s_pp.listCursor = (uint8_t) (s_pp.catalogCount - 1);
                s_pp.listScroll = (s_pp.catalogCount > PP_LIST_VISIBLE)
                                      ? (uint8_t) (s_pp.catalogCount - PP_LIST_VISIBLE)
                                      : 0;
            }
        }
        if (pressed & BUTTON_MASK_DOWN) {
            if ((int) s_pp.listCursor + 1 < (int) s_pp.catalogCount) {
                s_pp.listCursor++;
                if ((int) s_pp.listCursor >= (int) s_pp.listScroll + PP_LIST_VISIBLE) {
                    s_pp.listScroll = (uint8_t) (s_pp.listCursor - PP_LIST_VISIBLE + 1);
                }
            } else {
                s_pp.focus     = PP_FOCUS_MENU;
                s_pp.menuIndex = PP_MENU_PROGRAM;
                return;
            }
        }
        if (pressed & BUTTON_MASK_LEFT) {
            if (s_pp.listScroll > 0) {
                /* Page back inside SD:/PicoPlayer/ list; only jump to menu
                 * buttons when already on page 1 (listScroll == 0). */
                int relRow = (int) s_pp.listCursor - (int) s_pp.listScroll;
                if (s_pp.listScroll >= PP_LIST_VISIBLE) {
                    s_pp.listScroll = (uint8_t) (s_pp.listScroll - PP_LIST_VISIBLE);
                } else {
                    s_pp.listScroll = 0;
                }
                s_pp.listCursor = (uint8_t) (s_pp.listScroll + relRow);
                if (s_pp.listCursor >= s_pp.catalogCount) {
                    s_pp.listCursor = (uint8_t) (s_pp.catalogCount - 1);
                }
            } else {
                s_pp.focus     = PP_FOCUS_MENU;
                s_pp.menuIndex = PP_MENU_PROGRAM;
                return;
            }
        }
        if (pressed & BUTTON_MASK_RIGHT) {
            /* Advance to next page of SD:/PicoPlayer/ list */
            int relRow = (int) s_pp.listCursor - (int) s_pp.listScroll;
            if ((int) s_pp.listScroll + PP_LIST_VISIBLE < (int) s_pp.catalogCount) {
                s_pp.listScroll = (uint8_t) (s_pp.listScroll + PP_LIST_VISIBLE);
            } else {
                s_pp.listScroll = 0;
            }
            s_pp.listCursor = (uint8_t) (s_pp.listScroll + relRow);
            if (s_pp.listCursor >= s_pp.catalogCount) {
                s_pp.listCursor = (uint8_t) (s_pp.catalogCount - 1);
            }
        }

        if (s_pp.listCursor < s_pp.catalogCount) {
            uint8_t track = s_catalog[s_pp.listCursor].trackNum;
            s_pp.selTrack = track;

            if (pressed & BUTTON_MASK_X) {
                s_pp.trackClickIdx   = (int8_t) s_pp.listCursor;
                s_pp.trackClickTimer = 10;
                if (s_pp.program) {
                    ppToggleProgramTrack(track);
                } else {
                    ppPlayTrack(track);
                }
            }
        }
    } else if (s_pp.focus == PP_FOCUS_MENU) {
        int row = (s_pp.menuIndex >= PP_MENU_REPEAT) ? 1 : 0;
        int col = s_pp.menuIndex - (row ? PP_MENU_REPEAT : PP_MENU_CONTINUE);

        if (pressed & BUTTON_MASK_LEFT) {
            if (col == 0) {
                s_pp.focus          = PP_FOCUS_TRANSPORT;
                s_pp.transportIndex = row ? 6 : 5;
                return;
            }
            col--;
        }
        if (pressed & BUTTON_MASK_RIGHT) {
            if (col < 2) {
                col++;
            } else if (s_pp.catalogCount > 0) {
                s_pp.focus = PP_FOCUS_GRID;
                return;
            }
        }

        if (pressed & BUTTON_MASK_UP) {
            if (row) {
                row = 0;
            } else if (s_pp.catalogCount > 0) {
                s_pp.focus = PP_FOCUS_GRID;
                return;
            }
        }
        if (pressed & BUTTON_MASK_DOWN) {
            if (!row) row = 1;
        }

        s_pp.menuIndex = (uint8_t) ((row ? PP_MENU_REPEAT : PP_MENU_CONTINUE) + col);

        if (pressed & BUTTON_MASK_X) {
            ppActivateMenu();
        }
    }
}

static void ppHandleScopeInput(uint16_t pressed, uint16_t buttons) {
    (void) buttons;

    /* Authentic PU-20 BIOS SoundScope controls:
     *   SELECT      : exit back to PicoPlayer
     *   LEFT/RIGHT  : previous / next visualizer mode (12 modes)
     *   UP/DOWN     : increase / decrease response speed (1..8)
     *   CIRCLE      : cycle colour palette (5 palettes)
     *   SQUARE      : toggle phosphor trails on/off
     *   TRIANGLE    : next visualizer mode
     *   L1/R1       : previous / next track
     *   L2/R2       : seek -5s / +5s
     *   START       : play / pause */
    if (pressed & BUTTON_MASK_SELECT) {
        s_pp.scope = false;
        ppSetVideoMode(true, true);
        s_vis.bgDirty = true;
        return;
    }

    if (pressed & BUTTON_MASK_LEFT) {
        s_pp.scopeMode = (uint8_t) ((s_pp.scopeMode + PP_SCOPE_MODES - 1) % PP_SCOPE_MODES);
        s_pp.scopeFrame = 0;
    }
    if (pressed & (BUTTON_MASK_RIGHT | BUTTON_MASK_TRIANGLE)) {
        s_pp.scopeMode = (uint8_t) ((s_pp.scopeMode + 1) % PP_SCOPE_MODES);
        s_pp.scopeFrame = 0;
    }

    if (pressed & BUTTON_MASK_UP) {
        if (s_pp.scopeSpeed < 8) s_pp.scopeSpeed++;
    }
    if (pressed & BUTTON_MASK_DOWN) {
        if (s_pp.scopeSpeed > 1) s_pp.scopeSpeed--;
    }

    if (pressed & BUTTON_MASK_CIRCLE) {
        s_pp.scopePalette = (uint8_t) ((s_pp.scopePalette + 1) % PP_SCOPE_PALETTES);
    }

    if (pressed & BUTTON_MASK_SQUARE) {
        s_pp.scopeTrailLevel = (uint8_t) ((s_pp.scopeTrailLevel + 1) & 3u);
        s_pp.scopeTrails     = (s_pp.scopeTrailLevel > 0);
        s_pp.scopeFrame      = 0;
    }

    if (pressed & BUTTON_MASK_L1) {
        ppManualNext(-1);
        ppSetSelection(s_pp.curTrack);
    }
    if (pressed & BUTTON_MASK_R1) {
        ppManualNext(1);
        ppSetSelection(s_pp.curTrack);
    }
    if (pressed & BUTTON_MASK_L2) ppSeekRelative(-5);
    if (pressed & BUTTON_MASK_R2) ppSeekRelative(5);
    if (pressed & BUTTON_MASK_START) {
        if (s_pp.playing && !s_pp.paused) ppPause();
        else if (s_pp.paused) ppResume();
        else ppPlayTrack(s_pp.selTrack);
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

/* Wire format matching include/pico_audio.h (110 bytes packed) */
typedef struct __attribute__((packed)) {
    char     title[36];
    char     artist[32];
    char     album[32];
    uint8_t  format;
    uint8_t  trackNum;
    uint16_t bitrateKbps;
    uint16_t sampleRateHz;
    uint32_t lengthSectors;
} PpWireTrack;

static bool s_ppSdMounted = false;
static uint8_t s_ppMetaSecBuf[2352] __attribute__((aligned(4)));
static uint32_t s_sdTrackLen[PP_MAX_TRACKS];

static bool ppLoadTrackArt(uint16_t trackIdx, uint8_t *dstBuf) {
    picoSendCommand(PICO_CMD_PICOPLAYER, (uint16_t)(PICO_PP_GET_ART_MASK | (trackIdx & 0x00FFu)));
    for (uint32_t part = 0; part < PP_ART_PARTS; part++) {
        bool ok = false;
        for (int retry = 0; retry < 15; retry++) {
            memset(s_ppMetaSecBuf, 0, 32);
            startCDROMRead(PICO_LBA_PP_ART_BASE + part, s_ppMetaSecBuf, 1, 2340, true, true);
            if (!waitingForInt5) {
                return false;
            }
            const uint8_t *hdr = s_ppMetaSecBuf + 12;
            if (memcmp(hdr, "DEAD", 4) == 0) {
                return false;
            }
            if (memcmp(hdr, "PART", 4) == 0) {
                uint16_t gotPart = (uint16_t)(hdr[4] | ((uint16_t)hdr[5] << 8));
                if (gotPart == (uint16_t)part) {
                    memcpy(dstBuf + part * PP_ART_PART_BYTES, hdr + 6, PP_ART_PART_BYTES);
                    ok = true;
                    break;
                }
            }
            delayMicroseconds(4000);
        }
        if (!ok) return false;
    }
    return true;
}

static uint16_t ppLoadSdCatalog(void) {
    uint8_t trackArtGroup[PP_MAX_TRACKS];
    memset(trackArtGroup, 0, sizeof(trackArtGroup));
    ppArtFree();

    s_ppSdMounted = false;
    s_pp.catalogCount = 0;

    picoSendCommand(PICO_CMD_PICOPLAYER, PICO_PP_SCAN_SD);

    uint16_t totalTracks = 0;
    bool gotHeader = false;
    int int5Errors = 0;

    for (int retry = 0; retry < 200; retry++) {
        memset(s_ppMetaSecBuf, 0, sizeof(s_ppMetaSecBuf));
        startCDROMRead(PICO_LBA_PP_META_BASE, s_ppMetaSecBuf, 1, 2340, true, true);
        if (!waitingForInt5) {
            if (++int5Errors >= 3) {
                picoSendCommand(PICO_CMD_NONE, 0);
                return 0;
            }
            continue;
        }
        const uint8_t *hdr = s_ppMetaSecBuf + 12;
        if (memcmp(hdr, "PPA1", 4) == 0) {
            totalTracks = (uint16_t)(hdr[4] | ((uint16_t)hdr[5] << 8));
            if (totalTracks > PP_MAX_TRACKS) totalTracks = PP_MAX_TRACKS;
            gotHeader = true;
            break;
        }
        delayMicroseconds(20000);
        (void)getButtons(0, 0); /* Keep BlueRetro SPI bus alive during SD scan */
    }

    if (!gotHeader || totalTracks == 0) {
        picoSendCommand(PICO_CMD_NONE, 0);
        return 0;
    }

    bool anyArt = false;
    uint16_t pages = (uint16_t)((totalTracks + 15u) / 16u);
    for (uint16_t p = 0; p < pages; p++) {
        if (p > 0) {
            bool ok = false;
            for (int r = 0; r < 15; r++) {
                memset(s_ppMetaSecBuf, 0, sizeof(s_ppMetaSecBuf));
                startCDROMRead(PICO_LBA_PP_META_BASE + p, s_ppMetaSecBuf, 1, 2340, true, true);
                if (waitingForInt5 && memcmp(s_ppMetaSecBuf + 12, "PPA1", 4) == 0) {
                    ok = true;
                    break;
                }
                delayMicroseconds(5000);
            }
            if (!ok) break;
        }

        const uint8_t *hdr = s_ppMetaSecBuf + 12;
        uint16_t count = (uint16_t)(hdr[8] | ((uint16_t)hdr[9] << 8));
        const PpWireTrack *wt = (const PpWireTrack *)(hdr + 12);
        for (uint16_t i = 0; i < count && s_pp.catalogCount < PP_MAX_TRACKS; i++) {
            uint16_t catIdx = s_pp.catalogCount++;
            PpTrackMeta *m = &s_catalog[catIdx];
            memset(m, 0, sizeof(*m));
            ppCopyStr(m->title,  sizeof(m->title),  wt[i].title);
            ppCopyStr(m->artist, sizeof(m->artist), wt[i].artist);
            ppCopyStr(m->album,  sizeof(m->album),  wt[i].album);
            uint8_t grp = (uint8_t)wt[i].artist[31];
            if (grp == 0 && (wt[i].format & 0x80u) != 0) grp = 1;
            trackArtGroup[catIdx] = grp;
            if (grp != 0) anyArt = true;
            m->format       = (uint8_t)(wt[i].format & 0x7Fu);
            m->bitrateKbps  = wt[i].bitrateKbps;
            m->sampleRateHz = wt[i].sampleRateHz;
            m->trackNum     = wt[i].trackNum;
            s_sdTrackLen[catIdx] = wt[i].lengthSectors;
        }
    }

    /* Preload unique .cov album art into PS1 RAM BEFORE mounting the virtual
     * Audio CD. Tracks sharing an artGroup (2..254 from index.pp or cover.cov)
     * transfer the cover ONCE — making album startup up to 6x faster! */
    if (anyArt && s_pp.catalogCount > 0) {
        int8_t groupSlot[256];
        memset(groupSlot, -1, sizeof(groupSlot));
        uint8_t *tmpArt = (uint8_t *)malloc(PP_ART_RAW_BYTES);
        s_artCache = (uint8_t *)malloc((size_t)PP_ART_MAX_SLOTS * PP_ART_RAW_BYTES);
        if (tmpArt && s_artCache) {
            for (uint16_t i = 0; i < s_pp.catalogCount; i++) {
                (void)getButtons(0, 0); /* Prevent BlueRetro 3s idle reset during cover preload */
                uint8_t grp = trackArtGroup[i];
                if (grp == 0) continue;
                if (grp >= 2u && groupSlot[grp] >= 0) {
                    s_trackArtSlot[i] = groupSlot[grp];
                    continue;
                }
                if (!ppLoadTrackArt(i, tmpArt)) continue;

                int8_t matched = -1;
                for (uint8_t s = 0; s < s_artSlotCount; s++) {
                    if (memcmp(s_artCache + (size_t)s * PP_ART_RAW_BYTES, tmpArt, 16896u) == 0) {
                        matched = (int8_t)s;
                        break;
                    }
                }
                if (matched >= 0) {
                    s_trackArtSlot[i] = matched;
                } else if (s_artSlotCount < PP_ART_MAX_SLOTS) {
                    memcpy(s_artCache + (size_t)s_artSlotCount * PP_ART_RAW_BYTES,
                           tmpArt, PP_ART_RAW_BYTES);
                    s_trackArtSlot[i] = (int8_t)s_artSlotCount++;
                }
                if (grp >= 2u && s_trackArtSlot[i] >= 0) {
                    groupSlot[grp] = s_trackArtSlot[i];
                }
            }
        }
        if (tmpArt) free(tmpArt);
        if (s_artSlotCount == 0) {
            ppArtFree();
        }
    }

    if (s_pp.catalogCount > 0) {
        picoSendCommand(PICO_CMD_PICOPLAYER, PICO_PP_MOUNT_SD);
        delayMicroseconds(120000);
        updateCDROM_TOC();
        s_ppSdMounted = true;
    } else {
        picoSendCommand(PICO_CMD_NONE, 0);
    }
    return s_pp.catalogCount;
}

void picoplayer_enter(void) {
    if (s_pp.active) return;

    memset(&s_pp, 0, sizeof(s_pp));
    memset(&s_vis, 0, sizeof(s_vis));
    s_vis.bgDirty = true;

    s_pp.active         = true;
    s_pp.fps            = ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) ==
                           GP1_STAT_FB_MODE_PAL) ? 50 : 60;
    s_pp.focus          = PP_FOCUS_MENU;
    s_pp.gridIndex      = 0;
    s_pp.menuIndex      = PP_MENU_TIME;
    s_pp.transportIndex = 2; /* PLAY */
    s_pp.keypad         = false;
    s_pp.entry          = 0;
    s_pp.entryCount     = 0;
    s_pp.selTrack       = 1;
    s_pp.curTrack       = 1;
    s_pp.repeatMode     = PP_REPEAT_OFF;
    s_pp.timeMode       = 0;
    s_pp.scope           = false;
    s_pp.scopeMode       = 0;
    s_pp.scopePalette    = 0;
    s_pp.scopeSpeed      = 4;
    s_pp.scopeTrailLevel = 2; /* medium phosphor trails by default */
    s_pp.scopeTrails     = true;
    s_pp.scopeFrame      = 0;
    s_pp.scopePhase      = 0;
    s_pp.rng            = 0x13579BDFu;
    s_pp.discAngle      = 300;

    /* Keep the PicoMenu "Aguarde, carregando..." screen visible on TV while
     * scanning SD:/PicoPlayer/, preloading album art and reading the virtual TOC! */
    uint16_t sdCount = ppLoadSdCatalog();

    for (int attempt = 0; attempt < 8 && !s_pp.tocValid; attempt++) {
        if (ppReadToc()) {
            s_pp.tocValid = true;
        } else {
            delayMicroseconds(50000);
        }
    }

    /* Now switch cleanly to 640x480i and upload PU-20 textures (~10 ms blanked) */
    waitForDMADone();
    waitForGP0Ready();
    GPU_GP1 = gp1_dispBlank(true);
    ppVramClear(0, 0, 640, 480, 0, 0, 32);

    pu20SkinInit();
    pu20SkinUploadAssets();

    ppSetVideoMode(true, false);

    /* If SD:/PicoPlayer/ tracks were loaded via PPA1, synthesize exact TOC
     * timings as fallback so we never discard the user's SD catalogue. */
    if (sdCount > 0 && (!s_pp.tocValid || s_pp.lastTrack < sdCount)) {
        uint32_t cur = 150u;
        s_pp.firstTrack = 1;
        s_pp.lastTrack  = (uint8_t) sdCount;
        for (uint16_t i = 0; i < sdCount; i++) {
            uint8_t t = (uint8_t)(i + 1);
            uint32_t len = s_sdTrackLen[i] ? s_sdTrackLen[i] : (75u * 180u);
            s_pp.startFrame[t]  = cur;
            s_pp.lengthFrame[t] = len;
            s_pp.dataTrack[t]   = false;
            cur += len;
        }
        s_pp.discEndFrame = cur;
        s_pp.tocValid = true;
    }

    if (s_pp.tocValid) {
        if (sdCount == 0) {
            s_pp.dataDisc = ppProbeDataTrack1();
            if (s_pp.dataDisc) {
                s_pp.dataTrack[s_pp.firstTrack] = true;
            }
        } else {
            s_pp.dataDisc = false;
        }

        if (ppHasAudioTracks()) {
            uint8_t firstAudio = s_pp.firstTrack;
            if (s_pp.dataTrack[firstAudio]) {
                firstAudio = ppNextAudio(firstAudio, 1);
            }

            if (sdCount == 0) {
                ppBuildCatalog();
            }
            s_pp.selTrack = firstAudio;
            s_pp.curTrack = firstAudio;
            s_pp.focus    = PP_FOCUS_GRID;
            ppSetSelection(firstAudio);

            for (int t = s_pp.firstTrack; t <= s_pp.lastTrack; t++) {
                s_pp.programmed[t] = !s_pp.dataTrack[t];
            }
            ppProgramRebuild();
        } else {
            s_pp.catalogCount = 0;
            s_pp.selTrack = 0;
            s_pp.curTrack = 0;
            s_pp.focus    = PP_FOCUS_MENU;
            s_pp.menuIndex = PP_MENU_CONTINUE;
        }
    } else {
        s_pp.catalogCount = 0;
        s_pp.selTrack = 0;
        s_pp.curTrack = 0;
        s_pp.focus    = PP_FOCUS_MENU;
        s_pp.menuIndex = PP_MENU_CONTINUE;
    }

    ppAudioEnable(false);
    GPU_GP1 = gp1_dispBlank(false);
}

void picoplayer_present_vblank(void) {
    if (s_pp.active && s_pp.scope && s_pp.hasPendingDisp) {
        GPU_GP1 = gp1_fbOffset(s_pp.pendingDispX, 0);
        s_pp.hasPendingDisp = false;
    }
}

void picoplayer_begin_exit(void) {
    if (!s_pp.active) return;

    /* Pause (not STOP) so the HC05 spindle stays responsive for TOC reload,
     * mute audio, free album art and switch cleanly to 320x240p so PicoMenu
     * can display its "PicoMenu / Aguarde, carregando..." screen immediately. */
    if (ppCommand(CDROM_CMD_PAUSE, NULL, 0)) {
        ppWaitInt2(250);
    }
    ppAudioEnable(false);
    ppArtFree();
    ppSetVideoMode(false, true);
}

void picoplayer_finish_exit(void) {
    if (!s_pp.active) return;

    if (s_ppSdMounted) {
        picoSendCommand(PICO_CMD_PICOPLAYER, PICO_PP_EXIT);
        delayMicroseconds(25000);
        if (ppCommand(CDROM_CMD_INIT, NULL, 0)) {
            ppWaitInt2(1500);
        }
        s_ppSdMounted = false;
    }
    s_pp.active = false;
}

void picoplayer_exit(void) {
    picoplayer_begin_exit();
    picoplayer_finish_exit();
}

bool picoplayer_update_and_draw(DMAChain *chain, const TextureInfo *font,
                                uint16_t buttons, uint16_t prevButtons) {
    if (!s_pp.active) return true;

    /* Issue GP1 SetDisplayStartAddress once per frame in 640x480i mode
     * (mirrors Sony BIOS PutDispEnv) so emulators/DuckStation track the
     * true 60 FPS internal game frame rate instead of 0 FPS.
     * In SoundScope mode, picoplayer_present_vblank() flips inside VBlank. */
    if (!s_pp.scope) {
        GPU_GP1 = gp1_fbOffset(0, 0);
    }

    if (s_pp.exitFade > 0) {
        /* Smoothly fade ONLY the word "SAIR" from white -> black over the
         * rainbow splat (~0.23 s), keeping the rest of the screen intact! */
        s_pp.exitFade--;
        if (s_pp.exitFade == 0) {
            s_pp.requestExit = true;
            return true;
        }
        if (s_vis.cursorSaved) {
            ppVramBlit(chain, 960, 0, s_vis.cursorSaveX, s_vis.cursorSaveY, 36, 36);
            s_vis.cursorSaved = false;
        }
        {
            uint8_t v = (s_pp.exitFade > 2)
                            ? (uint8_t) (((int) (s_pp.exitFade - 2) * 255) / 12)
                            : 0u;
            Pu20Rgb exitCol = { v, v, v };
            pu20DrawSplat(chain, 480, 410, 214, 58, 0);
            pu20DrawFont(chain, 1, 480 - pu20FontWidth(1, "SAIR") / 2, 401,
                         "SAIR", exitCol);
        }
        return false;
    }

    uint16_t pressed = (uint16_t)(~prevButtons & buttons);

    ppTick();
    if (s_pp.scope) {
        ppReadCapture();
    }

    if (s_pp.toastTimer > 0) s_pp.toastTimer--;
    if (s_pp.menuClickTimer > 0) s_pp.menuClickTimer--;
    if (s_pp.trackClickTimer > 0) s_pp.trackClickTimer--;

    if (s_pp.playing && !s_pp.paused) {
        s_pp.discAngle = (s_pp.discAngle + 6) % 360;
    }

    if (s_pp.scope) {
        ppHandleScopeInput(pressed, buttons);
        if (s_pp.scope) ppComputeSpectrum();
    } else {
        ppHandlePlayerInput(pressed, buttons);
    }

    if (s_pp.scope) {
        ppDrawScope(chain);
    } else {
        ppDrawPlayer(chain, font);
    }

    return s_pp.requestExit;
}
