#include "modchip.h"

#include <stdio.h>

#include "cmd.h"
#include "disc_image.h"
#include "drive_mechanics.h"
#include "logging.h"
#include "pico/stdlib.h"
#include "picostation.h"
#include "values.h"

#if DEBUG_MODCHIP
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

namespace
{
// SCEX is nominally a 250 bps signal (4000 us/bit). Under Mechacon overclock (_OC)
// profiles, SCEX scales proportionally to match the overclocked HC05 clock.
constexpr uint32_t c_scexDefaultBitTimeUs =
#if defined(PICOSTATION_OVERCLOCK) && defined(PICOSTATION_MAINBOARD_PU18)
    2252;
#elif defined(PICOSTATION_OVERCLOCK) && defined(PICOSTATION_MAINBOARD_PU8)
    2475;
#else
    4000;
#endif
// Silence inserted between two SCEX strings (PSNee uses 80..100 ms).
constexpr uint32_t c_scexInterStringDelayUs = 90000;
// Number of consecutive "wobble groove" samples required before injecting.
// PSNee's sweet spot is 11..19; 17 is ~227 ms at 75 subcode frames/s.
constexpr int c_modchipHysteresis = 17;
// The SCEX region string is 4 characters, 11 bits each (1 start + 8 data + 2 stop).
constexpr int c_scexCharacters = 4;
constexpr int c_scexBitsPerChar = 11;
// Region selection follows PSNee: 0 = SCEA, 1 = SCEE, 2 = SCEI, 3 = universal
// (send all three once). A region-specific build sends only that string per
// burst; the hysteresis triggers another burst if the console needs it.
#ifndef PICOSTATION_SCEX_REGION
#define PICOSTATION_SCEX_REGION 3
#endif
constexpr int c_scexRegion = PICOSTATION_SCEX_REGION;
constexpr int c_scexRepetitions = (c_scexRegion == 3) ? 3 : 1;

static_assert(c_scexRegion >= 0 && c_scexRegion <= 3,
              "PICOSTATION_SCEX_REGION must be 0 (SCEA), 1 (SCEE), 2 (SCEI) or 3 (universal)");

// Compute the SCEX wire level for a bit within an 8N2 character frame using
// inverted polarity: idle low, start bit high, data bits LSB-first and inverted,
// then two stop bits low.
inline bool __time_critical_func(getScexBitLevel)(const char value, const int bitInChar)
{
    if (bitInChar == 0)
    {
        return true;  // Start bit
    }
    if (bitInChar <= 8)
    {
        return ((value >> (bitInChar - 1)) & 1) == 0;  // Inverted data bit
    }
    return false;  // First and second stop bits
}

inline void __time_critical_func(setScexPinLevel)(const bool level)
{
#if defined(PICOSTATION_SCEX_OPEN_DRAIN)
    if (level)
    {
        gpio_set_dir(Pin::SCEX_DATA, GPIO_IN);
    }
    else
    {
        gpio_put(Pin::SCEX_DATA, 0);
        gpio_set_dir(Pin::SCEX_DATA, GPIO_OUT);
    }
#else
    gpio_put(Pin::SCEX_DATA, level);
#endif
}
}  // namespace

volatile uint32_t picostation::ModChip::s_bitTimeUs = c_scexDefaultBitTimeUs;

void __time_critical_func(picostation::ModChip::endLicenseSequence)()
{
    setScexPinLevel(false);
    m_modchipTimer = time_us_64();
    DEBUG_PRINT("-SCEX\n");
}

void __time_critical_func(picostation::ModChip::init)()
{
    gpio_init(Pin::SCEX_DATA);
    gpio_set_outover(Pin::SCEX_DATA, GPIO_OVERRIDE_NORMAL);
#if defined(PICOSTATION_SCEX_OPEN_DRAIN)
    gpio_pull_up(Pin::SCEX_DATA);
#endif
    gpio_put(Pin::SCEX_DATA, 0);
    gpio_set_dir(Pin::SCEX_DATA, GPIO_OUT);
    reset();
}

void __time_critical_func(picostation::ModChip::reset)()
{
    setScexPinLevel(false);
    m_state = State::IDLE;
    m_hysteresis = 0;
    m_stringIndex = 0;
    m_bitIndex = 0;
    m_modchipTimer = time_us_64();
    m_nextDeadlineUs = 0;
}

