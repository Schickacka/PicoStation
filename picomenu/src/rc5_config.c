#include "rc5_config.h"
#include "rc5_lang.h"
#include "rc5_proto.h"
#include "controller.h"
#include "psxproject/cdrom.h"
#include "psxproject/delay.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

MenuConfig g_cfg;

// Sector buffer + dynamically allocated 77312-byte wallpaper staging buffer (keeps .psexe tiny!)
static uint8_t s_cfgSecBuf[2352] __attribute__((aligned(4)));

void rc5RgbToHsv(const RgbColor *rgb, HsvColor *hsv) {
    uint16_t mn = rgb->r < rgb->g ? rgb->r : rgb->g;
    if (rgb->b < mn) mn = rgb->b;
    uint16_t mx = rgb->r > rgb->g ? rgb->r : rgb->g;
    if (rgb->b > mx) mx = rgb->b;

    hsv->v = (uint8_t)mx;
    uint16_t delta = mx - mn;
    if (mx == 0 || delta == 0) {
        hsv->s = 0;
        hsv->h = 0;
        return;
    }

    hsv->s = (uint8_t)(((uint32_t)delta * 255u) / mx);
    int16_t hue;
    if (mx == rgb->r) {
        hue = (int16_t)(((int32_t)(rgb->g - rgb->b) * 60) / (int32_t)delta);
    } else if (mx == rgb->g) {
        hue = (int16_t)(((int32_t)(rgb->b - rgb->r) * 60) / (int32_t)delta) + 120;
    } else {
        hue = (int16_t)(((int32_t)(rgb->r - rgb->g) * 60) / (int32_t)delta) + 240;
    }
    if (hue < 0) hue += 360;
    hsv->h = hue;
}

void rc5HsvToRgb(const HsvColor *hsv, RgbColor *rgb) {
    if (hsv->s == 0) {
        rgb->r = rgb->g = rgb->b = hsv->v;
        return;
    }
    uint8_t region = (uint8_t)((hsv->h / 60) % 6);
    uint8_t rem = (uint8_t)((hsv->h % 60) * 6);
    uint8_t p = (uint8_t)(((uint32_t)hsv->v * (255u - hsv->s)) >> 8);
    uint8_t q = (uint8_t)(((uint32_t)hsv->v * (255u - (((uint32_t)hsv->s * rem) >> 8))) >> 8);
    uint8_t t = (uint8_t)(((uint32_t)hsv->v * (255u - (((uint32_t)hsv->s * (255u - rem)) >> 8))) >> 8);
    switch (region) {
        case 0:  rgb->r = hsv->v; rgb->g = t;      rgb->b = p;      break;
        case 1:  rgb->r = q;      rgb->g = hsv->v; rgb->b = p;      break;
        case 2:  rgb->r = p;      rgb->g = hsv->v; rgb->b = t;      break;
        case 3:  rgb->r = p;      rgb->g = q;      rgb->b = hsv->v; break;
        case 4:  rgb->r = t;      rgb->g = p;      rgb->b = hsv->v; break;
        default: rgb->r = hsv->v; rgb->g = p;      rgb->b = q;      break;
    }
}

void rc5StepCursorAnimation(void) {
    int nextV = (int)g_cfg.cursorHsv.v + (int)g_cfg.cursorDir;
    if (nextV >= (int)g_cfg.cursorVMax) {
        nextV = g_cfg.cursorVMax;
        g_cfg.cursorDir = -1;
    } else if (nextV <= 0x2E) {
        nextV = 0x2E;
        g_cfg.cursorDir = 1;
    }
    g_cfg.cursorHsv.v = (uint8_t)nextV;
    rc5HsvToRgb(&g_cfg.cursorHsv, &g_cfg.cursor);
}

static void applyButtonRemap(uint32_t bitIndex, bool isBack) {
    if (bitIndex >= 16) return;
    uint16_t mask = (uint16_t)(1u << bitIndex);
    uint8_t glyph = 0;
    switch (mask) {
        case BUTTON_MASK_SQUARE:   glyph = 0x90; break;
        case BUTTON_MASK_X:        glyph = 0x91; break;
        case BUTTON_MASK_CIRCLE:   glyph = 0x94; break;
        case BUTTON_MASK_TRIANGLE: glyph = 0x95; break;
        case BUTTON_MASK_START:    glyph = 0x96; break;
        default: return;
    }
    if (!isBack) {
        g_cfg.openMask  = mask;
        g_cfg.openGlyph = glyph;
    } else {
        g_cfg.backMask  = mask;
        g_cfg.backGlyph = glyph;
    }
}

