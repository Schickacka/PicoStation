#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "gpu.h"

typedef struct {
    uint8_t r, g, b;
} RgbColor;

typedef struct {
    int16_t h;
    uint8_t s;
    uint8_t v;
} HsvColor;

typedef struct {
    RgbColor bg;
    RgbColor frame;
    RgbColor filelist;
    RgbColor text;
    RgbColor cursor;
    HsvColor cursorHsv;
    uint8_t  cursorVMax;
    int8_t   cursorDir;
    uint8_t  style;          // 0 = list + cover (default), 1 = full-width list
    bool     wallpaper;      // 1 = load background.raw
    uint16_t openMask;       // default BUTTON_MASK_X (0x4000)
    uint8_t  openGlyph;      // default 0x91
    uint16_t backMask;       // default BUTTON_MASK_CIRCLE (0x2000)
    uint8_t  backGlyph;      // default 0x94
    bool     optOverclock;   // default true
    bool     optEdc;         // default true
    bool     optBootsector;  // default true on SCEI BIOS, false on A/D/E
    bool     hasWallpaperTex;
    TextureInfo bgTexLeft;   // 256x240 @ (256, 256)
    TextureInfo bgTexRight;  // 64x240  @ (512, 256)
} MenuConfig;

extern MenuConfig g_cfg;

void rc5ConfigInitDefaults(void);
void rc5ConfigLoad(void);
void rc5WallpaperLoad(void);
void rc5RgbToHsv(const RgbColor *rgb, HsvColor *hsv);
void rc5HsvToRgb(const HsvColor *hsv, RgbColor *rgb);
void rc5StepCursorAnimation(void);
