#include "modchip.h"

#include <stdio.h>

#include "cmd.h"
#include "disc_image.h"
#include "pico_audio.h"
#include "drive_mechanics.h"
#include "hardware/pio.h"
#include "logging.h"
#include "main.pio.h"
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
// Silence inserted between two SCEX strings (90 ms, matching official RC5
// PU8_OC/PU18_OC 0x15f8f = 89999 us and PSNee's 80..100 ms window).
constexpr uint32_t c_scexInterStringDelayUs = 90000;
// Number of consecutive "wobble groove" samples required before injecting.
// PSNee's sweet spot is 11..19; 17 is ~227 ms at 75 subcode frames/s.
constexpr int c_modchipHysteresis = 17;
// The SCEX region string is 4 characters, 11 bits each (1 start + 8 data + 2 stop).
constexpr int c_scexCharacters = 4;
// PIO1 runs 20 cycles per bit at sys_clk = 271.2 MHz -> clkdiv = bitTimeUs * 13.56f.
constexpr float c_scexPioClkdivPerUs = 271.2f / 20.0f;
// Region selection follows PSNee: 0 = SCEA, 1 = SCEE, 2 = SCEI, 3 = universal
// (send all three once). Even on a regional build, send 3 consecutive strings
// per burst (aborted immediately once the console leaves lead-in) so the HC05
// always captures 2 full consecutive strings without waiting for a second
// hysteresis cycle (especially on Menu -> Full Boot without hardware RESET).
#ifndef PICOSTATION_SCEX_REGION
#define PICOSTATION_SCEX_REGION 3
#endif
constexpr int c_scexRegion = PICOSTATION_SCEX_REGION;
// Match official RC5 (6 strings for Universal = SCEA/SCEE/SCEI x2 so HC05 always
// captures 2 consecutive strings of its region; 3 strings for regional builds).
constexpr int c_scexRepetitions = (c_scexRegion == 3) ? 6 : 3;

static_assert(c_scexRegion >= 0 && c_scexRegion <= 3,
              "PICOSTATION_SCEX_REGION must be 0 (SCEA), 1 (SCEE), 2 (SCEI) or 3 (universal)");

static constexpr char s_licenseData[3][5] = {"SCEA", "SCEE", "SCEI"};

// Encode one 8N2 inverted SCEX character into bits [10:0] (LSB-first):
//   bit 0     = 1 (start bit, HIGH)
//   bits 1..8 = inverted data bits (~value & 0xFF)
//   bits 9..10 = 0, 0 (two stop bits, LOW)
// For open-drain PIO (out pindirs, 1 with pin latch = 0), invert all 11 bits
// so pindir=0 releases Hi-Z (HIGH via pull-up) and pindir=1 drives LOW.
inline uint32_t __time_critical_func(encodeScexChar)(const char value)
{
    const uint32_t pushPullWord = 1u | ((static_cast<uint32_t>(static_cast<uint8_t>(~value))) << 1);
#if defined(PICOSTATION_SCEX_OPEN_DRAIN)
    return pushPullWord ^ 0x7FFu;
#else
    return pushPullWord;
#endif
}

inline float __time_critical_func(scexClkdivForBitTime)(const uint32_t bitTimeUs)
{
    return static_cast<float>(bitTimeUs) * c_scexPioClkdivPerUs;
}
}  // namespace

volatile uint32_t picostation::ModChip::s_bitTimeUs = c_scexDefaultBitTimeUs;
volatile bool picostation::ModChip::s_resetRequested = false;

void __time_critical_func(picostation::ModChip::forceIdleLow)()
{
    if (!m_pioReady)
    {
        return;
    }
    pio_sm_set_enabled(PIOInstance::SCEX, SM::SCEX, false);
    pio_sm_clear_fifos(PIOInstance::SCEX, SM::SCEX);
    pio_sm_restart(PIOInstance::SCEX, SM::SCEX);
    pio_sm_set_pins_with_mask(PIOInstance::SCEX, SM::SCEX, 0u, 1u << Pin::SCEX_DATA);
    pio_sm_set_pindirs_with_mask(PIOInstance::SCEX, SM::SCEX, 1u << Pin::SCEX_DATA, 1u << Pin::SCEX_DATA);
}

