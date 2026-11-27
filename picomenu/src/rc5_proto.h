#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "ps1/cdrom.h"
#include "psxproject/cdrom.h"
#include "psxproject/delay.h"

// Exact RC5 PicoStation custom Mechacon command IDs (sent via CDROM_CMD_TEST 0x19, subcmd 0x50)
typedef enum {
    PICO_CMD_NONE              = 0x0,
    PICO_CMD_GOTO_ROOT         = 0x1,
    PICO_CMD_GOTO_PARENT       = 0x2,
    PICO_CMD_GOTO_DIRECTORY    = 0x3,
    PICO_CMD_GET_NEXT_CONTENTS = 0x4,
    PICO_CMD_MOUNT_FILE        = 0x5,
    PICO_CMD_IO_COMMAND        = 0x6,
    PICO_CMD_IO_DATA           = 0x7,
    PICO_CMD_EXTENDED          = 0x8,
    PICO_CMD_GET_COVER         = 0x9,
    PICO_CMD_BOOTLOADER        = 0xA,
    PICO_CMD_GET_CHEAT         = 0xB,
    PICO_CMD_PICOPLAYER        = 0xC
} PicoCommand;

typedef enum {
    PICO_PP_SCAN_SD      = 0,
    PICO_PP_MOUNT_SD     = 1,
    PICO_PP_EXIT         = 2,
    PICO_PP_GET_ART_MASK = 0x0100
} PicoPlayerSubCmd;

// Extended subcommand arguments: arg = (ext << 1) | value
typedef enum {
    PICO_EXT_SKIP_BOOTSECTOR = 1, // arg 2 (off) / 3 (on -> skip bootsector)
    PICO_EXT_SKIP_EDC        = 2, // arg 4 (off) / 5 (on -> skip EDC)
    PICO_EXT_GET_CFG         = 3, // arg 6
    PICO_EXT_GET_BG          = 4, // arg 8
    PICO_EXT_OVERCLOCK       = 5  // arg 10 (off) / 11 (on)
} PicoExtendedCmd;

// Exact RC5 virtual LBAs (drive sector = 4650 + LBA)
#define PICO_LBA_LISTING      100 // sector 4750 ("LST1")
#define PICO_LBA_CFG          150 // sector 4800 ("CFG1" / "DEAD")
#define PICO_LBA_COVER_BASE   200 // sectors 4850..4857 (8 x 2114 B, "PART" / "DEAD")
#define PICO_LBA_CHEAT_BASE   250 // sectors 4900..4902 (3 x 2048 B, "CHT1" / "CHT0")
#define PICO_LBA_BG_BASE      300 // sectors 4950..4983 (34 x 2316 B, "PART" / "DEAD")
#define PICO_LBA_PP_META_BASE 350 // sectors 5000..5009 ("PPA1" SD:/PicoPlayer/ metadata)
#define PICO_LBA_PP_ART_BASE  360 // sectors 5010..5017 ("PART"/"DEAD" 128x128 8bpp .cov)

static inline void picoSendCommand(uint8_t command, uint16_t argument) {
    while (cdromBusy) {
        delayMicroseconds(100);
    }
    uint8_t test[4] = {
        0x50, // CDROM_TEST_DSP_CMD (0x50 = pass 3 bytes directly to CXD/PicoStation DSP)
        (uint8_t)(0xF0u | (command & 0x0Fu)),
        (uint8_t)((argument >> 8) & 0xFFu),
        (uint8_t)(argument & 0xFFu)
    };
    issueCDROMCommand(CDROM_CMD_TEST, test, sizeof(test));
    waitForINT3();
}
