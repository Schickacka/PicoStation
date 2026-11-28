#include "picostation.h"
#include "pico_audio.h"

#include <stdio.h>
#include <time.h>

#include "cmd.h"
#include "disc_image.h"
#include "directory_listing.h"
#include "drive_mechanics.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include <hardware/i2c.h>
#include "i2s.h"
#include "logging.h"
#include "main.pio.h"
#include "modchip.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pseudo_atomics.h"
#include "subq.h"
#include "values.h"
#include "si5351.h"

#if DEBUG_MAIN
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

// To-do: Implement lid switch behavior
// To-do: Implement a console side menu to select the cue file
// To-do: Implement level meter mode to command $AX - AudioCTRL
// To-do: Fix seeks that go into the lead-in + track 1 pregap areas, possibly sending bad data over I2S

// To-do: Make an ODE class and move these to members
picostation::I2S m_i2s;
picostation::MechCommand m_mechCommand;
extern pseudoatomic<picostation::FileListingStates> needFileCheckAction;
extern pseudoatomic<int> listReadyState;

volatile bool picostation::g_subqDelay = false;  // core0: r/w

static int s_currentPlaybackSpeed = 1;
volatile int picostation::g_targetPlaybackSpeed = 1;  // core0: r/w

mutex_t picostation::g_mechaconMutex;
pseudoatomic<bool> picostation::g_coreReady[2];

volatile unsigned int picostation::g_audioCtrlMode = audioControlModes::NORMAL;
volatile bool picostation::g_audioMuted = false;
volatile bool picostation::g_audioAttenuated = false;

pseudoatomic<picostation::FileListingStates> picostation::g_fileListingState;
pseudoatomic<uint32_t> picostation::g_fileArg;

static unsigned int s_mechachonOffset;
unsigned int picostation::g_soctOffset;
unsigned int picostation::g_subqOffset;

static volatile uint8_t s_resetPending = 0;
static volatile alarm_id_t s_subqAlarm = -1;

static picostation::PWMSettings pwmMechaconClock =
{
	.gpio = Pin::MECHACON_CLK,
#if defined(PICOSTATION_MAINBOARD_PU8)
#if defined(PICOSTATION_OVERCLOCK)
	.wrap = 41,
	.clkdiv = 1,
	.invert = false,
	.level = 21
#else
	.wrap = 67,
	.clkdiv = 1,
	.invert = false,
	.level = 34
#endif
#else
#if defined(PICOSTATION_OVERCLOCK)
	.wrap = 35,
	.clkdiv = 1,
	.invert = false,
	.level = 18
#else
	.wrap = 63,
	.clkdiv = 1,
	.invert = false,
	.level = 32
#endif
#endif
};

static picostation::PWMSettings pwmDataClock = 
{
	.gpio = Pin::DA15,
	.wrap = (1 * 32) - 1, 
	.clkdiv = 4, 
	.invert = true, 
	.level = (32 / 2)
};

static picostation::PWMSettings pwmLRClock = 
{
	.gpio = Pin::LRCK,
	.wrap = (48 * 32) - 1,
	.clkdiv = 4,
	.invert = false,
	.level = (48 * (32 / 2))
};

static picostation::PWMSettings pwmMainClock =
{
	.gpio = Pin::CLK,
	.wrap = 1,
	.clkdiv = 2,
	.invert = false,
	.level = 1
};

static inline uint32_t __time_critical_func(enabledPwmMask)()
{
    uint32_t mask = (1u << pwmLRClock.sliceNum) | (1u << pwmDataClock.sliceNum) | (1u << pwmMainClock.sliceNum);
    if (Pin::MECHACON_CLK != Pin::NOT_USED)
    {
        mask |= (1u << pwmMechaconClock.sliceNum);
    }
    return mask;
}

static void initPWM(picostation::PWMSettings *settings);

