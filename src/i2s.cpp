#include "i2s.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <array>

#include "cmd.h"
#include "directory_listing.h"
#include "disc_image.h"
#include "drive_mechanics.h"
#include "ff.h"
#include "global.h"
#include "hardware/pio.h"
#include "logging.h"
#include "main.pio.h"
#include "modchip.h"
#include "pico/stdlib.h"
#include "pico_audio.h"
#include "picostation.h"
#include "pseudo_atomics.h"
#include "subq.h"
#include "values.h"
#include "listingBuilder.h"

#if DEBUG_I2S
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...) while (0)
#endif

pseudoatomic<picostation::FileListingStates> needFileCheckAction;
pseudoatomic<int> listReadyState;
pseudoatomic<int> g_entryOffset;
pseudoatomic<uint32_t> picostation::g_fileCmdSeq;
pseudoatomic<picostation::MenuDataKind> picostation::g_readyDataKind;

pseudoatomic<picostation::DiscImage::DataLocation> s_dataLocation;

static uint16_t *__time_critical_func(generateScramblingLUT)()
{
    static uint16_t ScramblingLUT[1176] = {0};
    int shift = 1;
	
	for (int i = 0; i < 6; i++)
	{
		ScramblingLUT[i] = 0;
	}
	
    for (size_t i = 6; i < 1176; i++)
    {
        uint8_t upper = shift & 0xFF;
        for (size_t j = 0; j < 8; j++)
        {
            unsigned bit = ((shift & 1) ^ ((shift & 2) >> 1)) << 15;
            shift = (bit | shift) >> 1;
        }

        uint8_t lower = shift & 0xFF;

        ScramblingLUT[i] = (lower << 8) | upper;

        for (size_t j = 0; j < 8; j++)
        {
            unsigned bit = ((shift & 1) ^ ((shift & 2) >> 1)) << 15;
            shift = (bit | shift) >> 1;
        }
    }

    return ScramblingLUT;
}

int __time_critical_func(picostation::I2S::initDMA)(const volatile void *read_addr, unsigned int transfer_count)
{
    int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(channel);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    const unsigned int i2sDREQ = PIOInstance::I2S_DATA == pio0 ? DREQ_PIO0_TX0 : DREQ_PIO1_TX0;
    channel_config_set_dreq(&c, i2sDREQ);
    dma_channel_configure(channel, &c, &PIOInstance::I2S_DATA->txf[SM::I2S_DATA], read_addr, transfer_count, false);

    return channel;
}

