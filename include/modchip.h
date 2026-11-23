#pragma once

#include <stdint.h>

#include "cmd.h"

namespace picostation {
class MechCommand;

class ModChip {
  public:
    void init();
    void reset();
    void sendLicenseString(const int sector, MechCommand &mechCommand);
    static void setBitTimeUs(uint32_t bitTimeUs) { s_bitTimeUs = bitTimeUs; }

  private:
    enum class State : uint8_t {
        IDLE,
        SENDING_BITS,
        INTER_STRING_GAP
    };

    void endLicenseSequence();
    static volatile uint32_t s_bitTimeUs;
    State m_state = State::IDLE;
    int m_hysteresis = 0;
    int m_stringIndex = 0;
    int m_bitIndex = 0;
    uint64_t m_modchipTimer = 0;
    uint64_t m_nextDeadlineUs = 0;
};
}  // namespace picostation