static void __time_critical_func(interruptHandler)(unsigned int gpio, uint32_t events)
{
    static uint64_t lastLowEvent = 0;
	static uint64_t lastLowEventDoor = 0;

    switch (gpio)
    {
        case Pin::RESET:
        {
            if (events & GPIO_IRQ_LEVEL_LOW)
            {
                lastLowEvent = time_us_64();
                // Disable low signal edge detection
                gpio_set_irq_enabled(Pin::RESET, GPIO_IRQ_LEVEL_LOW, false);
                // Enable high signal edge detection
                gpio_set_irq_enabled(Pin::RESET, GPIO_IRQ_LEVEL_HIGH, true);
            } 
            else if (events & GPIO_IRQ_LEVEL_HIGH)
            {
                // Disable the rising edge detection
                gpio_set_irq_enabled(Pin::RESET, GPIO_IRQ_LEVEL_HIGH, false);

                const uint64_t c_now = time_us_64();
                const uint64_t c_timeElapsed = c_now - lastLowEvent;
                
                if (c_timeElapsed >= 50000U)  // Debounce, only reset if the pin was low for more than 50000us(50 ms)
                {
                    if (c_timeElapsed >= 1000000U) // pressed more one second
					{
						s_resetPending = 2;
					}
					else
					{
						s_resetPending = 1;
					}
                }
                
				// Enable the low signal edge detection again
				gpio_set_irq_enabled(Pin::RESET, GPIO_IRQ_LEVEL_LOW, true);
            }
        } break;
        
        case Pin::DOOR:
        {
            if (events & GPIO_IRQ_LEVEL_HIGH)
            {
                lastLowEventDoor = time_us_64();
                // Disable low signal edge detection
                gpio_set_irq_enabled(Pin::DOOR, GPIO_IRQ_LEVEL_HIGH, false);
                // Enable high signal edge detection
                gpio_set_irq_enabled(Pin::DOOR, GPIO_IRQ_LEVEL_LOW, true);
            }
            else if (events & GPIO_IRQ_LEVEL_LOW)
            {
                // Disable the rising edge detection
                gpio_set_irq_enabled(Pin::DOOR, GPIO_IRQ_LEVEL_LOW, false);

                const uint64_t c_now = time_us_64();
                const uint64_t c_timeElapsed = c_now - lastLowEventDoor;
                if (c_timeElapsed >= 50000U)  // Debounce, only reset if the pin was low for more than 50000us(50 ms)
                {
                    m_i2s.s_doorPending = true;
                }
                
                // Enable the low signal edge detection again
                gpio_set_irq_enabled(Pin::DOOR, GPIO_IRQ_LEVEL_HIGH, true);
            }
        } break;

        case Pin::XLAT:
        {
            m_mechCommand.processLatchedCommand();
        } break;
    }
}

static void __time_critical_func(mech_irq_hnd)()
{
	// Update latching
	m_mechCommand.updateMech();
	pio_interrupt_clear(PIOInstance::MECHACON, 0);
}

static void __time_critical_func(send_subq)(const int Sector)
{
	picostation::SubQ subq(&picostation::g_discImage);
	
	subq.start_subq(Sector);
	s_subqAlarm = -1;
	picostation::g_subqDelay = false;
}