void __time_critical_func(picostation::ModChip::sendLicenseString)(const int sector, MechCommand &mechCommand)
{
    static constexpr char s_licenseData[3][5] = {"SCEA", "SCEE", "SCEI"};

    const bool inWobbleGroove = (sector > 0) && (sector < c_leadIn);
    const bool isDataDisc = g_discImage.hasData();
    const bool soctDisabled = !mechCommand.getSoct();
    const bool gfsSet = mechCommand.getSens(SENS::GFS);
    const bool sledStopped = g_driveMechanics.isSledStopped();
    const bool shouldActivateModchip = inWobbleGroove && sledStopped && gfsSet && soctDisabled && isDataDisc;
    const uint64_t now = time_us_64();
    const uint32_t bitTimeUs = s_bitTimeUs;

    // Non-blocking phase-locked state machine: advancing m_nextDeadlineUs by
    // exact bitTimeUs ticks preserves 8N2 timing while letting Core 1 keep
    // m_sectorSending updated so Core 0 never freezes SCOR/SubQ during Lead-In.
    switch (m_state)
    {
        case State::IDLE:
        {
            if (shouldActivateModchip)
            {
                if ((now - m_modchipTimer) > 13333)
                {
                    m_modchipTimer = now;

                    if (++m_hysteresis >= c_modchipHysteresis)
                    {
                        m_hysteresis = 0;
                        m_stringIndex = 0;
                        m_bitIndex = 0;
                        DEBUG_PRINT("+SCEX\n");

                        const int index = (c_scexRegion == 3) ? 0 : c_scexRegion;
                        DEBUG_PRINT("%s\n", s_licenseData[index]);
                        setScexPinLevel(getScexBitLevel(s_licenseData[index][0], 0));
                        m_nextDeadlineUs = now + bitTimeUs;
                        m_state = State::SENDING_BITS;
                    }
                }
            }
            else
            {
                m_hysteresis = 0;
                m_modchipTimer = now;
            }
            break;
        }

        case State::SENDING_BITS:
        {
            if (now < m_nextDeadlineUs)
            {
                return;
            }

            m_bitIndex++;
            if (m_bitIndex < (c_scexCharacters * c_scexBitsPerChar))
            {
                const int index = (c_scexRegion == 3) ? (m_stringIndex % 3) : c_scexRegion;
                const char ch = s_licenseData[index][m_bitIndex / c_scexBitsPerChar];
                setScexPinLevel(getScexBitLevel(ch, m_bitIndex % c_scexBitsPerChar));
                m_nextDeadlineUs += bitTimeUs;
                if (now >= m_nextDeadlineUs)
                {
                    m_nextDeadlineUs = now + bitTimeUs;
                }
            }
            else
            {
                setScexPinLevel(false);
                mechCommand.setBootSectorPattern(255);
                m_stringIndex++;

                if (m_stringIndex >= c_scexRepetitions || !shouldActivateModchip)
                {
                    endLicenseSequence();
                    m_state = State::IDLE;
                }
                else
                {
                    m_nextDeadlineUs += c_scexInterStringDelayUs;
                    if (now >= m_nextDeadlineUs)
                    {
                        m_nextDeadlineUs = now + c_scexInterStringDelayUs;
                    }
                    m_state = State::INTER_STRING_GAP;
                }
            }
            break;
        }

        case State::INTER_STRING_GAP:
        {
            if (!shouldActivateModchip)
            {
                endLicenseSequence();
                m_state = State::IDLE;
                break;
            }

            if (now >= m_nextDeadlineUs)
            {
                m_bitIndex = 0;
                const int index = (c_scexRegion == 3) ? (m_stringIndex % 3) : c_scexRegion;
                DEBUG_PRINT("%s\n", s_licenseData[index]);
                setScexPinLevel(getScexBitLevel(s_licenseData[index][0], 0));
                m_nextDeadlineUs += bitTimeUs;
                if (now >= m_nextDeadlineUs)
                {
                    m_nextDeadlineUs = now + bitTimeUs;
                }
                m_state = State::SENDING_BITS;
            }
            break;
        }
    }
}
