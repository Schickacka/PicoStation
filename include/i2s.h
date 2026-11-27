#pragma once

#include <stdint.h>

#include <array>

#include "pseudo_atomics.h"
#include "hardware/dma.h"
#include "ff.h"
#include "disc_image.h"

#define CACHED_SECS		32 /* Only 2, 4, 8, 16, 32 — preserved intact for PS1 games! */

namespace picostation {
class MechCommand;

class I2S {
  public:
    I2S() {};
    int dmaChannel;
    volatile bool menu_active;
    volatile bool s_doorPending;
    
    void i2s_set_state(uint8_t state) { i2s_state = state; }
    int getSectorSending() { return m_sectorSending.Load(); }
    uint64_t getLastSectorTime() { return m_lastSectorTime.Load(); }
    bool isSdReadInProgress() const { return m_sdReadInProgress; }
	void reinitI2S() {
		for (int i = 0; i < CACHED_SECS; i++) {
			loadedSector[i] = -2;
		}
		lastSector = -1;
		i2s_state = 0;
		m_sectorSending = -1;
		m_lastSectorTime = 0;
	}

    [[noreturn]] void start(MechCommand &mechCommand);
	
  private:
    int initDMA(const volatile void *read_addr, unsigned int transfer_count);  // Returns DMA channel number
    void mountSDCard();
	
	int loadedSector[CACHED_SECS];
	int lastSector;
	volatile uint8_t i2s_state = 0;
	volatile bool m_sdReadInProgress = false;
	
    pseudoatomic<int> m_sectorSending;
    pseudoatomic<uint64_t> m_lastSectorTime;
};
}  // namespace picostation

extern pseudoatomic<picostation::DiscImage::DataLocation> s_dataLocation;