[[noreturn]] void __time_critical_func(picostation::core0Entry)()
{
    g_coreReady[0] = true;
    while (!g_coreReady[1].Load())
    {
        tight_loop_contents();
    }

    while (true)
    {
        uint8_t resetMode = 0;
        {
            const uint32_t irq = save_and_disable_interrupts();
            resetMode = s_resetPending;
            s_resetPending = 0;
            restore_interrupts(irq);
        }
        if (resetMode != 0)
        {
            if (resetMode != 3)
            {
                while (gpio_get(Pin::RESET) == 0)
                {
                    tight_loop_contents();
                }
            }
			reset(resetMode);
		}

        const int currentSector = g_driveMechanics.getSector();

        // Limit Switch
        gpio_put(Pin::LMTSW, currentSector > 3000);

        updatePlaybackSpeed();

        // Soct/Sled/seek
        if (m_mechCommand.getSoct())
        {
            if (pio_sm_get_rx_fifo_level(PIOInstance::SOCT, SM::SOCT))
            {
                pio_sm_drain_tx_fifo(PIOInstance::SOCT, SM::SOCT);
                m_mechCommand.setSoct(false);
                pio_sm_set_enabled(PIOInstance::SOCT, SM::SOCT, false);
            }
        }
        else if (!g_driveMechanics.isSledStopped())
        {
            g_driveMechanics.moveSled(m_mechCommand);
        }
        else if (m_mechCommand.getSens(SENS::GFS))
        {
            // g_subqDelay also acts as a "subq alarm already pending" flag.
            // Without it, a loop at the end of the disc (where the sector no
            // longer advances) can schedule thousands of alarms in a few
            // milliseconds.
            if (m_i2s.getSectorSending() == currentSector && !g_subqDelay)
            {
                const uint64_t lastSectorTime = m_i2s.getLastSectorTime();
                const uint64_t now = time_us_64();
                const uint64_t elapsed = (lastSectorTime != 0 && now > lastSectorTime) ? (now - lastSectorTime) : 0;
                const uint64_t delayUs = (elapsed < c_MaxSubqDelayTime) ? (c_MaxSubqDelayTime - elapsed) : 1;

                const uint32_t irq = save_and_disable_interrupts();
                g_driveMechanics.moveToNextSector();
                g_subqDelay = true;

                const alarm_id_t alarm = add_alarm_in_us(
                    delayUs,
					[](alarm_id_t id, void *user_data) -> int64_t {
						if (s_subqAlarm == id)
						{
							send_subq(static_cast<int>(reinterpret_cast<intptr_t>(user_data)));
						}
						return 0;
					}, reinterpret_cast<void *>(static_cast<intptr_t>(currentSector)), true);

                // Do not leave g_subqDelay stuck if the alarm pool is full.
                if (alarm <= 0)
                {
                    s_subqAlarm = -1;
                    g_subqDelay = false;
                }
                else
                {
                    s_subqAlarm = alarm;
                }
                restore_interrupts(irq);
            }
        }
    }
}

[[noreturn]] void picostation::core1Entry()
{
    m_i2s.start(m_mechCommand);
    while (1) asm("");
    __builtin_unreachable();
}

