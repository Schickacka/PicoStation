#pragma once

#include <stdint.h>

#include "../third_party/cueparser/cueparser.h"
#include "../third_party/cueparser/disc.h"
#include "../third_party/cueparser/scheduler.h"
#include "../third_party/posix_file.h"
#include "ff.h"
#include "subq.h"

namespace picostation {
class DiscImage {
  public:
    DiscImage() {};
    ~DiscImage() {};

    enum DataLocation {
        RAM,
        SDCard
    };

    void buildSector(const int sector, uint32_t *buffer, uint16_t *userData, const uint16_t *scramling, bool pregap = false);
    FRESULT load(const TCHAR *targetCue);
    void unload();
    SubQ::Data generateSubQ(const int sector);
    bool isSbiSector(const int sector) const;
    bool hasData() { return m_hasData; };
    void makeDummyCue();
    void readSector(void *buffer, const int sector, DataLocation location, const uint16_t *scramling);
    void readSectorRAM(void *buffer, const int sector, const uint16_t *scramling);
    void readSectorSD(void *buffer, const int sector, const uint16_t *scramling);
    void set_skip_bootsector(bool skip) { skip_bootsector =  skip; }
	void set_skip_edc(bool skip) { skip_edc =  skip; }
    void resetAudioPeak();

  private:
    static constexpr size_t c_maxSbiEntries = 64;

    void loadSbi(const TCHAR *targetCue);
    void updateAudioPeak(const uint32_t *i2sWords);

    CueDisc m_cueDisc;
    uint32_t m_sbiSectors[c_maxSbiEntries] = {0};
    volatile size_t m_sbiCount = 0;
    volatile uint32_t m_audioPeakLR = 0;   // low16 = Left, high16 = Right (single atomic 32-bit word)
    volatile uint16_t m_audioPeakMax = 0;
    uint8_t m_audioLevelSide = 0;
    volatile bool m_hasData = false;
    volatile bool m_discLoading = false;
    volatile bool skip_bootsector = false;
    volatile bool skip_edc = false;
};

extern DiscImage g_discImage;
}  // namespace picostation
