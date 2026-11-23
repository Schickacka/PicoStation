#pragma once

#include <algorithm>
#include <stdint.h>

#include "hardware/sync.h"
#include "pseudo_atomics.h"
#include "values.h"

namespace picostation {
class MechCommand;

class DriveMechanics {
  public:
    void moveToNextSector();
    void setSector(uint32_t step, bool rev);
    int getSector() { return m_sector; }
    void moveSled(MechCommand &mechCommand);
    bool servo_valid();
    void startSled(bool rev, bool isBrake);
    void stopSled();
    uint32_t get_track_count() const { return cur_track_counter; }
	
    void resetDrive()
    {
      const uint32_t irq = save_and_disable_interrupts();
      sled_work = false;
      m_sledRev = false;
      m_sledBrake = false;
      m_sector = 0;
      cur_track_counter = 0;
      cur_zone = 0;
      skip_subq = 0;
      __dmb();
      restore_interrupts(irq);
    }

    bool isSledStopped() const { return !sled_work; }
    bool isSledBrake() const { return m_sledBrake; }
    void setMaxTrackMoveTime(uint32_t us) { c_MaxTrackMoveTime = us; }
    
    uint32_t req_skip_subq() { return skip_subq; }
    void clear_skip_subq() { skip_subq = 0; }
    
  private:
    volatile uint32_t cur_track_counter = 0;
    volatile uint64_t m_sledTimer = 0;
    volatile uint32_t m_sector = 0;
    volatile uint8_t cur_zone = 0;
#if defined(PICOSTATION_OVERCLOCK)
    volatile uint32_t c_MaxTrackMoveTime = 15;
#else
    volatile uint32_t c_MaxTrackMoveTime = 29;
#endif
    volatile bool sled_work = false;
    volatile bool m_sledRev = false;
    volatile bool m_sledBrake = false;
    volatile uint32_t skip_subq = 0;
};

extern DriveMechanics g_driveMechanics;
}  // namespace picostation