[[noreturn]] void __time_critical_func(picostation::I2S::start)(MechCommand &mechCommand)
{
    picostation::ModChip modChip;
    static uint32_t pioSamples[CACHED_SECS][1176];
    static uint16_t *cdScramblingLUT = generateScramblingLUT();

    static uint8_t bufferForDMA = 1;
    static uint8_t bufferForSDRead = 0;
    static int currentSector = -1;
    lastSector = -1;
    m_sectorSending = -1;
    m_lastSectorTime = 0;
    static uint32_t loadedImageIndex = 0;
    static uint16_t img_count;

    needFileCheckAction = picostation::FileListingStates::IDLE;
    listReadyState = 0;
    picostation::g_fileCmdSeq = 0;
    picostation::g_readyDataKind = picostation::MenuDataKind::NONE;
    g_entryOffset = 0;
    s_dataLocation = picostation::DiscImage::DataLocation::RAM;
    
    menu_active = true;
    s_doorPending = false;
    
    reinitI2S();
    g_picoAudio.setScratchArena(&pioSamples[2][0], (CACHED_SECS - 2u) * sizeof(pioSamples[0]));
	
    dmaChannel = initDMA(pioSamples[0], 1176);

    g_coreReady[1] = true;          // Core 1 is ready
    while (!g_coreReady[0].Load())  // Wait for Core 0 to be ready
    {
        tight_loop_contents();
    }

    modChip.init();

#if DEBUG_I2S
    uint64_t startTime;
    uint64_t endTime;
#endif

    g_discImage.makeDummyCue();
	
    // this need to be moved to diskimage
    picostation::DirectoryListing::init();
    picostation::DirectoryListing::getDirectoryEntries(0);

    while (true)
    {
        // Sector could change during the loop, so we need to keep track of it
        currentSector = g_driveMechanics.getSector();
        
        modChip.sendLicenseString(currentSector, mechCommand);
		
		if (needFileCheckAction.Load() == picostation::FileListingStates::PP_EXIT)
		{
			picostation::cancelSubqAlarm();
			mechCommand.cancelSoct();
			g_picoAudio.deactivate();
			g_discImage.unload();
			g_discImage.makeDummyCue();
			s_dataLocation = picostation::DiscImage::DataLocation::RAM;
			menu_active = true;
			s_doorPending = false;
			needFileCheckAction = picostation::FileListingStates::IDLE;
			picostation::g_readyDataKind = picostation::MenuDataKind::NONE;
			listReadyState = 0;
			reinitI2S();
			bufferForDMA = 0;
			bufferForSDRead = 1;
			lastSector = -1;
			g_driveMechanics.resetDrive();
			g_discImage.set_skip_bootsector(false);
			g_discImage.set_skip_edc(false);
			mechCommand.resetBootSectorPattern();
			modChip.reset();
			continue;
		}
		
		if (menu_active && needFileCheckAction.Load() != picostation::FileListingStates::IDLE)
		{
			const uint32_t cmdSeq = picostation::g_fileCmdSeq.Load();
			__dmb();
			const auto pendingAction = needFileCheckAction.Load();
			const uint32_t cmdArg = g_fileArg.Load();

			switch (pendingAction)
			{
				case picostation::FileListingStates::GOTO_ROOT:
				{
					//printf("Processing GOTO_ROOT\n");
					picostation::DirectoryListing::gotoRoot();
					g_entryOffset = 0;
					if (picostation::g_fileCmdSeq.Load() == cmdSeq)
					{
						needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
					}
					break;
				}
				
				case picostation::FileListingStates::GOTO_PARENT:
				{
					//printf("Processing GOTO_PARENT\n");
					picostation::DirectoryListing::gotoParentDirectory();
					g_entryOffset = 0;
					if (picostation::g_fileCmdSeq.Load() == cmdSeq)
					{
						needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
					}
					break;
				}
				
				case picostation::FileListingStates::GOTO_DIRECTORY:
				{
					//printf("Processing GOTO_DIRECTORY %i\n", cmdArg);
					picostation::DirectoryListing::gotoDirectory(cmdArg);
					g_entryOffset = 0;
					if (picostation::g_fileCmdSeq.Load() == cmdSeq)
					{
						needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
					}
					break;
				}
				
				case picostation::FileListingStates::GET_NEXT_CONTENTS:
				{
					//printf("Processing GET_NEXT_CONTENTS\n");
					g_entryOffset = static_cast<int>(cmdArg);
					if (picostation::g_fileCmdSeq.Load() == cmdSeq)
					{
						needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
					}
					break;
				}
				
				case picostation::FileListingStates::MOUNT_FILE:
				{
					//printf("Processing MOUNT_FILE\n");
					picostation::cancelSubqAlarm();
					mechCommand.cancelSoct();
					g_picoAudio.deactivate();
					s_dataLocation = picostation::DiscImage::DataLocation::SDCard;
					char filePath[c_maxFilePathLength + 1];
					loadedImageIndex = cmdArg;
					img_count = DirectoryListing::getDirectoryEntriesCount();

					const bool gotPath = picostation::DirectoryListing::getPath(loadedImageIndex, filePath);
					const bool mounted = gotPath && (FR_OK == g_discImage.load(filePath));

					needFileCheckAction = picostation::FileListingStates::IDLE;
					picostation::g_readyDataKind = picostation::MenuDataKind::NONE;

					if (!mounted)
					{
						DEBUG_PRINT("mount failed: %s\n", gotPath ? filePath : "(invalid path)");

						// Stay in the menu instead of pretending a broken image
						// was mounted and then hanging on the first sector read.
						picostation::cancelSubqAlarm();
						mechCommand.cancelSoct();
						g_discImage.unload();
						g_discImage.makeDummyCue();
						s_dataLocation = picostation::DiscImage::DataLocation::RAM;
						menu_active = true;
						s_doorPending = false;
						reinitI2S();
						lastSector = -1;
						g_driveMechanics.resetDrive();
						g_discImage.set_skip_bootsector(false);
						mechCommand.resetBootSectorPattern();
						modChip.reset();
						continue;
					}

					//printf("image cue name:%s\n", filePath);
					picostation::cancelSubqAlarm();
					mechCommand.cancelSoct();
					g_targetPlaybackSpeed = 1;
					g_audioCtrlMode = audioControlModes::NORMAL;
					g_audioMuted = false;
					g_audioAttenuated = false;
					g_discImage.resetAudioPeak();
					menu_active = false;
					s_doorPending = false;
					reinitI2S();
					lastSector = -1;
					g_driveMechanics.resetDrive();
					// Reset per-game flags to defaults before the menu's post-mount
					// EXTENDED commands (case 9) apply any user overrides.
					g_discImage.set_skip_bootsector(false);
					g_discImage.set_skip_edc(false);
					mechCommand.resetBootSectorPattern();
					modChip.reset();

					continue;
					break;
				}
				
				case picostation::FileListingStates::PROCESS_FILES:
				{
					if (!listReadyState.Load())
					{
						picostation::DirectoryListing::getDirectoryEntries(g_entryOffset.Load());
						if (picostation::g_fileCmdSeq.Load() == cmdSeq)
						{
							picostation::g_readyDataKind = picostation::MenuDataKind::LISTING;
							__dmb();
							listReadyState = 1;
						}
					}
					break;
				}
				
				case picostation::FileListingStates::GET_COVER:
				{
					if (!listReadyState.Load())
					{
						picostation::DirectoryListing::openCover(cmdArg);
						if (picostation::g_fileCmdSeq.Load() == cmdSeq)
						{
							picostation::g_readyDataKind = picostation::MenuDataKind::COVER;
							needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
							__dmb();
							listReadyState = 1;
						}
					}
					break;
				}
				
				case picostation::FileListingStates::GET_CFG:
				{
					if (!listReadyState.Load())
					{
						picostation::DirectoryListing::openCfg();
						if (picostation::g_fileCmdSeq.Load() == cmdSeq)
						{
							picostation::g_readyDataKind = picostation::MenuDataKind::CFG;
							needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
							__dmb();
							listReadyState = 1;
						}
					}
					break;
				}

				case picostation::FileListingStates::GET_BG:
				{
					if (!listReadyState.Load())
					{
						picostation::DirectoryListing::openBg();
						if (picostation::g_fileCmdSeq.Load() == cmdSeq)
						{
							picostation::g_readyDataKind = picostation::MenuDataKind::BG;
							needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
							__dmb();
							listReadyState = 1;
						}
					}
					break;
				}

				case picostation::FileListingStates::GET_CHEAT:
				{
					if (!listReadyState.Load())
					{
						picostation::DirectoryListing::openCheat(cmdArg);
						if (picostation::g_fileCmdSeq.Load() == cmdSeq)
						{
							picostation::g_readyDataKind = picostation::MenuDataKind::CHEAT;
							needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
							__dmb();
							listReadyState = 1;
						}
					}
					break;
				}

				case picostation::FileListingStates::PP_SCAN_SD:
				{
					if (!listReadyState.Load())
					{
						bufferForDMA &= 1u;
						bufferForSDRead = bufferForDMA ^ 1u;
						g_picoAudio.setScratchArena(&pioSamples[2][0], (CACHED_SECS - 2u) * sizeof(pioSamples[0]));
						g_picoAudio.scanFolder();
						if (picostation::g_fileCmdSeq.Load() == cmdSeq)
						{
							picostation::g_readyDataKind = picostation::MenuDataKind::PP_META;
							needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
							__dmb();
							listReadyState = 1;
						}
					}
					break;
				}

				case picostation::FileListingStates::PP_GET_ART:
				{
					if (!listReadyState.Load())
					{
						g_picoAudio.openArt(static_cast<uint16_t>(cmdArg & 0x00FFu));
						if (picostation::g_fileCmdSeq.Load() == cmdSeq)
						{
							picostation::g_readyDataKind = picostation::MenuDataKind::PP_ART;
							needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
							__dmb();
							listReadyState = 1;
						}
					}
					break;
				}

				case picostation::FileListingStates::PP_MOUNT_SD:
				{
					picostation::cancelSubqAlarm();
					mechCommand.cancelSoct();
					needFileCheckAction = picostation::FileListingStates::IDLE;
					picostation::g_readyDataKind = picostation::MenuDataKind::NONE;
					listReadyState = 0;

					// Ping-pong DMA uses only pioSamples[0..1] during PicoPlayer; ArenaState
					// (tracks + FatFS + PlaybackBuffers + Helix) already lives in pioSamples[2..31]!
					reinitI2S();
					bufferForDMA = 0;
					bufferForSDRead = 1;

					if (g_picoAudio.activate())
					{
						c_sectorMax = static_cast<int>(g_picoAudio.getTotalSectors()) + c_leadIn + c_preGap + 150;
						g_targetPlaybackSpeed = 1;
						g_audioCtrlMode = audioControlModes::NORMAL;
						g_audioMuted = false;
						g_audioAttenuated = false;
						g_discImage.resetAudioPeak();
						menu_active = false;
						s_doorPending = false;
						lastSector = -1;
						g_driveMechanics.resetDrive();
						mechCommand.resetBootSectorPattern();
						modChip.reset();
						continue;
					}
					break;
				}
				
				default:
					break;
			}
		}
		else if (s_doorPending && !menu_active && !g_picoAudio.isActive())
		{
			s_doorPending = false;

			// Valid indices are 0..img_count-1.
			if (img_count == 0 || ++loadedImageIndex >= img_count)
			{
				loadedImageIndex = 0;
			}
			
			char filePath[c_maxFilePathLength + 1];
			s_dataLocation = picostation::DiscImage::DataLocation::SDCard;
			const bool gotPath = picostation::DirectoryListing::getPath(loadedImageIndex, filePath);
			const bool mounted = gotPath && (FR_OK == g_discImage.load(filePath));

			if (!mounted)
			{
				DEBUG_PRINT("disc change mount failed\n");

				picostation::cancelSubqAlarm();
				mechCommand.cancelSoct();
				g_discImage.unload();
				g_discImage.makeDummyCue();
				s_dataLocation = picostation::DiscImage::DataLocation::RAM;
				menu_active = true;
				needFileCheckAction = picostation::FileListingStates::IDLE;
				reinitI2S();
				g_driveMechanics.resetDrive();
				g_discImage.set_skip_bootsector(false);
				mechCommand.resetBootSectorPattern();
				modChip.reset();
				continue;
			}
			
			picostation::cancelSubqAlarm();
			mechCommand.cancelSoct();
			reinitI2S();
			g_driveMechanics.resetDrive();
			g_discImage.set_skip_bootsector(false);
			mechCommand.resetBootSectorPattern();
			modChip.reset();
		}
		
        // Data sent via DMA, load the next sector (never hammer SD while slewing)
        if (currentSector != lastSector && currentSector >= 4503 && currentSector < c_sectorMax &&
            g_driveMechanics.isSledStopped())
        {
			if (!menu_active && !g_picoAudio.isActive())
			{
				for (int i = 0; i < CACHED_SECS; i++)
				{
					if (loadedSector[i] == currentSector)
					{
						// already in cache
						bufferForDMA = i;
						lastSector = currentSector;
#if DEBUG_I2S0
						DEBUG_PRINT("sector %d in cache\n", currentSector);
#endif
						goto continue_transfer;
					}
				}
			}
			
			{
				const uint8_t ringMask = (menu_active || g_picoAudio.isActive()) ? 1u : (CACHED_SECS - 1u);
				bufferForDMA &= ringMask;
				bufferForSDRead &= ringMask;
				while (bufferForSDRead == bufferForDMA)
				{
					++bufferForSDRead &= ringMask;
				}
			}
			
			if (menu_active && needFileCheckAction.Load() == picostation::FileListingStates::PROCESS_FILES && listReadyState.Load())
			{
				const auto readyKind = picostation::g_readyDataKind.Load();
				if (currentSector == 4750 && readyKind == picostation::MenuDataKind::LISTING)
				{
					g_discImage.buildSector(currentSector - c_leadIn, pioSamples[bufferForSDRead], picostation::DirectoryListing::getFileListingData(), cdScramblingLUT);
				}
				else if (currentSector == 4800 && readyKind == picostation::MenuDataKind::CFG)
				{
					g_discImage.buildSector(currentSector - c_leadIn, pioSamples[bufferForSDRead], picostation::DirectoryListing::readCfg(), cdScramblingLUT);
				}
				else if (currentSector >= 4850 && currentSector < 4858 && readyKind == picostation::MenuDataKind::COVER)
				{
					g_discImage.buildSector(currentSector - c_leadIn, pioSamples[bufferForSDRead], picostation::DirectoryListing::readCover(currentSector - 4850), cdScramblingLUT);
				}
				else if (currentSector >= 4900 && currentSector < 4903 && readyKind == picostation::MenuDataKind::CHEAT)
				{
					g_discImage.buildSector(currentSector - c_leadIn, pioSamples[bufferForSDRead], picostation::DirectoryListing::readCheat(currentSector - 4900), cdScramblingLUT);
				}
				else if (currentSector >= 4950 && currentSector < 4984 && readyKind == picostation::MenuDataKind::BG)
				{
					g_discImage.buildSector(currentSector - c_leadIn, pioSamples[bufferForSDRead], picostation::DirectoryListing::readBg(currentSector - 4950), cdScramblingLUT);
				}
				else if (currentSector >= 5000 && currentSector < 5010 && readyKind == picostation::MenuDataKind::PP_META)
				{
					g_discImage.buildSector(currentSector - c_leadIn, pioSamples[bufferForSDRead], g_picoAudio.getMetaBuffer(currentSector - 5000), cdScramblingLUT);
				}
				else if (currentSector >= 5010 && currentSector < 5018 && readyKind == picostation::MenuDataKind::PP_ART)
				{
					g_discImage.buildSector(currentSector - c_leadIn, pioSamples[bufferForSDRead], g_picoAudio.readArtPart(currentSector - 5010), cdScramblingLUT);
				}
				else
				{
					goto not_menu_request;
				}
			}
			else
			{
not_menu_request:
#if DEBUG_I2S
				startTime = time_us_64();
#endif
				// Load the next sector
				m_sdReadInProgress = true;
				__dmb();
				if (g_picoAudio.isActive())
				{
					g_picoAudio.readSector(pioSamples[bufferForSDRead], currentSector - c_leadIn);
				}
				else
				{
					g_discImage.readSector(pioSamples[bufferForSDRead], currentSector - c_leadIn, s_dataLocation.Load(), cdScramblingLUT);
				}
				__dmb();
				m_sdReadInProgress = false;
#if DEBUG_I2S
				endTime = time_us_64()-startTime;
				
				if (endTime > 5000)
				{
					DEBUG_PRINT("read time: %lluus (%d)\n", endTime, currentSector);
				}
#endif
			}
			
			loadedSector[bufferForSDRead] = currentSector;
			bufferForDMA = bufferForSDRead;
			lastSector = currentSector;
		}

continue_transfer:

        // Start the next transfer if the DMA channel is not busy
        if (!dma_channel_is_busy(dmaChannel) && i2s_state)
        {
			if (currentSector >= 4503 && currentSector < c_sectorMax)
			{
				// Publish timestamp before sector ID so Core 0 never pairs the
				// new sector with the previous sector's timestamp.
				m_lastSectorTime = time_us_64();
				m_sectorSending = loadedSector[bufferForDMA];

				dma_hw->ch[dmaChannel].read_addr = (uint32_t)pioSamples[bufferForDMA];

				// Sync with the I2S clock
				while (gpio_get(Pin::LRCK) == 1)
				{
					tight_loop_contents();
				}
				
				while (gpio_get(Pin::LRCK) == 0)
				{
					tight_loop_contents();
				}

				dma_channel_start(dmaChannel);
			}
			else if(picostation::g_subqDelay == false)
			{
				m_lastSectorTime = time_us_64();
				m_sectorSending = currentSector;
			}
        }
    }
    __builtin_unreachable();
}
