/*
 * PicoStation 2.0 - PU-20 shell skin engine (clean-room reimplementation)
 *
 * Reusable skin primitives that reproduce the look of the PlayStation BIOS
 * PU-20 (SCPH-7000) / PU-22 (SCPH-7500) "CD PLAYER" shell: dark navy gradient
 * backgrounds, bevelled 3D panels, gold selection bars and a 4bpp icon atlas.
 *
 * Nothing in this file was extracted from Sony binaries. All coordinates,
 * colours and icons are an original reconstruction for ps1-bare-metal.
 *
 * VRAM layout (see PU20_BIOS_ANALYSIS.md):
 *   framebuffer A  (0,   0)   320x240 16bpp
 *   framebuffer B  (320, 0)   320x240 16bpp
 *   PicoMenu font  (640, 0)    96x196  8bpp + CLUT (640,196)
 *   PicoMenu logo  (736, 0)   128x20   4bpp + CLUT (736,20)
 *   cover art      (0,   256) 128x128  8bpp + CLUT (0,384)
 *   wallpaper      (256, 256) 320x240 16bpp (optional)
 *   PU-20 icons    (896, 384) 128x64   4bpp + CLUT (960, 384) <-- this module
 *   All PU-20 assets live in x >= 640, y >= 256 (never touching PicoMenu's
 *   font/logo area at x >= 640, y = 0..200).
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gpu.h"
#include "rc5_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PU20_SCREEN_WIDTH  320
#define PU20_SCREEN_HEIGHT 240

#define PU20_ATLAS_X      896
#define PU20_ATLAS_Y      384
#define PU20_ATLAS_WIDTH  128
#define PU20_ATLAS_HEIGHT 64
#define PU20_ATLAS_CLUT_X 960
#define PU20_ATLAS_CLUT_Y 384

#define PU20_ICON_SIZE    16
#define PU20_ICON_COLUMNS 8

/* Palette indices used by the atlas. */
typedef enum {
    PU20_CLEAR = 0,
    PU20_WHITE,
    PU20_LIGHT,
    PU20_MID,
    PU20_DARK,
    PU20_BLUE,
    PU20_CYAN,
    PU20_GREEN,
    PU20_YELLOW,
    PU20_ORANGE,
    PU20_RED,
    PU20_PURPLE,
    PU20_NAVY,
    PU20_SILVER,
    PU20_STEEL,
    PU20_BLACK
} Pu20ColorIndex;

/* Icon slots in the 8x4 atlas (row 0..3, column 0..7). */
typedef enum {
    PU20_ICON_PLAY = 0,
    PU20_ICON_PAUSE,
    PU20_ICON_STOP,
    PU20_ICON_PREV,
    PU20_ICON_NEXT,
    PU20_ICON_FF,
    PU20_ICON_REW,
    PU20_ICON_REPEAT,
    PU20_ICON_REPEAT_ONE,
    PU20_ICON_SHUFFLE,
    PU20_ICON_PROGRAM,
    PU20_ICON_SCOPE,
    PU20_ICON_DISC,
    PU20_ICON_FOLDER,
    PU20_ICON_OPTIONS,
    PU20_ICON_CHEATS,
    PU20_ICON_CREDITS,
    PU20_ICON_CROSS,
    PU20_ICON_CIRCLE,
    PU20_ICON_TRIANGLE,
    PU20_ICON_SQUARE,
    PU20_ICON_L1,
    PU20_ICON_R1,
    PU20_ICON_START,
    PU20_ICON_SELECT,
    PU20_ICON_CLOCK,
    PU20_ICON_SPEAKER,
    PU20_ICON_NOTE,
    PU20_ICON_HOME,
    PU20_ICON_BACK,
    PU20_ICON_CHECK,
    PU20_ICON_ARROW,
    PU20_ICON_COUNT
} Pu20Icon;

typedef enum {
    PU20_PANEL_FLAT = 0,
    PU20_PANEL_RAISED,
    PU20_PANEL_INSET,
    PU20_PANEL_GLASS
} Pu20PanelStyle;

typedef struct {
    int x, y, w, h;
} Pu20Rect;

