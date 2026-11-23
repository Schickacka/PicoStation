#include "drive_mechanics.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include "hardware/gpio.h"
#include "i2s.h"
#include "cmd.h"
#include "values.h"
#include "logging.h"

#if DEBUG_CMD
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...) while (0)
#endif

#define ZONE_CNT 	16
#define ZONE_MAX 	ZONE_CNT-1

extern picostation::I2S m_i2s;
uint32_t zone[ZONE_CNT] = 			{4500, 7750, 13500, 27000, 45000, 63000, 85500, 103500, 130500, 153000, 175500, 207000, 234000, 265500, 297000, 999999};
uint32_t sect_per_track[ZONE_CNT] = {	8,    9,    10,    11,    12,    13,    14,     15,     16,     17,     18,     19,     20,     21,     22,     23};

static_assert(sizeof(zone) / sizeof(zone[0]) == ZONE_CNT, "zone table size mismatch");
static_assert(sizeof(sect_per_track) / sizeof(sect_per_track[0]) == ZONE_CNT, "sect_per_track table size mismatch");

picostation::DriveMechanics picostation::g_driveMechanics;

// Return the zone that contains the given sector. zone[i] is the last sector
// of zone i, so the containing zone is the first one whose end is at or past
// the requested sector. Keeping this as a single source of truth avoids the
// stale/off-by-one cur_zone values that used to make forward seeks underflow
// and jump straight to the end of the disc.
static inline uint8_t __time_critical_func(zoneForSector)(uint32_t sector)
{
    uint8_t zoneIndex = 0;

    while (zoneIndex < ZONE_MAX && sector > zone[zoneIndex])
    {
        zoneIndex++;
    }

    return zoneIndex;
}

void __time_critical_func(picostation::DriveMechanics::moveToNextSector)()
{
    // Prevent the XLAT GPIO ISR from preempting this read-modify-write and
    // having its seek target overwritten by an old sector + 1.
    const uint32_t irq = save_and_disable_interrupts();
    uint32_t s = m_sector;
    if (s < static_cast<uint32_t>(c_sectorMax))
    {
        s++;
    }

    m_sector = s;
    cur_zone = zoneForSector(s);
    gpio_put(Pin::LMTSW, s > 3000);
    __dmb();
    restore_interrupts(irq);
}

void __time_critical_func(picostation::DriveMechanics::setSector)(uint32_t step, bool rev)
{
    m_i2s.i2s_set_state(0);

    const uint32_t sectorMax = static_cast<uint32_t>(c_sectorMax);
    uint32_t s = m_sector;
    if (s > sectorMax)
    {
        s = sectorMax;
    }

    // Start from a known-good zone instead of whatever the previous seek left
    // behind, and compute on local state so Core 1 never observes intermediate
    // sectors or broken zone invariants.
    uint8_t z = zoneForSector(s);

    while (step != 0)
    {
        if (!rev)
        {
            if (s >= sectorMax)
            {
                s = sectorMax;
                break;
            }

            const uint32_t sectorPerTrack = sect_per_track[z];
            const uint32_t sectorsToZoneEnd = (s < zone[z]) ? (zone[z] - s) : 0;
            const uint32_t fitSteps = sectorsToZoneEnd / sectorPerTrack;

            if (fitSteps == 0)
            {
                // Cross the boundary in one track step rather than advancing
                // cur_zone before m_sector actually crosses into the next zone.
                const uint32_t nextSector = s + sectorPerTrack;
                s = (nextSector > sectorMax) ? sectorMax : nextSector;
                step--;
            }
            else
            {
                const uint32_t do_steps = (step < fitSteps) ? step : fitSteps;
                const uint32_t nextSector = s + (do_steps * sectorPerTrack);
                s = (nextSector > sectorMax) ? sectorMax : nextSector;
                step -= do_steps;
            }

            z = zoneForSector(s);
        }
        else
        {
            if (s == 0)
            {
                z = 0;
                break;
            }

            const uint32_t sectorPerTrack = sect_per_track[z];
            const uint32_t zoneStart = (z > 0) ? zone[z - 1] : 0;
            const uint32_t sectBeforeZoneStart = (s > zoneStart) ? (s - zoneStart) : 0;
            const uint32_t fitSteps = (z > 0) ? (sectBeforeZoneStart / sectorPerTrack) : step;

            if (fitSteps == 0)
            {
                s = (s > sectorPerTrack) ? (s - sectorPerTrack) : 0;
                step--;
            }
            else
            {
                const uint32_t do_steps = (step < fitSteps) ? step : fitSteps;
                const uint64_t dec = static_cast<uint64_t>(do_steps) * sectorPerTrack;
                s = (static_cast<uint64_t>(s) > dec) ? (s - static_cast<uint32_t>(dec)) : 0;
                step -= do_steps;
            }

            z = zoneForSector(s);
        }
    }

    // Publish target sector, zone, LMTSW, and SubQ skip flag atomically wrt IRQs.
    const uint32_t irq = save_and_disable_interrupts();
    m_sector = s;
    cur_zone = z;
    skip_subq = s;
    gpio_put(Pin::LMTSW, s > 3000);
    __dmb();
    restore_interrupts(irq);

#ifdef DEBUG_CMD
    DEBUG_PRINT("set sector %u\n", static_cast<unsigned>(s >= 4500u ? (s - 4500u) : 0u));
#endif
}

