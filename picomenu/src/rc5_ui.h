#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "gpu.h"
#include "rc5_config.h"

void rc5DrawBox(DMAChain *chain, int x, int y, int w, int h, bool bordered,
                RgbColor fill, bool semiTrans);
void rc5DrawSolidRect(DMAChain *chain, int x, int y, int w, int h, RgbColor color);
void rc5DrawTexture(DMAChain *chain, const TextureInfo *tex, int x, int y);
int  rc5MeasureString(const char *str);
void rc5PrintString(DMAChain *chain, const TextureInfo *font, int x, int y,
                    const char *str, int maxX);
