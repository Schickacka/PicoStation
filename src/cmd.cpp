#include "cmd.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "i2s.h"
#include "drive_mechanics.h"
#include "hardware/pio.h"
#include "logging.h"
#include "main.pio.h"
#include "pico/bootrom.h"
#include "picostation.h"
#include "pseudo_atomics.h"
#include "values.h"

#if DEBUG_CMD
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...) while (0)
#endif
extern picostation::I2S m_i2s;
extern pseudoatomic<uint32_t> g_fileArg;
extern pseudoatomic<picostation::FileListingStates> needFileCheckAction;
extern pseudoatomic<int> listReadyState;

static spin_lock_t *s_bootPatternLock = nullptr;

void picostation::MechCommand::initLock()
{
    if (!s_bootPatternLock)
    {
        s_bootPatternLock = spin_lock_init(spin_lock_claim_unused(true));
    }
}

void __time_critical_func(picostation::MechCommand::cancelSoct)()
{
    setSoct(false);
    pio_sm_set_enabled(PIOInstance::SOCT, SM::SOCT, false);
    pio_sm_clear_fifos(PIOInstance::SOCT, SM::SOCT);
}

void __time_critical_func(picostation::MechCommand::resetBootSectorPattern)()
{
    cancelXbusyAlarm();
    const uint32_t irq = spin_lock_blocking(s_bootPatternLock);
    m_bootSectorPattern = 0;
    m_latched = 0;
    m_jumpTrack = 0;
    m_dir = false;
    m_prevDir = false;
    m_sledBreak = false;
    m_lastSledStop = 0;
    spin_unlock(s_bootPatternLock, irq);
}

void __time_critical_func(picostation::MechCommand::drainMechFifoLocked)()
{
    while (pio_sm_get_rx_fifo_level(PIOInstance::MECHACON, SM::MECHACON))
    {
        const uint32_t c = pio_sm_get_blocking(PIOInstance::MECHACON, SM::MECHACON) >> 24;
        m_latched = (m_latched >> 8) | (c << 16);
        m_currentSens = c >> 4;
    }
}