void __time_critical_func(picostation::ModChip::startString)(const int index, const uint32_t bitTimeUs)
{
    if (!m_pioReady)
    {
        return;
    }

    DEBUG_PRINT("%s\n", s_licenseData[index]);

    forceIdleLow();
    pio_sm_set_clkdiv(PIOInstance::SCEX, SM::SCEX, scexClkdivForBitTime(bitTimeUs));
    pio_sm_clkdiv_restart(PIOInstance::SCEX, SM::SCEX);
    pio_sm_exec(PIOInstance::SCEX, SM::SCEX, pio_encode_jmp(m_pioOffset));

    // Clear TXSTALL before pushing the 4 encoded 11-bit words (44 bits total).
    // Hardware sets TXSTALL at the exact cycle the final stop bit completes.
    PIOInstance::SCEX->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + SM::SCEX);
    for (int i = 0; i < c_scexCharacters; i++)
    {
        pio_sm_put(PIOInstance::SCEX, SM::SCEX, encodeScexChar(s_licenseData[index][i]));
    }
    pio_sm_set_enabled(PIOInstance::SCEX, SM::SCEX, true);
}

void __time_critical_func(picostation::ModChip::endLicenseSequence)()
{
    forceIdleLow();
    m_modchipTimer = time_us_64();
    DEBUG_PRINT("-SCEX\n");
}

void __time_critical_func(picostation::ModChip::init)()
{
    const float clkdiv = scexClkdivForBitTime(s_bitTimeUs);
#if defined(PICOSTATION_SCEX_OPEN_DRAIN)
    m_pioOffset = pio_add_program(PIOInstance::SCEX, &scex_tx_od_program);
    scex_tx_od_program_init(PIOInstance::SCEX, SM::SCEX, m_pioOffset, Pin::SCEX_DATA, clkdiv);
#else
    m_pioOffset = pio_add_program(PIOInstance::SCEX, &scex_tx_program);
    scex_tx_program_init(PIOInstance::SCEX, SM::SCEX, m_pioOffset, Pin::SCEX_DATA, clkdiv);
#endif
    m_pioReady = true;
    reset();
}

void __time_critical_func(picostation::ModChip::reset)()
{
    forceIdleLow();
    m_state = State::IDLE;
    m_hysteresis = 0;
    m_stringIndex = 0;
    m_modchipTimer = time_us_64();
    m_nextDeadlineUs = 0;
}

void __time_critical_func(picostation::ModChip::sendLicenseString)(const int sector, MechCommand &mechCommand)
{
    if (s_resetRequested)
    {
        s_resetRequested = false;
        reset();
    }

    // Match official RC5 PU8_OC/PU18_OC gating: do NOT gate on sledStopped
    // (brief HC05 wobble micro-seeks during Lead-In otherwise abort SCEX or
    // reset hysteresis). Never abort an active 44-bit SCEX string mid-byte;
    // only check gating in IDLE and during the 90 ms INTER_STRING_GAP.
    const bool inWobbleGroove = (sector > 0) && (sector < c_leadIn);
    const bool isDataDisc = !g_picoAudio.isActive() && g_discImage.hasData();
    const bool soctDisabled = !mechCommand.getSoct();
    const bool gfsSet = mechCommand.getSens(SENS::GFS);
    const bool shouldActivateModchip = inWobbleGroove && gfsSet && soctDisabled && isDataDisc;
    const uint64_t now = time_us_64();
    const uint32_t bitTimeUs = s_bitTimeUs;

    // Hardware-timed PIO1 state machine: all 44 bits of each SCEX string are
    // shifted out by PIO1 with 0% CPU jitter even if Core 1 is busy with SD/DMA.
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
                        DEBUG_PRINT("+SCEX\n");

                        const int index = (c_scexRegion == 3) ? 0 : c_scexRegion;
                        startString(index, bitTimeUs);
                        m_state = State::SENDING_STRING;
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

        case State::SENDING_STRING:
        {
            // Wait for the active 44-bit string to finish shifting out cleanly
            // (matching official RC5 UART1 behavior — never cut a string mid-byte).
            const uint32_t txStallMask = 1u << (PIO_FDEBUG_TXSTALL_LSB + SM::SCEX);
            if ((PIOInstance::SCEX->fdebug & txStallMask) == 0)
            {
                return;
            }

            forceIdleLow();
            m_stringIndex++;

            if (m_stringIndex >= c_scexRepetitions)
            {
                endLicenseSequence();
                m_state = State::IDLE;
            }
            else
            {
                m_nextDeadlineUs = now + c_scexInterStringDelayUs;
                m_state = State::INTER_STRING_GAP;
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
                const int index = (c_scexRegion == 3) ? (m_stringIndex % 3) : c_scexRegion;
                startString(index, bitTimeUs);
                m_state = State::SENDING_STRING;
            }
            break;
        }
    }
}