typedef struct {
    uint8_t r, g, b;
} Pu20Rgb;

/* 15-bit palette shared by every icon in the atlas (index 0 = transparent). */
extern const uint16_t pu20IconPalette[16];

/* Nominal PU-20 shell colours, 24-bit RGB. */
extern const Pu20Rgb pu20BgTop;
extern const Pu20Rgb pu20BgBottom;
extern const Pu20Rgb pu20PanelTop;
extern const Pu20Rgb pu20PanelBottom;
extern const Pu20Rgb pu20EdgeLight;
extern const Pu20Rgb pu20EdgeDark;
extern const Pu20Rgb pu20TextMain;
extern const Pu20Rgb pu20TextDim;
extern const Pu20Rgb pu20AccentBlue;
extern const Pu20Rgb pu20AccentCyan;
extern const Pu20Rgb pu20AccentGold;
extern const Pu20Rgb pu20AccentGreen;
extern const Pu20Rgb pu20AccentRed;
extern const Pu20Rgb pu20AccentPurple;

/* Uploads an indexed texture with exact VRAM word counts (unlike the shared
 * sendVRAMData helper, which truncates indexed uploads). */
void pu20UploadTexture(TextureInfo *info, const void *image, const void *palette,
                       int x, int y, int paletteX, int paletteY,
                       int width, int height, GP0ColorDepth depth);

/* Lifecycle ------------------------------------------------------------- */

/* Resets internal state. Does not touch VRAM. */
void pu20SkinInit(void);

/* Uploads the 128x64 4bpp icon atlas + 16-colour CLUT to VRAM.
 * Must be called once after setupGPU() and before drawing icons. */
void pu20SkinUploadAssets(void);

/* Drawing --------------------------------------------------------------- */

/* Full screen PU-20 gradient with slow floating "CD" spheres. */
void pu20DrawBackground(DMAChain *chain, uint32_t frame);

/* Bevelled panel. `bevel` is the border thickness in pixels (0..3). */
void pu20DrawPanel(DMAChain *chain, Pu20Rect r, Pu20PanelStyle style, int bevel);

/* Panel with a vertical gradient between two arbitrary colours. */
void pu20DrawGouraudPanel(DMAChain *chain, Pu20Rect r, Pu20Rgb top, Pu20Rgb bottom,
                          int bevel);

/* Transport / function button with optional icon and label. */
void pu20DrawButton(DMAChain *chain, Pu20Rect r, Pu20Icon icon, const char *label,
                    const TextureInfo *font, bool selected, bool pressed,
                    uint32_t frame);

/* Top title bar (0,0,320,18) with optional right-aligned clock. */
void pu20DrawHeader(DMAChain *chain, const TextureInfo *font, const char *title,
                    const char *clockText, uint32_t frame);

/* Bottom help bar (0,226,320,14). */
void pu20DrawFooter(DMAChain *chain, const TextureInfo *font, const char *text);

/* Pulsing gold selection bar used by lists and the track grid. */
void pu20DrawSelectionBar(DMAChain *chain, Pu20Rect r, uint32_t frame);

/* Small animated chevron at (x,y) pointing right. */
void pu20DrawCursor(DMAChain *chain, int x, int y, uint32_t frame);

/* Icon helpers. Tinted icons multiply the atlas by `tint`. */
void pu20DrawIcon(DMAChain *chain, Pu20Icon icon, int x, int y);
void pu20DrawIconTinted(DMAChain *chain, Pu20Icon icon, int x, int y, Pu20Rgb tint);

/* Spinning disc with rim shading. `angle` is in degrees (0..359). */
void pu20DrawDisc(DMAChain *chain, int cx, int cy, int radius, int angle,
                  bool spinning);

/* Single track cell (29x13) for the 6x10 track grid. */
void pu20DrawTrackCell(DMAChain *chain, const TextureInfo *font, int x, int y,
                       int track, bool dataTrack, bool selected, bool playing,
                       uint32_t frame);

/* Two-channel VU meter (16 segments per channel). Levels are 0..15. */
void pu20DrawVuMeter(DMAChain *chain, Pu20Rect r, int levelL, int levelR);