void __time_critical_func(picostation::MechCommand::processLatchedCommand)()
{
    mech_cmd command;
    {
        // Drain any remaining byte from the PIO RX FIFO together with reading
        // and clearing m_latched so PIO0_IRQ_0 and XLAT never tear a command.
        const uint32_t irq = save_and_disable_interrupts();
        drainMechFifoLocked();
        command.raw = m_latched;
        m_latched = 0;
        gpio_put(Pin::SENS, m_sensData[m_currentSens]);
        restore_interrupts(irq);
    }
    
	switch (command.cmd.id)
    {
		case MECH_CMD_TRACKING_MODE:
		{
			const uint64_t now = time_us_64();
			if (!g_driveMechanics.isSledStopped())
			{
				DEBUG_PRINT("%c\n", (!m_dir) ? '+' : '-');

				const bool wasBrake = g_driveMechanics.isSledBrake();
				g_driveMechanics.stopSled();

				if (!wasBrake)
				{
					// Arm a single-use brake window for an immediate reverse kick
					// (mechanical sled braking from the HC05).
					m_sledBreak = true;
					m_prevDir = m_dir;
					m_lastSledStop = now;
				}
				else
				{
					m_sledBreak = false;
				}
			}

			switch (command.tracking_mode.sled)
			{		
				case SLED_FORWARD:
				case SLED_REVERSE:
				{
					const bool newDir = (command.tracking_mode.sled == SLED_REVERSE);
					const bool isBrake = m_sledBreak && (m_prevDir != newDir) && ((now - m_lastSledStop) < 40000U);
					if (isBrake)
					{
						// Consume the single brake pulse so subsequent real seeks are never dropped.
						m_sledBreak = false;
					}
					DEBUG_PRINT("%s%s\n", newDir ? "SLED REVERSE" : "SLED FORWARD", isBrake ? " (BRAKE)" : "");
					m_dir = newDir;
					m_i2s.i2s_set_state(0);
					g_driveMechanics.startSled(newDir, isBrake);
					break;
				}
			}
			break;
		}
		
		case MECH_CMD_AUTO_SEQUENCE:
		{
	        switch(command.aseq_cmd.cmd)
	        {
	            case ASEQ_CMD_CANCEL:
					cancelXbusyAlarm();
					setSens(SENS::XBUSY, false);
	            	m_i2s.i2s_set_state(1);
					return;
				
	            case ASEQ_CMD_FOCUS_ON:
				{
					DEBUG_PRINT("ASEQ FOCUS ON\n");
					cancelXbusyAlarm();
					setSens(SENS::FOK, true);
					m_i2s.i2s_set_state(0);
					setSens(SENS::XBUSY, true);
					const uint32_t irq = save_and_disable_interrupts();
					const alarm_id_t alarm = add_alarm_in_ms(15, [](alarm_id_t id, void *user_data) -> int64_t 
					{
						picostation::MechCommand *mechCommand = static_cast<picostation::MechCommand *>(user_data);
						if (mechCommand->m_xbusyAlarm == id)
						{
							mechCommand->m_xbusyAlarm = -1;
							mechCommand->resetXBUSY();
						}
						return 0;
					}, this, true);
					m_xbusyAlarm = (alarm >= 0) ? alarm : -1;
					restore_interrupts(irq);
					break;
				}

				case ASEQ_CMD_1TRK_JUMP:
					DEBUG_PRINT("ASEQ 1TRK %c\n", command.aseq_cmd.dir ? '-' : '+');
					g_driveMechanics.setSector(1, command.aseq_cmd.dir);
					break;

				case ASEQ_CMD_10TRK_JUMP:
					DEBUG_PRINT("ASEQ 10TRK %c\n", command.aseq_cmd.dir ? '-' : '+');
					g_driveMechanics.setSector(10, command.aseq_cmd.dir);
					break;

	            case ASEQ_CMD_2NTRK_JUMP:
					DEBUG_PRINT("ASEQ 2NTRK (%d) %c\n", m_jumpTrack << 1, command.aseq_cmd.dir ? '-' : '+');
	            	g_driveMechanics.setSector(static_cast<uint32_t>(m_jumpTrack) << 1, command.aseq_cmd.dir);
					break;

				case ASEQ_CMD_MTRK_JUMP:
					DEBUG_PRINT("ASEQ MTRK (%d) %c\n", m_jumpTrack, command.aseq_cmd.dir ? '-' : '+');
					g_driveMechanics.setSector(m_jumpTrack, command.aseq_cmd.dir);
					break;
			}
			break;
		}
		
		case MECH_CMD_ASEQ_TRACK_COUNT:
		{
			m_jumpTrack = command.aseq_track_count.count;
			break;
		}
		
		case MECH_CMD_MODE_SPECIFICATION:
		{
			if (command.mode_specification.SOCT)
			{
				pio_sm_set_enabled(PIOInstance::SUBQ, SM::SUBQ, false);
				pio_sm_set_enabled(PIOInstance::SOCT, SM::SOCT, false);
				soct_program_init(PIOInstance::SOCT, SM::SOCT, g_soctOffset, Pin::SQSO, Pin::SQCK);
				pio_sm_clear_fifos(PIOInstance::SOCT, SM::SOCT);
				pio_sm_set_enabled(PIOInstance::SOCT, SM::SOCT, true);
				pio_sm_put(PIOInstance::SOCT, SM::SOCT, 0xFFFFFFF);
				setSoct(true);
			}
			else
			{
				// If the console cancels SOCT without draining the FIFO, the
				// SOCT state machine would otherwise keep driving SQSO and
				// collide with the SUBQ state machine; clear FIFOs too so the
				// next SOCT activation cannot stall on a full TX FIFO.
				setSoct(false);
				pio_sm_set_enabled(PIOInstance::SOCT, SM::SOCT, false);
				pio_sm_clear_fifos(PIOInstance::SOCT, SM::SOCT);
			}
			break;
		}
		
		case MECH_CMD_FUNCTION_SPECIFICATION:
		{
			g_targetPlaybackSpeed = command.function_specification.DSPB + 1;
			break;
		}

		case MECH_CMD_AUDIO_CONTROL:
		{
			const unsigned int pct = command.audio_control.PCT;
			const bool mute = (command.audio_control.Mute != 0);
			const bool att = (command.audio_control.ATT != 0);
			g_audioCtrlMode = pct;
			g_audioMuted = mute;
			g_audioAttenuated = att;
			if (mute || pct == audioControlModes::NORMAL || pct == audioControlModes::ALTNORMAL)
			{
				g_discImage.resetAudioPeak();
			}
			// Do NOT call setBootSectorPattern here: official RC5 removed the
			// 10 -> 255 -> 10 heuristic because it prematurely flips
			// skip_bootsector = true mid-boot on Japanese (SCEI) BIOSes, breaking
			// Full Boot of out-of-region games (e.g. Resident Evil 2/3 USA).
			break;
		}
		
		case MECH_CMD_CLV_MODE:
		{
			m_sledBreak = false;
			switch(command.clv_mode.mode)
			{
				case CLV_MODE_STOP:
					cancelXbusyAlarm();
					setSens(SENS::GFS, false);
					m_i2s.i2s_set_state(0);
					DEBUG_PRINT("Stop\n");
					break;
				
				case CLV_MODE_BRAKE:
					cancelXbusyAlarm();
					DEBUG_PRINT("Brake\n");
					break;
					
				case CLV_MODE_KICK:
					DEBUG_PRINT("Kick\n");
					break;

				case CLV_MODE_CLVS:
				case CLV_MODE_CLVH:
				case CLV_MODE_CLVP:
				case CLV_MODE_CLVA:
					setSens(SENS::GFS, true);
					m_i2s.i2s_set_state(1);
					setSens(SENS::XBUSY, false);
					break;
			}
			break;
		}
		
		case MECH_CMD_CUSTOM:
		{
			auto dispatchFileAction = [&](FileListingStates nextAction, bool clearReady) {
				if (clearReady)
				{
					listReadyState = 0;
					picostation::g_readyDataKind = picostation::MenuDataKind::NONE;
				}
				g_fileArg = command.custom_cmd.arg;
				__dmb();
				needFileCheckAction = nextAction;
				__dmb();
				picostation::g_fileCmdSeq = picostation::g_fileCmdSeq.Load() + 1u;
			};

			switch (command.custom_cmd.cmd)
			{
				case COMMAND_NONE:
				{
					dispatchFileAction(FileListingStates::IDLE, true);
					break;
				}
					
				case COMMAND_GOTO_ROOT:
				{
					DEBUG_PRINT("GOTO_ROOT\n");
					dispatchFileAction(FileListingStates::GOTO_ROOT, true);
					break;
				}
					
				case COMMAND_GOTO_PARENT:
				{
					DEBUG_PRINT("GOTO_PARENT\n");
					dispatchFileAction(FileListingStates::GOTO_PARENT, true);
					break;
				}
					
				case COMMAND_GOTO_DIRECTORY:
				{
					DEBUG_PRINT("GOTO_DIRECTORY\n");
					dispatchFileAction(FileListingStates::GOTO_DIRECTORY, true);
					break;
				}
					
				case COMMAND_GET_NEXT_CONTENTS:
				{
					DEBUG_PRINT("GET_NEXT_CONTENTS\n");
					dispatchFileAction(FileListingStates::GET_NEXT_CONTENTS, true);
					break;
				}
					
				case COMMAND_MOUNT_FILE:
				{
					DEBUG_PRINT("MOUNT_FILE\n");
					dispatchFileAction(FileListingStates::MOUNT_FILE, true);
					break;
				}
					
				case COMMAND_IO_COMMAND:
				{
					g_fileArg = command.custom_cmd.arg;
					DEBUG_PRINT("COMMAND_IO_COMMAND %x\n", command.custom_cmd.arg);
					break;
				}
					
				case COMMAND_IO_DATA:
				{
					g_fileArg = command.custom_cmd.arg;
					DEBUG_PRINT("COMMAND_IO_DATA %x\n", command.custom_cmd.arg);
					break;
				}
				
				case COMMAND_EXTENDED:
				{
					switch (command.custom_cmd.arg >> 1)
					{
						case EXTENDED_SKIP_BOOTSECTOR:
							g_discImage.set_skip_bootsector(command.custom_cmd.arg & 1);
							break;
						
						case EXTENDED_SKIP_EDC:
							g_discImage.set_skip_edc(command.custom_cmd.arg & 1);
							break;
						
						case EXTENDED_GET_CFG:
							dispatchFileAction(FileListingStates::GET_CFG, true);
							break;

						case EXTENDED_GET_BG:
							dispatchFileAction(FileListingStates::GET_BG, true);
							break;

						case EXTENDED_OVERCLOCK:
#if defined(PICOSTATION_OVERCLOCK)
							picostation::setOverclock((command.custom_cmd.arg & 1) != 0);
#endif
							break;
						
						default:
							break;
					}
					break;
				}
				
				case COMMAND_GET_COVER:
				{
					dispatchFileAction(FileListingStates::GET_COVER, true);
					break;
				}

				case COMMAND_GET_CHEAT:
				{
					dispatchFileAction(FileListingStates::GET_CHEAT, true);
					break;
				}

				case COMMAND_PICOPLAYER:
				{
					if (command.custom_cmd.arg == PP_SUBCMD_SCAN_SD)
					{
						dispatchFileAction(FileListingStates::PP_SCAN_SD, true);
					}
					else if (command.custom_cmd.arg == PP_SUBCMD_MOUNT_SD)
					{
						dispatchFileAction(FileListingStates::PP_MOUNT_SD, true);
					}
					else if (command.custom_cmd.arg == PP_SUBCMD_EXIT)
					{
						dispatchFileAction(FileListingStates::PP_EXIT, true);
					}
					else if ((command.custom_cmd.arg & PP_SUBCMD_GET_ART_MASK) != 0)
					{
						dispatchFileAction(FileListingStates::PP_GET_ART, true);
					}
					break;
				}
				
				case COMMAND_BOOTLOADER:
				{
					if (command.custom_cmd.arg == 0xBEEF)
					{
						// Restart into bootloader
						rom_reset_usb_boot_extra(Pin::LED, 0, false);
					}
					break;
				}
				
				default:
					break;
			}
			break;
		}
		
		default:
			break;
	}
}