void __time_critical_func(picostation::initHW)()
{
#if DEBUG_LOGGING_ENABLED
	stdio_init_all();
    stdio_set_chars_available_callback(NULL, NULL);
    sleep_ms(1250);
#endif

    DEBUG_PRINT("Initializing...\n");

    mutex_init(&g_mechaconMutex);
    m_mechCommand.initLock();

    for (const unsigned int pin : Pin::allPins)
    {
        gpio_init(pin);
    }
    
    gpio_put(Pin::SD_CS, 1);
    gpio_set_dir(Pin::SD_CS, GPIO_OUT);
    
    gpio_set_dir(Pin::XLAT, GPIO_IN);
    gpio_set_dir(Pin::SQCK, GPIO_IN);
    gpio_set_dir(Pin::LMTSW, GPIO_OUT);
    gpio_set_dir(Pin::DOOR, GPIO_IN);
    gpio_set_dir(Pin::RESET, GPIO_IN);
    gpio_set_dir(Pin::SENS, GPIO_OUT);
    gpio_set_dir(Pin::DA15, GPIO_OUT);
    gpio_set_dir(Pin::DA16, GPIO_OUT);
    gpio_set_dir(Pin::LRCK, GPIO_OUT);
    gpio_set_dir(Pin::SCOR, GPIO_OUT);
    gpio_put(Pin::SCOR, 0);
    gpio_set_dir(Pin::SQSO, GPIO_OUT);
    gpio_put(Pin::SQSO, 0);
    gpio_set_dir(Pin::CLK, GPIO_OUT);
    gpio_set_dir(Pin::CMD_CK, GPIO_IN);
    gpio_set_dir(Pin::CMD_DATA, GPIO_IN);

    gpio_set_input_hysteresis_enabled(Pin::XLAT, true);
    gpio_set_input_hysteresis_enabled(Pin::SQCK, true);
    gpio_set_input_hysteresis_enabled(Pin::RESET, true);
    gpio_set_input_hysteresis_enabled(Pin::CMD_CK, true);
    
    if (Pin::EXP_I2C0_SDA != Pin::NOT_USED && Pin::EXP_I2C0_SCL != Pin::NOT_USED)
    {
        i2c_init(i2c0, 400*1000);
        gpio_set_function(Pin::EXP_I2C0_SDA, GPIO_FUNC_I2C);
        gpio_set_function(Pin::EXP_I2C0_SCL, GPIO_FUNC_I2C);
        gpio_pull_up(Pin::EXP_I2C0_SDA);
        gpio_pull_up(Pin::EXP_I2C0_SCL);

        // Initialize the Si5351
        if (si5351_Init(0))
        {
            si5351_SetupCLK1(53203425, SI5351_DRIVE_STRENGTH_8MA);
            si5351_SetupCLK2(53693175, SI5351_DRIVE_STRENGTH_8MA);
            si5351_EnableOutputs((1<<1) | (1<<2));
        }
    }

    if (Pin::MECHACON_CLK != Pin::NOT_USED)
    {
        gpio_init(Pin::MECHACON_CLK);
        gpio_set_dir(Pin::MECHACON_CLK, GPIO_OUT);
        initPWM(&pwmMechaconClock);
    }
    initPWM(&pwmMainClock);
    initPWM(&pwmDataClock);
    initPWM(&pwmLRClock);

    uint32_t i2s_pio_offset = pio_add_program(PIOInstance::I2S_DATA, &i2s_data_program);
    i2s_data_program_init(PIOInstance::I2S_DATA, SM::I2S_DATA, i2s_pio_offset, Pin::DA15, Pin::DA16);

    s_mechachonOffset = pio_add_program(PIOInstance::MECHACON, &mechacon_program);
    mechacon_program_init(PIOInstance::MECHACON, SM::MECHACON, s_mechachonOffset, Pin::CMD_DATA);

    g_soctOffset = pio_add_program(PIOInstance::SOCT, &soct_program);
    g_subqOffset = pio_add_program(PIOInstance::SUBQ, &subq_program);

    pio_sm_set_enabled(PIOInstance::I2S_DATA, SM::I2S_DATA, true);
    pwm_set_mask_enabled(enabledPwmMask());

    uint64_t startTime = time_us_64();
    gpio_set_dir(Pin::RESET, GPIO_OUT);
    gpio_put(Pin::RESET, 0);
    sleep_ms(300);
    gpio_set_dir(Pin::RESET, GPIO_IN);

    while ((time_us_64() - startTime) < 30000)
    {
        if (gpio_get(Pin::RESET) == 0)
        {
            startTime = time_us_64();
        }
    }

    while ((time_us_64() - startTime) < 30000)
    {
        if (gpio_get(Pin::CMD_CK) == 0)
        {
            startTime = time_us_64();
        }
    }

    gpio_set_irq_enabled_with_callback(Pin::RESET, GPIO_IRQ_LEVEL_LOW, true, &interruptHandler);
    gpio_set_irq_enabled_with_callback(Pin::DOOR, GPIO_IRQ_LEVEL_HIGH, true, &interruptHandler);
    gpio_set_irq_enabled_with_callback(Pin::XLAT, GPIO_IRQ_EDGE_FALL, true, &interruptHandler);

    pio_sm_set_enabled(PIOInstance::MECHACON, SM::MECHACON, true);
    
    pio_set_irq0_source_enabled(PIOInstance::MECHACON, (enum pio_interrupt_source)pis_interrupt0, true);
    
    
    pio_interrupt_clear(PIOInstance::MECHACON, 0);
    irq_set_exclusive_handler(PIO0_IRQ_0, mech_irq_hnd);
    irq_set_enabled(PIO0_IRQ_0, true);

    g_coreReady[0] = false;
    g_coreReady[1] = false;

    DEBUG_PRINT("ON!\n");
}