/* Text with an arbitrary colour (temporarily overrides g_cfg.text). */
void pu20DrawText(DMAChain *chain, const TextureInfo *font, int x, int y,
                  const char *str, int maxX, Pu20Rgb color);


/* --- PU-20 v2 shell (real BIOS layout, 640x480 -> 320x240) ------------- */

/* Uploads the large font and the procedural main/player backgrounds. */
void pu20SkinUploadShellAssets(void);

/* Backgrounds: radial rings (main menu) and rings + spiral (CD player). */
void pu20DrawMainBackground(DMAChain *chain, uint32_t frame);
void pu20DrawVortexBackground(DMAChain *chain, uint32_t frame);

/* Procedural PU-20 radial background (any resolution). */
void pu20DrawShellBackground(DMAChain *chain, int w, int h, uint32_t frame);

/* Panel background only (gradient + 1px border), for custom 640x480 titles. */
void pu20DrawTitlePanelRect(DMAChain *chain, int x, int y, int w, int h);

/* Blue title panel ("CD PLAYER", "MAIN MENU", screen names). */
void pu20DrawTitlePanel(DMAChain *chain, const TextureInfo *font,
                        const char *title);

/* Large 12x18 clean-room font (A-Z, 0-9, accented Latin-1). */
void pu20DrawLargeText(DMAChain *chain, int x, int y, const char *text,
                       int maxX);
int  pu20MeasureLargeText(const char *text);

/* Same font, colour-modulated (for outlines and highlighted labels). */
void pu20DrawLargeTextColor(DMAChain *chain, int x, int y, const char *text,
                            int maxX, Pu20Rgb color);


/* RC5 text with a 1px dark shadow (Sony-like bold labels). */
void pu20DrawTextShadow(DMAChain *chain, const TextureInfo *font,
                        int x, int y, const char *str, int maxX,
                        Pu20Rgb color);

/* Clean-room Phong sphere CLUT indices uploaded to VRAM (640, 496..503). */
typedef enum {
    PU20_CLUT_INDIGO = 0,
    PU20_CLUT_GREEN,
    PU20_CLUT_RED,
    PU20_CLUT_GOLD,
    PU20_CLUT_YELLOW,
    PU20_CLUT_PURPLE,
    PU20_CLUT_CYAN,
    PU20_CLUT_ORANGE,
    PU20_CLUT_COUNT
} Pu20SphereClut;

/* 3D Phong sphere/ellipsoid sprite (1 textured quad, hardware-scaled). */
void pu20DrawSphereQuad(DMAChain *chain, int x0, int y0, int x1, int y1,
                        int clutIdx);
void pu20DrawSphereClut(DMAChain *chain, int cx, int cy, int r, int clutIdx);

/* 3D sphere/ball with smooth Phong shading. */
void pu20DrawBall(DMAChain *chain, int cx, int cy, int r, Pu20Rgb base);
void pu20DrawFlatDisc(DMAChain *chain, int cx, int cy, int r, Pu20Rgb c);
void pu20DrawBallValue(DMAChain *chain, int cx, int cy, int r, int value,
                       int digits, Pu20Rgb base);

/* Generated BIOS-style font (0 = small 11px, 1 = medium 17px). */
int  pu20FontWidth(int size, const char *text);
void pu20DrawFont(DMAChain *chain, int size, int x, int y, const char *text,
                  Pu20Rgb color);
void pu20DrawFontClipped(DMAChain *chain, int size, int x, int y, int maxX,
                         const char *text, Pu20Rgb color);
void pu20DrawFontClean(DMAChain *chain, int size, int x, int y, const char *text,
                       Pu20Rgb color);
void pu20DrawFontClippedClean(DMAChain *chain, int size, int x, int y, int maxX,
                              const char *text, Pu20Rgb color);

/* Clean-room PU-20 shell elements (spheres, icons, splat, cursor, title). */
void pu20DrawTransportIcon(DMAChain *chain, int icon, int cx, int cy);
void pu20DrawTransportIconScaled(DMAChain *chain, int icon, int cx, int cy,
                                 bool large);