void __time_critical_func(picostation::MechCommand::setBootSectorPattern) (uint8_t value)
{
	// Official RC5 firmware controls skip_bootsector exclusively via
	// COMMAND_EXTENDED / EXTENDED_SKIP_BOOTSECTOR (reset to false on MOUNT_FILE
	// and hardware RESET). The legacy 10 -> 255 -> 10 heuristic broke Full Boot
	// of out-of-region games on Japanese (SCEI) BIOSes by disabling bootsector
	// patching right before the BIOS read sectors 0..4.
	(void)value;
}

bool __time_critical_func(picostation::MechCommand::getSens)(const size_t what) const
{
    if (what >= 16)
    {
        return false;
    }
    return m_sensData[what];
}

void __time_critical_func(picostation::MechCommand::setSens)(const size_t what, const bool new_value)
{
    if (what >= 16)
    {
        return;
    }

    const uint32_t irq = save_and_disable_interrupts();
    m_sensData[what] = new_value;
    if (what == m_currentSens)
    {
        gpio_put(Pin::SENS, new_value);
    }
    restore_interrupts(irq);
}

bool picostation::MechCommand::getSoct() { return m_soctEnabled.Load(); }
void picostation::MechCommand::setSoct(const bool new_value) { m_soctEnabled = new_value; }

void __time_critical_func(picostation::MechCommand::resetXBUSY)()
{
	m_i2s.i2s_set_state(1);
	setSens(SENS::XBUSY, false);
}

void __time_critical_func(picostation::MechCommand::updateMech)() 
{
    const uint32_t irq = save_and_disable_interrupts();
    drainMechFifoLocked();
    gpio_put(Pin::SENS, m_sensData[m_currentSens]);
    restore_interrupts(irq);
}