static void __time_critical_func(initPWM)(picostation::PWMSettings *settings)
{
    gpio_set_function(settings->gpio, GPIO_FUNC_PWM);
    settings->sliceNum = pwm_gpio_to_slice_num(settings->gpio);
    settings->config = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&settings->config, PWM_DIV_FREE_RUNNING);
    pwm_config_set_wrap(&settings->config, settings->wrap);
    pwm_config_set_clkdiv(&settings->config, settings->clkdiv);
    pwm_config_set_output_polarity(&settings->config, settings->invert, settings->invert);
    pwm_init(settings->sliceNum, &settings->config, false);
    pwm_set_both_levels(settings->sliceNum, settings->level, settings->level);
}

void __time_critical_func(picostation::updatePlaybackSpeed)()
{
    static constexpr unsigned int c_clockDivNormal = 4;
    static constexpr unsigned int c_clockDivDouble = 2;

    if (s_currentPlaybackSpeed != g_targetPlaybackSpeed)
    {
        const uint32_t irq = save_and_disable_interrupts();
        s_currentPlaybackSpeed = g_targetPlaybackSpeed;
        const unsigned int clock_div = (s_currentPlaybackSpeed == 1) ? c_clockDivNormal : c_clockDivDouble;
        pwm_set_mask_enabled(0);
        pwm_config_set_clkdiv_int(&pwmDataClock.config, clock_div);
        pwm_config_set_clkdiv_int(&pwmLRClock.config, clock_div);
        pwm_hw->slice[pwmDataClock.sliceNum].div = pwmDataClock.config.div;
        pwm_hw->slice[pwmLRClock.sliceNum].div = pwmLRClock.config.div;
        pwm_set_mask_enabled(enabledPwmMask());
        restore_interrupts(irq);
        DEBUG_PRINT("x%i\n", s_currentPlaybackSpeed);
    }
}

void __time_critical_func(picostation::setOverclock)(const bool enable)
{
#if defined(PICOSTATION_OVERCLOCK)
    static int8_t s_overclockState = 1;
#else
    static int8_t s_overclockState = 0;
#endif
    const int8_t targetState = enable ? 1 : 0;
    if (s_overclockState == targetState)
    {
        return;
    }
    s_overclockState = targetState;

#if defined(PICOSTATION_MAINBOARD_PU8)
    const uint16_t wrap = enable ? 41 : 67;
    const uint16_t level = enable ? 21 : 34;
    const uint32_t scexBitTimeUs = enable ? 2475 : 4000;
#else
    const uint16_t wrap = enable ? 35 : 63;
    const uint16_t level = enable ? 18 : 32;
    const uint32_t scexBitTimeUs = enable ? 2252 : 4000;
#endif

    g_driveMechanics.setMaxTrackMoveTime(enable ? 15u : 29u);
    ModChip::setBitTimeUs(scexBitTimeUs);

    if (Pin::MECHACON_CLK != Pin::NOT_USED)
    {
        const uint32_t irq = save_and_disable_interrupts();
        pwmMechaconClock.wrap = wrap;
        pwmMechaconClock.level = level;
        pwm_set_mask_enabled(0);
        pwm_set_wrap(pwmMechaconClock.sliceNum, wrap);
        pwm_set_both_levels(pwmMechaconClock.sliceNum, level, level);
        pwm_set_mask_enabled(enabledPwmMask());
        restore_interrupts(irq);
    }
}

void __time_critical_func(picostation::cancelSubqAlarm)()
{
    const uint32_t irq = save_and_disable_interrupts();
    const alarm_id_t pendingAlarm = s_subqAlarm;
    s_subqAlarm = -1;
    g_subqDelay = false;
    restore_interrupts(irq);
    if (pendingAlarm >= 0)
    {
        cancel_alarm(pendingAlarm);
    }
}

void __time_critical_func(picostation::requestHardReset)()
{
    const uint32_t irq = save_and_disable_interrupts();
    s_resetPending = 3;
    restore_interrupts(irq);
}

