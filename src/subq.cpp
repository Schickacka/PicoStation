#include "subq.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "drive_mechanics.h"
#include "disc_image.h"
#include "hardware/pio.h"
#include "logging.h"
#include "main.pio.h"
#include "pico/time.h"
#include "picostation.h"
#include "values.h"

#if DEBUG_SUBQ
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...) while (0)
#endif

void __time_critical_func(picostation::SubQ::start_subq)(const int sector)
{
    if (!g_driveMechanics.isSledStopped() || g_driveMechanics.req_skip_subq())
	{
		g_driveMechanics.clear_skip_subq();
		return;
	}

    // LibCrypt (.sbi): emulate Sony CXD2510Q/CXD2545Q SubQ CRC failure (CRCF=0).
    // Drive SQSO LOW before SCOR rises and still pulse SCOR so the MC68HC05
    // keeps sector sync intact, sees CRCF=0, ignores this sector's SubQ, and
    // returns the previous valid sector on GetlocP.
    if (m_discImage->isSbiSector(sector))
    {
        pio_sm_set_enabled(PIOInstance::SUBQ, SM::SUBQ, false);
        pio_sm_clear_fifos(PIOInstance::SUBQ, SM::SUBQ);
        subq_program_init(PIOInstance::SUBQ, SM::SUBQ, g_subqOffset, Pin::SQSO, Pin::SQCK);
        pio_sm_set_pins_with_mask(PIOInstance::SUBQ, SM::SUBQ, 0, 1u << Pin::SQSO);
        busy_wait_us_32(1);

        gpio_put(Pin::SCOR, 1);
        busy_wait_us_32(4);
        gpio_put(Pin::SCOR, 0);
        return;
    }
    
    const SubQ::Data tracksubq = m_discImage->generateSubQ(sector);
    
    gpio_put(Pin::SCOR, 1);
    
    subq_program_init(PIOInstance::SUBQ, SM::SUBQ, g_subqOffset, Pin::SQSO, Pin::SQCK);
    pio_sm_clear_fifos(PIOInstance::SUBQ, SM::SUBQ);
    pio_sm_set_enabled(PIOInstance::SUBQ, SM::SUBQ, true);
    
    const uint sub[3] = {
        (uint)((tracksubq.raw[3] << 24) | (tracksubq.raw[2] << 16) | (tracksubq.raw[1] << 8) | (tracksubq.raw[0])),
        (uint)((tracksubq.raw[7] << 24) | (tracksubq.raw[6] << 16) | (tracksubq.raw[5] << 8) | (tracksubq.raw[4])),
        (uint)((tracksubq.raw[11] << 24) | (tracksubq.raw[10] << 16) | (tracksubq.raw[9] << 8) | (tracksubq.raw[8]))};
    pio_sm_put_blocking(PIOInstance::SUBQ, SM::SUBQ, sub[0]);
    pio_sm_put_blocking(PIOInstance::SUBQ, SM::SUBQ, sub[1]);
    pio_sm_put_blocking(PIOInstance::SUBQ, SM::SUBQ, sub[2]);

    gpio_put(Pin::SCOR, 0);
    
#if DEBUG_SUBQ
	const uint8_t *d = tracksubq.raw;
	DEBUG_PRINT("%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %d\n", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], sector-4500);
#endif
}

