#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "gpu.h"
#include "psxproject/spu.h"

void rc5CheatsReset(void);
bool rc5CheatsHasEnabled(void);
void rc5CheatsInstallToRam(void);

// Returns: 0 = stay in cheats modal, 1 = close back to options, 2 = launch game with cheats
int rc5CheatsDrawAndUpdate(DMAChain *chain, const TextureInfo *font,
                           uint16_t buttons, uint16_t prevButtons,
                           uint16_t fileIndex, Sound *sfxClick, Sound *sfxSlide);
