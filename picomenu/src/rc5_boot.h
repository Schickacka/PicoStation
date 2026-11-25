#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "gpu.h"

typedef enum {
    BOOT_MODE_FAST    = 4, // Fast Boot (BIOS LoadExec / softFastReboot)
    BOOT_MODE_REGULAR = 5, // Regular Boot (Full BIOS reboot)
    BOOT_MODE_CHEATS  = 8  // Fast Boot + install GameShark cheats at 0x8000C000
} Rc5BootMode;

void rc5BootStart(Rc5BootMode mode, uint16_t selectedVisualIdx, uint8_t mcpPresent);
void rc5BootStepAndDraw(DMAChain *chain, const TextureInfo *font);