void pu20DrawShellSprite(DMAChain *chain, int id, int x, int y);
void pu20DrawShellBall(DMAChain *chain, int which, int cx, int cy);
void pu20DrawShellTrackBall(DMAChain *chain, int which, int cx, int cy);
void pu20DrawShellSplat(DMAChain *chain, int cx, int cy);
void pu20DrawShellCursor(DMAChain *chain, int x, int y);
void pu20DrawShellTitle(DMAChain *chain, int x, int y);

/* Circular transport button with a white glyph (0 play, 1 pause, 2 stop,
 * 3 ff, 4 rew, 5 prev, 6 next). */
void pu20DrawTransport(DMAChain *chain, int cx, int cy, int r, Pu20Rgb base,
                       int icon);

/* Rectangular CD-player menu button and the light blue CONTINUE panel. */
void pu20DrawMenuButton(DMAChain *chain, const TextureInfo *font,
                        int x, int y, int w, int h, const char *label,
                        bool selected);
void pu20DrawGlossPanel(DMAChain *chain, int x, int y, int w, int h,
                        bool selected);
void pu20DrawShellButton(DMAChain *chain, int x, int y, int w, int h,
                         bool focused);

/* Rainbow paint splat used behind menu entries and EXIT. */
void pu20DrawSplat(DMAChain *chain, int cx, int cy, int w, int h,
                   uint32_t seed);

/* Pink 3D selection arrow. */
void pu20DrawArrow(DMAChain *chain, int x, int y, uint32_t frame);

/* Large background sphere (main menu), optionally with a soft shadow. */
void pu20DrawSphere(DMAChain *chain, int cx, int cy, int r, Pu20Rgb base,
                    bool shadow);

/* Tiny 3x5 text for MIN/SEC and similar labels. */
void pu20DrawTinyText(DMAChain *chain, int x, int y, const char *text);


/* Low-level Gouraud helpers for the SoundScope and custom screens. */
void pu20DrawGradientTriangle(DMAChain *chain, int x0, int y0, Pu20Rgb c0,
                              int x1, int y1, Pu20Rgb c1,
                              int x2, int y2, Pu20Rgb c2, bool blend);
void pu20DrawGradientQuad(DMAChain *chain, int x0, int y0, int x1, int y1,
                          Pu20Rgb tl, Pu20Rgb tr, Pu20Rgb bl, Pu20Rgb br,
                          bool blend);
void pu20DrawGradientLine(DMAChain *chain, int x0, int y0, Pu20Rgb c0,
                          int x1, int y1, Pu20Rgb c1, bool blend);
void pu20SetBlendMode(int mode);
void pu20DrawSemiRect(DMAChain *chain, int x, int y, int w, int h,
                      Pu20Rgb color);
void pu20DrawSubtractRect(DMAChain *chain, int x, int y, int w, int h,
                          Pu20Rgb sub);
void pu20SetChainBlend(DMAChain *chain, int mode);
void pu20DrawRing(DMAChain *chain, int cx, int cy, int rIn, int rOut,
                  Pu20Rgb color, int segments);
void pu20DrawEllipseRing(DMAChain *chain, int cx, int cy,
                         int rxIn, int rxOut, int ryIn, int ryOut,
                         Pu20Rgb color, int segments);
void pu20DrawShadedDisc(DMAChain *chain, int cx, int cy, int r, Pu20Rgb base,
                        int lightAngle, int segments);
void pu20DrawShadedEllipse(DMAChain *chain, int cx, int cy, int rx, int ry,
                           Pu20Rgb base, int lightAngle, int segments);
void pu20DrawFlatEllipse(DMAChain *chain, int cx, int cy, int rx, int ry,
                         Pu20Rgb c);

/* Utilities ------------------------------------------------------------- */

void pu20Blend(const Pu20Rgb *a, const Pu20Rgb *b, int t, Pu20Rgb *out);
void pu20FormatTime(char *buffer, size_t size, uint32_t frames);
int  pu20SinQ12(int degrees);
int  pu20CosQ12(int degrees);

#ifdef __cplusplus
}
#endif
