#include "rc5_ui.h"
#include "rc5_font_sprites.h"
#include "ps1/gpucmd.h"

#define FONT_FIRST_CHAR  0x21
#define FONT_SPACE_WIDTH 4
#define FONT_TAB_WIDTH   32
#define FONT_LINE_HEIGHT 13

void rc5DrawSolidRect(DMAChain *chain, int x, int y, int w, int h, RgbColor color) {
    uint32_t *ptr = allocatePacket(chain, 3);
    ptr[0] = gp0_rgb(color.r, color.g, color.b) | gp0_rectangle(false, false, false);
    ptr[1] = gp0_xy(x, y);
    ptr[2] = gp0_xy(w, h);
}

void rc5DrawBox(DMAChain *chain, int x, int y, int w, int h, bool bordered,
                RgbColor fill, bool semiTrans) {
    uint32_t *ptr = allocatePacket(chain, 3);
    ptr[0] = gp0_rgb(fill.r, fill.g, fill.b) | gp0_rectangle(false, semiTrans, false);
    ptr[1] = gp0_xy(x, y);
    ptr[2] = gp0_xy(w, h);

    if (bordered) {
        ptr = allocatePacket(chain, 7);
        ptr[0] = gp0_rgb(g_cfg.frame.r, g_cfg.frame.g, g_cfg.frame.b) | 0x48000000u; // Poly-line
        ptr[1] = gp0_xy(x - 1, y - 1);
        ptr[2] = gp0_xy(x + w, y - 1);
        ptr[3] = gp0_xy(x + w, y + h);
        ptr[4] = gp0_xy(x - 1, y + h);
        ptr[5] = gp0_xy(x - 1, y - 1);
        ptr[6] = 0x50005000u; // Poly-line terminator
    }
}

void rc5DrawTexture(DMAChain *chain, const TextureInfo *tex, int x, int y) {
    uint32_t *ptr = allocatePacket(chain, 5);
    ptr[0] = gp0_texpage(tex->page, false, false);
    ptr[1] = gp0_rectangle(true, true, true);
    ptr[2] = gp0_xy(x, y);
    ptr[3] = gp0_uv(tex->u, tex->v, tex->clut);
    ptr[4] = gp0_xy(tex->width, tex->height);
}

static const Rc5SpriteInfo *lookupGlyph(uint8_t ch) {
    if (ch < FONT_FIRST_CHAR || (ch > 0x98 && ch < 0xA0)) {
        ch = 0x7F;
    }
    // Exact RC5 sprite table layout (FUN_80011e18):
    //   0x21..0x98 -> indices   0..119 (120 entries, no gap slots for 0x99..0x9F)
    //   0xA0..0xFF -> indices 120..215 (96 Latin-1 entries)
    if (ch < 0xA0) {
        return &rc5FontSprites[ch - FONT_FIRST_CHAR];
    }
    return &rc5FontSprites[120 + (ch - 0xA0)];
}

int rc5MeasureString(const char *str) {
    int width = 0;
    for (; *str; str++) {
        uint8_t ch = (uint8_t)*str;
        if (ch == ' ') {
            width += FONT_SPACE_WIDTH;
        } else if (ch == '\t') {
            width = (width + FONT_TAB_WIDTH) & ~(FONT_TAB_WIDTH - 1);
        } else if (ch == '\n' || ch == '\r') {
            break;
        } else {
            width += lookupGlyph(ch)->width;
        }
    }
    return width;
}

void rc5PrintString(DMAChain *chain, const TextureInfo *font, int x, int y,
                    const char *str, int maxX) {
    int curX = x;
    int curY = y;

    uint32_t *ptr = allocatePacket(chain, 1);
    ptr[0] = gp0_texpage(font->page, false, false);

    for (; *str; str++) {
        uint8_t ch = (uint8_t)*str;
        if (ch == ' ') {
            curX += FONT_SPACE_WIDTH;
            continue;
        }
        if (ch == '\t') {
            curX = (curX + FONT_TAB_WIDTH) & ~(FONT_TAB_WIDTH - 1);
            continue;
        }
        if (ch == '\n') {
            curX = x;
            curY += FONT_LINE_HEIGHT;
            continue;
        }
        if (ch == '\r') {
            continue;
        }

        const Rc5SpriteInfo *sp = lookupGlyph(ch);
        // PlayStation icon glyphs (0x80..0x98) use untinted texture (raw color); text uses g_cfg.text
        bool rawIcon = (ch >= 0x80 && ch <= 0x98);

        if (curX + sp->width <= maxX) {
            ptr = allocatePacket(chain, 4);
            if (rawIcon) {
                ptr[0] = gp0_rectangle(true, true, true);
            } else {
                ptr[0] = gp0_rgb(g_cfg.text.r, g_cfg.text.g, g_cfg.text.b) |
                         gp0_rectangle(true, true, false);
            }
            ptr[1] = gp0_xy(curX, curY);
            ptr[2] = gp0_uv(font->u + sp->x, font->v + sp->y, font->clut);
            ptr[3] = gp0_xy(sp->width, sp->height);
        }
        curX += sp->width;
    }
}