static bool parseKeyNumber(const char *text, const char *key, uint32_t *outVal) {
    size_t klen = strlen(key);
    const char *p = text;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        const char *lineEnd = p;
        while (*lineEnd && *lineEnd != '\n' && *lineEnd != '\r') lineEnd++;

        if (strncmp(p, key, klen) == 0) {
            const char *v = p + klen;
            while (v < lineEnd && (*v == ' ' || *v == '\t' || *v == '=' || *v == '#')) v++;
            if (v < lineEnd) {
                uint32_t val = 0;
                int base = 10;
                if (v + 2 <= lineEnd && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
                    base = 16;
                    v += 2;
                } else {
                    // Check if hex digits (a-f/A-F) or 6-char RGB hex color
                    const char *scan = v;
                    int digits = 0;
                    bool hasHexAlpha = false;
                    while (scan < lineEnd && *scan != ' ' && *scan != '\t' && *scan != ';' && *scan != '#') {
                        char c = *scan++;
                        if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) hasHexAlpha = true;
                        digits++;
                    }
                    if (hasHexAlpha || digits == 6) base = 16;
                }
                bool any = false;
                while (v < lineEnd) {
                    char c = *v++;
                    int d = -1;
                    if (c >= '0' && c <= '9') d = c - '0';
                    else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
                    else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
                    else break;
                    val = val * (uint32_t)base + (uint32_t)d;
                    any = true;
                }
                if (any) {
                    *outVal = val;
                    return true;
                }
            }
        }
        p = lineEnd;
    }
    return false;
}

void rc5ConfigInitDefaults(void) {
    // PU-20 BIOS Shell Skin palette (overridable via config.ini)
    g_cfg.bg        = (RgbColor){0x08, 0x08, 0x28};
    g_cfg.frame     = (RgbColor){0x31, 0x41, 0xC5};
    g_cfg.filelist  = (RgbColor){0x22, 0x26, 0x62};
    g_cfg.text      = (RgbColor){0xFF, 0xFF, 0xFF};
    g_cfg.cursor    = (RgbColor){0x4C, 0x5E, 0xF0};
    g_cfg.style     = 0;
    g_cfg.wallpaper = false;
    g_cfg.openMask  = BUTTON_MASK_X;
    g_cfg.openGlyph = 0x91;
    g_cfg.backMask  = BUTTON_MASK_CIRCLE;
    g_cfg.backGlyph = 0x94;
    g_cfg.optOverclock  = true;
    g_cfg.optEdc        = true;
    g_cfg.optBootsector = true;
    g_cfg.hasWallpaperTex = false;

    // Exact RC5 BIOS region check at 0xBFC7FF52: disable bootsector fix on A/D/E BIOS, keep ON for J (I)
    uint8_t biosRegion = *(const volatile uint8_t *)0xBFC7FF52u;
    if (biosRegion == 'A' || biosRegion == 'D' || biosRegion == 'E') {
        g_cfg.optBootsector = false;
    }

    rc5RgbToHsv(&g_cfg.cursor, &g_cfg.cursorHsv);
    g_cfg.cursorVMax = g_cfg.cursorHsv.v;
    g_cfg.cursorDir = -1;
}