void __time_critical_func(picostation::reset)(const uint8_t resetMode)
{
    DEBUG_PRINT("RESET!\n");
    if (resetMode == 3)
    {
        // Prevent our own hardware RESET pulse from re-triggering the button IRQ
        gpio_set_irq_enabled(Pin::RESET, GPIO_IRQ_LEVEL_LOW, false);
        gpio_set_irq_enabled(Pin::RESET, GPIO_IRQ_LEVEL_HIGH, false);
    }
    cancelSubqAlarm();
    m_i2s.i2s_set_state(0);
    pio_sm_set_enabled(PIOInstance::SUBQ, SM::SUBQ, false);
    pio_sm_set_enabled(PIOInstance::SOCT, SM::SOCT, false);
    pio_sm_restart(PIOInstance::MECHACON, SM::MECHACON);

    pio_sm_clear_fifos(PIOInstance::MECHACON, SM::MECHACON);
    pio_sm_clear_fifos(PIOInstance::SOCT, SM::SOCT);
    pio_sm_clear_fifos(PIOInstance::SUBQ, SM::SUBQ);

    g_targetPlaybackSpeed = 1;
    updatePlaybackSpeed();
    g_audioCtrlMode = audioControlModes::NORMAL;
    g_audioMuted = false;
    g_audioAttenuated = false;
    g_discImage.resetAudioPeak();
    
    if (resetMode == 2)
    {
        // Stop Core 1 from starting any new SD sector read and wait for an
        // in-flight readSectorSD() to finish before freeing open track FILs.
        s_dataLocation = picostation::DiscImage::DataLocation::RAM;
        m_i2s.menu_active = true;
        m_i2s.s_doorPending = false;
        needFileCheckAction = picostation::FileListingStates::IDLE;
        listReadyState = 0;
        g_readyDataKind = picostation::MenuDataKind::NONE;
        g_fileCmdSeq = g_fileCmdSeq.Load() + 1u;
        __dmb();
        while (m_i2s.isSdReadInProgress())
        {
            tight_loop_contents();
        }
        g_picoAudio.deactivate();
        g_discImage.unload();
        g_discImage.makeDummyCue();
        g_discImage.set_skip_bootsector(false);
        g_discImage.set_skip_edc(false);
#if defined(PICOSTATION_OVERCLOCK)
        setOverclock(true);
#endif
        m_mechCommand.resetBootSectorPattern();
        picostation::DirectoryListing::gotoRoot();
    }
    else if (resetMode == 3)
    {
        // Wait for any in-flight Core 1 SD read to finish before resetting I2S/drive
        while (m_i2s.isSdReadInProgress())
        {
            tight_loop_contents();
        }
    }

    mechacon_program_init(PIOInstance::MECHACON, SM::MECHACON, s_mechachonOffset, Pin::CMD_DATA);
    g_subqDelay = false;
    m_mechCommand.setSoct(false);

    gpio_put(Pin::SCOR, 0);
    gpio_put(Pin::SQSO, 0);
	g_driveMechanics.resetDrive();
	m_i2s.reinitI2S();
    if (resetMode != 3)
    {
        g_discImage.set_skip_bootsector(false);
    }
    m_mechCommand.resetBootSectorPattern();
    ModChip::requestReset();

    if (resetMode == 3)
    {
        // Open-drain style pulse on PS1 /RESET (identical to initHW / short physical press):
        // resets CPU + GPU + SPU + HC05 together while keeping mounted disc & options intact.
        gpio_put(Pin::RESET, 0);
        gpio_set_dir(Pin::RESET, GPIO_OUT);
        sleep_ms(200);
        gpio_set_dir(Pin::RESET, GPIO_IN);
    }
	
	uint64_t startTime = time_us_64();
	
	while ((time_us_64() - startTime) < 30000)
	{
		if (gpio_get(Pin::RESET) == 0)
		{
			startTime = time_us_64();
		}
	}
	
	while ((time_us_64() - startTime) < 30000)
	{
		if (gpio_get(Pin::CMD_CK) == 0)
		{
			startTime = time_us_64();
		}
	}
	
    pio_sm_set_enabled(PIOInstance::MECHACON, SM::MECHACON, true);
    
	gpio_set_irq_enabled(Pin::RESET, GPIO_IRQ_LEVEL_HIGH, false);
	gpio_set_irq_enabled(Pin::RESET, GPIO_IRQ_LEVEL_LOW, true);
}

