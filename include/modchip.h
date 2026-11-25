#pragma once

#include <stdint.h>

#include "cmd.h"

namespace picostation {
class MechCommand;

class ModChip {
  public:
    void init();
    void reset();
    static void requestReset() { s_resetRequested = true; }
    void sendLicenseString(const int sector, MechCommand &mechCommand);
    static void setBitTimeUs(uint32_t bitTimeUs) { s_bitTimeUs = bitTimeUs; }

  private:
    enum class State : uint8_t {
        IDLE,
        SENDING_STRING,
        INTER_STRING_GAP
    };

    void startString(const int index, const uint32_t bitTimeUs);
    void forceIdleLow();
    void endLicenseSequence();
    static volatile uint32_t s_bitTimeUs;
    static volatile bool s_resetRequested;
    State m_state = State::IDLE;
    int m_hysteresis = 0;
    int m_stringIndex = 0;
    uint m_pioOffset = 0;
    bool m_pioReady = false;
    uint64_t m_modchipTimer = 0;
    uint64_t m_nextDeadlineUs = 0;
};
}  // namespace picostation