bool __time_critical_func(picostation::DriveMechanics::servo_valid)()
{
	return m_sector < static_cast<uint32_t>(c_sectorMax);

}

void __time_critical_func(picostation::DriveMechanics::moveSled)(picostation::MechCommand &mechCommand)
{
    const uint64_t now = time_us_64();
    if ((now - m_sledTimer) > c_MaxTrackMoveTime)
    {
        bool toggleCout = false;
        const uint32_t irq = save_and_disable_interrupts();

        // Re-check under IRQ lock so XLAT stopping/restarting the sled cannot
        // leave a stale ghost track step in flight.
        if (sled_work && (now - m_sledTimer) > c_MaxTrackMoveTime)
        {
            m_sledTimer = now;
            bool atBoundary = false;

            if (!m_sledBrake)
            {
                const uint32_t sectorMax = static_cast<uint32_t>(c_sectorMax);
                uint32_t s = m_sector;
                if (s > sectorMax)
                {
                    s = sectorMax;
                }

                uint8_t z = zoneForSector(s);
                const uint32_t sectorPerTrack = sect_per_track[z];

                if (!m_sledRev)
                {
                    if (s >= sectorMax)
                    {
                        atBoundary = true;
                    }
                    else
                    {
                        const uint32_t nextSector = s + sectorPerTrack;
                        s = (nextSector > sectorMax) ? sectorMax : nextSector;
                        z = zoneForSector(s);
                    }
                }
                else
                {
                    if (s == 0)
                    {
                        atBoundary = true;
                    }
                    else
                    {
                        s = (s > sectorPerTrack) ? (s - sectorPerTrack) : 0;
                        z = zoneForSector(s);
                    }
                }

                if (!atBoundary)
                {
                    m_sector = s;
                    cur_zone = z;
                    // Update LMTSW live so Mechacon Init / ReadTOC homing from
                    // outer tracks (e.g. Capcom 63-65 min) sees LMTSW=0 while
                    // SLED_REVERSE is active instead of deadlocking.
                    gpio_put(Pin::LMTSW, s > 3000);
                    __dmb();
                }
            }

            if (!atBoundary)
            {
                const uint32_t trackCount = cur_track_counter + 1;
                cur_track_counter = trackCount;

                if (!(trackCount & 255))
                {
                    toggleCout = true;
                }
            }
        }

        restore_interrupts(irq);

        if (toggleCout)
        {
            mechCommand.setSens(SENS::COUT, !mechCommand.getSens(SENS::COUT));
        }
    }
}

void __time_critical_func(picostation::DriveMechanics::startSled)(bool rev, bool isBrake)
{
    const uint32_t irq = save_and_disable_interrupts();
    cur_track_counter = 0;
    m_sledTimer = time_us_64();
    m_sledRev = rev;
    m_sledBrake = isBrake;
    sled_work = true;
    __dmb();
    restore_interrupts(irq);
}

void __time_critical_func(picostation::DriveMechanics::stopSled)()
{
    const uint32_t irq = save_and_disable_interrupts();
    const bool wasBrake = m_sledBrake;
    sled_work = false;
    m_sledBrake = false;
    if (!wasBrake)
    {
        skip_subq = m_sector;
    }
    __dmb();
    restore_interrupts(irq);
}