void rc5ConfigLoad(void) {
    picoSendCommand(PICO_CMD_EXTENDED, (uint16_t)(PICO_EXT_GET_CFG << 1));
    for (int attempt = 0; attempt < 5; attempt++) {
        memset(s_cfgSecBuf, 0, sizeof(s_cfgSecBuf));
        startCDROMRead(PICO_LBA_CFG, s_cfgSecBuf, 1, 2340, true, true);
        if (!waitingForInt5) {
            break;
        }
        const uint8_t *hdr = s_cfgSecBuf + 12;
        if (memcmp(hdr, "DEAD", 4) == 0) {
            break;
        }
        if (memcmp(hdr, "CFG1", 4) == 0) {
            // Exact RC5 layout: payload starts at offset +276 after "CFG1" (0x8003784c - 0x8003772c = 0x120 = 12 + 276)
            char *cfgText = (char *)(s_cfgSecBuf + 12 + 276);
            s_cfgSecBuf[2340 - 1] = '\0';
            uint32_t v = 0;
            if (parseKeyNumber(cfgText, "background", &v)) {
                g_cfg.bg = (RgbColor){(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
            }
            if (parseKeyNumber(cfgText, "frame", &v)) {
                g_cfg.frame = (RgbColor){(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
            }
            if (parseKeyNumber(cfgText, "filelist", &v)) {
                g_cfg.filelist = (RgbColor){(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
            }
            if (parseKeyNumber(cfgText, "cursor", &v)) {
                g_cfg.cursor = (RgbColor){(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
            }
            if (parseKeyNumber(cfgText, "text", &v)) {
                g_cfg.text = (RgbColor){(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
            }
            if (parseKeyNumber(cfgText, "style", &v)) {
                g_cfg.style = (v == 1) ? 1 : 0;
            }
            if (parseKeyNumber(cfgText, "wallpaper", &v)) {
                g_cfg.wallpaper = (v == 1);
            }
            if (parseKeyNumber(cfgText, "actopen", &v)) {
                applyButtonRemap(v, false);
            }
            if (parseKeyNumber(cfgText, "actback", &v)) {
                applyButtonRemap(v, true);
            }
            if (parseKeyNumber(cfgText, "lang", &v)) {
                rc5LangSelect((int)v);
            }
            break;
        }
    }
    picoSendCommand(PICO_CMD_NONE, 0);
    rc5RgbToHsv(&g_cfg.cursor, &g_cfg.cursorHsv);
    g_cfg.cursorVMax = g_cfg.cursorHsv.v;
    g_cfg.cursorDir = -1;
}

void rc5WallpaperLoad(void) {
    if (!g_cfg.wallpaper) return;
    g_cfg.hasWallpaperTex = false;

    uint8_t *bgRawBuf = (uint8_t *)malloc(34u * 2316u);
    if (!bgRawBuf) return;

    picoSendCommand(PICO_CMD_EXTENDED, (uint16_t)(PICO_EXT_GET_BG << 1));
    uint32_t part = 0;
    int retries = 20;
    while (part < 34) {
        memset(s_cfgSecBuf, 0, sizeof(s_cfgSecBuf));
        startCDROMRead(PICO_LBA_BG_BASE + part, s_cfgSecBuf, 1, 2340, true, true);
        const uint8_t *hdr = s_cfgSecBuf + 12;
        if (memcmp(hdr, "DEAD", 4) == 0 || memcmp(hdr, "PART", 4) != 0) {
            break;
        }
        uint16_t gotPart = (uint16_t)(hdr[4] | ((uint16_t)hdr[5] << 8));
        uint16_t gotLen  = (uint16_t)(hdr[6] | ((uint16_t)hdr[7] << 8));
        if (gotPart == part) {
            if (gotLen > 2316) gotLen = 2316;
            memcpy(bgRawBuf + part * 2316u, hdr + 8, gotLen);
            if (part == 33) {
                // Upload 320x240 8bpp wallpaper: first 512 B = CLUT, next 256x240 left, next 64x240 right
                uploadIndexedTexture(&g_cfg.bgTexLeft, bgRawBuf + 512, bgRawBuf,
                                     256, 256, 256, 496, 256, 240, GP0_COLOR_8BPP);
                // Right strip shares the exact same CLUT!
                sendVRAMData(bgRawBuf + 512 + (256 * 240), 512, 256, 64 / 2, 240);
                waitForDMADone();
                g_cfg.bgTexRight.u      = 0;
                g_cfg.bgTexRight.v      = 0;
                g_cfg.bgTexRight.width  = 64;
                g_cfg.bgTexRight.height = 240;
                g_cfg.bgTexRight.page   = gp0_page(512 / 64, 256 / 256, GP0_BLEND_SEMITRANS, GP0_COLOR_8BPP);
                g_cfg.bgTexRight.clut   = g_cfg.bgTexLeft.clut;
                g_cfg.hasWallpaperTex   = true;
                break;
            }
            part++;
        } else {
            if (--retries < 0) break;
        }
    }
    free(bgRawBuf);
    picoSendCommand(PICO_CMD_NONE, 0);
}
