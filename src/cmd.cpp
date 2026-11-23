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
#include "directory_listing.h"

#if DEBUG_CMD
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...) while (0)
#endif
extern picostation::I2S m_i2s;
extern pseudoatomic<uint32_t> g_fileArg;
extern pseudoatomic<picostation::FileListingStates> needFileCheckAction;
extern pseudoatomic<int> listReadyState;

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
			g_fileArg = command.custom_cmd.arg;

			switch (command.custom_cmd.cmd)
			{
				case COMMAND_NONE:
				{
					needFileCheckAction = FileListingStates::IDLE;
					break;
				}
					
				case COMMAND_GOTO_ROOT:
				{
					DEBUG_PRINT("GOTO_ROOT\n");
					listReadyState = 0;
					needFileCheckAction = FileListingStates::GOTO_ROOT;
					break;
				}
					
				case COMMAND_GOTO_PARENT:
				{
					DEBUG_PRINT("GOTO_PARENT\n");
					listReadyState = 0;
					needFileCheckAction = FileListingStates::GOTO_PARENT;
					break;
				}
					
				case COMMAND_GOTO_DIRECTORY:
				{
					DEBUG_PRINT("GOTO_DIRECTORY\n");
					listReadyState = 0;
					needFileCheckAction = FileListingStates::GOTO_DIRECTORY;
					break;
				}
					
				case COMMAND_GET_NEXT_CONTENTS:
				{
					DEBUG_PRINT("GET_NEXT_CONTENTS\n");
					listReadyState = 0;
					needFileCheckAction = FileListingStates::GET_NEXT_CONTENTS;
					break;
				}
					
				case COMMAND_MOUNT_FILE:
				{
					DEBUG_PRINT("MOUNT_FILE\n");
					needFileCheckAction = FileListingStates::MOUNT_FILE;
					break;
				}
					
				case COMMAND_IO_COMMAND:
				{
					DEBUG_PRINT("COMMAND_IO_COMMAND %x\n", command.custom_cmd.arg);
					break;
				}
					
				case COMMAND_IO_DATA:
				{
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
							listReadyState = 0;
							needFileCheckAction = FileListingStates::GET_CFG;
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
					listReadyState = 0;
					needFileCheckAction = FileListingStates::GET_COVER;
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
			if (!((1u << command.cmd.id) & 0x6Au))
			{
				setBootSectorPattern(command.cmd.id);
			}
			break;
	}
}

void __time_critical_func(picostation::MechCommand::setBootSectorPattern) (uint8_t value)
{
	if (!isBootSectorPattern())
	{
		switch (value)
		{
		case 10:
			if (m_bootSectorPattern == 0b0011)
			{
				m_bootSectorPattern |= 1 << 2;
			}
			else
			{
				m_bootSectorPattern = 1 << 0;	
			}

			break;

		case 255:
			if (m_bootSectorPattern == 0b0001)
			{
				m_bootSectorPattern |= 1 << 1;
			}
			break;
		
		default:
			// Keep the upstream behavior: unrelated commands clear the
			// pattern. Only the stale static state was a bug; the heuristic
			// itself is left as the community tuned it.
			m_bootSectorPattern = 0;
			break;
		}

		if (m_bootSectorPattern == 0b0111)
		{
			g_discImage.set_skip_bootsector(true); 
			DEBUG_PRINT("BOOTSECTOR PATTERN\n");
		}
	}
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

