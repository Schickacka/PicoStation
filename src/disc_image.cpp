#include "disc_image.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "hardware/sync.h"
#include "logging.h"
#include "pico_audio.h"
#include "picostation.h"
#include "subq.h"
#include "third_party/posix_file.h"
#include "values.h"
#include "global.h"
#include "edc.h"

#if DEBUG_CUE
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

extern const uint8_t  loaderImage[];
extern const uint32_t loaderImageSize;

// The official RC5 firmware ships the 0xbeef placeholder. The real CRC is
// implemented below but kept opt-in until it has hardware validation, so a
// normal source build stays behavior-compatible with the official release.
#ifndef PICOSTATION_SUBQ_CRC
#define PICOSTATION_SUBQ_CRC 0
#endif

struct MSF
{
    int mm;
    int ss;
    int ff;
};

namespace Submode
{
	enum : uint8_t
	{
		EndOfRecord = 1 << 0,
		Video = 1 << 1,
		Audio = 1 << 2,
		Data = 1 << 3,
		Trigger = 1 << 4,
		Form = 1 << 5,
		RealTime = 1 << 6,
		EndOfFile = 1 << 7,
	};
}

picostation::DiscImage picostation::g_discImage;

static constexpr uint16_t crc16_lut[256] =
{
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, 0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad,
    0xe1ce, 0xf1ef, 0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6, 0x9339, 0x8318, 0xb37b, 0xa35a,
    0xd3bd, 0xc39c, 0xf3ff, 0xe3de, 0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485, 0xa56a, 0xb54b,
    0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d, 0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc, 0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861,
    0x2802, 0x3823, 0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b, 0x5af5, 0x4ad4, 0x7ab7, 0x6a96,
    0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a, 0x6ca6, 0x7c87,
    0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70, 0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a,
    0x9f59, 0x8f78, 0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f, 0x1080, 0x00a1, 0x30c2, 0x20e3,
    0x5004, 0x4025, 0x7046, 0x6067, 0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e, 0x02b1, 0x1290,
    0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256, 0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e,
    0xc71d, 0xd73c, 0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634, 0xd94c, 0xc96d, 0xf90e, 0xe92f,
    0x99c8, 0x89e9, 0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3, 0xcb7d, 0xdb5c,
    0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a, 0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83,
    0x1ce0, 0x0cc1, 0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8, 0x6e17, 0x7e36, 0x4e55, 0x5e74,
    0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

static uint16_t s_userData[c_cdSamplesBytes/2] = {0};

static MSF __time_critical_func(sectorToMSF)(const int sector)
{
    MSF msf;
    msf.mm = abs(sector / 75 / 60);
    msf.ss = abs((sector / 75) % 60);
    msf.ff = abs(sector % 75);
    return msf;
}

static inline int toBCD(const int in)
{
    if (in < 0)
    {
        return 0x00;
    }
    if (in > 99)
    {
        return 0x99;
    }
    else
    {
        return (in / 10) << 4 | (in % 10);
    }
}

static inline bool fromBCD(const uint8_t bcd, uint32_t &out)
{
    const uint8_t hi = bcd >> 4;
    const uint8_t lo = bcd & 0x0Fu;
    if (hi > 9 || lo > 9)
    {
        return false;
    }
    out = static_cast<uint32_t>(hi * 10u + lo);
    return true;
}

static void __time_critical_func(getParentPath)(const TCHAR *path, TCHAR *parentPath, size_t parentPathSize)
{
    if (parentPathSize == 0)
    {
        return;
    }

    strncpy(parentPath, path, parentPathSize - 1);
    parentPath[parentPathSize - 1] = 0;

    char *lastSlash = strrchr(parentPath, '/');
    char *lastBackslash = strrchr(parentPath, '\\');
    
    if (lastBackslash && (!lastSlash || lastBackslash > lastSlash))
    {
        lastSlash = lastBackslash;
    }
    
    if (lastSlash)
    {
        *lastSlash = 0;
    }
    else
    {
        parentPath[0] = 0;
    }
}

void __time_critical_func(picostation::DiscImage::buildSector)(const int sector, uint32_t *buffer, uint16_t *userData, const uint16_t *scramling, bool pregap)
{
	uint32_t tmp;
	alignas(2) uint8_t header[24] =
	{
		// Sync - 12 bytes
		0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
		// Header - 4 bytes
		0, 0, 0, 2,
		// Sub-Header - 8 bytes (Green book)
		0, 0, 0, 0, 0, 0, 0, 0
	};
	
	const MSF msf = sectorToMSF(sector);
	
	header[12] = toBCD(msf.mm);
	header[13] = toBCD(msf.ss);
	header[14] = toBCD(msf.ff);
	
	if (pregap)
	{
		header[18] = header[22] = (Submode::Data);
	}
	else
	{
		header[18] = (Submode::Form);
		header[22] = 0;
	}
	
	uint16_t *src = (uint16_t *) header;
	
	for (int i = 0; i < 12; i++)
	{
		tmp = (src[i] ^ scramling[i]) << 8;
		
		if (tmp & 0x100)
		{
			tmp |= 0xFF;
		}
		
		buffer[i] = tmp;
	}
    
	for (int i = 12; i < 1174; i++)
	{
		tmp = (((userData) ? *userData++ : 0) ^ scramling[i]) << 8;
		
		if (tmp & 0x100)
		{
			tmp |= 0xFF;
		}
		
		buffer[i] = tmp;
	}

    // EDC/ECC - 4 bytes
    tmp = scramling[1174] << 8;
    if (tmp & 0x100)
    {
		tmp |= 0xFF;
	}
    buffer[1174] = tmp;
    
    tmp = scramling[1175] << 8;
    if (tmp & 0x100)
    {
		tmp |= 0xFF;
	}
	buffer[1175] = tmp;
}

picostation::SubQ::Data __time_critical_func(picostation::DiscImage::generateSubQ)(const int sector)
{
    SubQ::Data subqdata{};

    if (g_picoAudio.isActive())
    {
        subqdata = g_picoAudio.generateSubQ(sector - c_leadIn);
    }
    else if (m_discLoading)
    {
        return subqdata;
    }
    else if (m_cueDisc.trackCount >= 1)
    {
        int sector_track;
        if (sector < c_leadIn)  // Lead-in area
        {
            // Red Book / Sony PlayStation standard Lead-In TOC order:
            // Emit A0 (first track + CDROM-XA flag 0x20), A1 (last track), A2 (lead-out)
            // FIRST in every cycle, followed by Tracks 1..N (each repeated 3 times).
            // On 57-track discs like Tomb Raider this delivers A0/A1/A2 within 120 ms
            // instead of 2.4 seconds, preventing HC05 ReadTOC/SetSession timeouts!
            const int tocTotal = 3 + m_cueDisc.trackCount;
            const int safeSector = (sector >= 0) ? sector : 0;
            const int tocIdx = (safeSector / 3) % tocTotal;

            if (tocIdx == 0)  // A0 - Report first track number & disc type
            {
                subqdata.ctrladdr = m_cueDisc.tracks[1].trackType == CueTrackType::TRACK_TYPE_DATA ? 0x41 : 0x01;
                subqdata.tno = 0x00;
                subqdata.point = 0xA0;
                subqdata.pmin = 0x01;
                subqdata.psec = m_hasData ? 0x20 : 0x00;  // 0 = audio, 20 = CDROM-XA
                subqdata.pframe = 0x00;
            }
            else if (tocIdx == 1)  // A1 - Report last track number
            {
                // Thanks rama! )
                subqdata.ctrladdr = m_cueDisc.tracks[m_cueDisc.trackCount].trackType == CueTrackType::TRACK_TYPE_DATA ? 0x41 : 0x01;
                subqdata.tno = 0x00;
                subqdata.point = 0xA1;
                subqdata.pmin = toBCD(m_cueDisc.trackCount);
                subqdata.psec = 0x00;
                subqdata.pframe = 0x00;
            }
            else if (tocIdx == 2)  // A2 - Report lead-out track location
            {
                // <3
                const int sector_lead_out = m_cueDisc.tracks[m_cueDisc.trackCount + 1].indices[1] + c_preGap;
                const MSF msf_lead_out = sectorToMSF(sector_lead_out);
                subqdata.ctrladdr = m_cueDisc.tracks[m_cueDisc.trackCount].trackType == CueTrackType::TRACK_TYPE_DATA ? 0x41 : 0x01;
                subqdata.tno = 0x00;
                subqdata.point = 0xA2;
                subqdata.pmin = toBCD(msf_lead_out.mm);
                subqdata.psec = toBCD(msf_lead_out.ss);
                subqdata.pframe = toBCD(msf_lead_out.ff);
            }
            else  // TOC Track Entries (logical_track = 1 .. trackCount)
            {
                const int logical_track = tocIdx - 2;
                if (logical_track == 1)
                {
                    // Track 1 has a hardcoded 2 second pre-gap
                    sector_track = c_preGap;
                }
                else
                {
                    // Offset each track by track 1's pre-gap
                    sector_track = m_cueDisc.tracks[logical_track].indices[1] + c_preGap;
                }

                const MSF msf_track = sectorToMSF(sector_track);

                subqdata.ctrladdr =
                    (m_cueDisc.tracks[logical_track].trackType == CueTrackType::TRACK_TYPE_DATA) ? 0x41 : 0x01;
                subqdata.tno = 0x00;
                subqdata.x = toBCD(logical_track);
                subqdata.pmin = toBCD(msf_track.mm);
                subqdata.psec = toBCD(msf_track.ss);
                subqdata.pframe = toBCD(msf_track.ff);
            }

            const MSF msf_sector = sectorToMSF(sector);
            subqdata.min = toBCD(msf_sector.mm);
            subqdata.sec = toBCD(msf_sector.ss);
            subqdata.frame = toBCD(msf_sector.ff);
            subqdata.zero = 0x00;
        } 
        else  // Program area + lead-out
        {
            int currentLogicalTrack = m_cueDisc.trackCount + 1;  // in case seek overshoots past end of disc

            if (sector - c_leadIn < c_preGap)
            {
                currentLogicalTrack = 1;
            } 
            else
            {
                // Compare against the start of the next track. The lead-out track
                // (trackCount + 1) is the last valid entry, so stop there instead
                // of reading past the end of the track array.
                for (size_t i = 1; i <= static_cast<size_t>(m_cueDisc.trackCount); i++)
                {
                    if (m_cueDisc.tracks[i + 1].indices[0] > static_cast<uint32_t>(sector - c_leadIn - c_preGap))
                    {
                        currentLogicalTrack = static_cast<int>(i);
                        break;
                    }
                }
            }
            // Keep this arithmetic signed: during a track pregap sector_track is
            // negative, and mixing it with the uint32_t cue indices would rely on
            // unsigned wraparound.
            sector_track = sector - static_cast<int>(m_cueDisc.tracks[currentLogicalTrack].indices[1]) -
                           c_leadIn - c_preGap;
            const MSF msf_track = sectorToMSF(sector_track);

            // Align absolute MSF with the physical CD header (sector - c_leadIn)
            // so A - R == 150 (00:02:00) and LibCrypt .sbi sectors match GetlocP.
            const int sector_abs = sector - c_leadIn;
            const MSF msf_abs = sectorToMSF(sector_abs);

            const bool isLeadOut = (currentLogicalTrack == m_cueDisc.trackCount + 1);
            const int ctrlTrack = isLeadOut ? m_cueDisc.trackCount : currentLogicalTrack;
            subqdata.ctrladdr = (m_cueDisc.tracks[ctrlTrack].trackType == CueTrackType::TRACK_TYPE_DATA) ? 0x41 : 0x01;

            if (isLeadOut)
            {
                subqdata.tno = 0xAA;  // Lead-out track
            } 
            else
            {
                subqdata.tno = toBCD(currentLogicalTrack);  // Track numbers
            }

            if (sector_track < 0)					   // 2 sec pause track
            {
                subqdata.x = 0x00;                     // Pause encoding
                subqdata.min = toBCD(msf_track.mm);    // min
                subqdata.sec = toBCD(msf_track.ss);    // sec (count down)
                subqdata.frame = toBCD(msf_track.ff);  // frame (count down)
            } 
            else
            {
                subqdata.x = 0x01;
                subqdata.min = toBCD(msf_track.mm);
                subqdata.sec = toBCD(msf_track.ss);
                subqdata.frame = toBCD(msf_track.ff);
            }
            subqdata.zero = 0x00;
            subqdata.amin = toBCD(msf_abs.mm);
            subqdata.asec = toBCD(msf_abs.ss);
            subqdata.aframe = toBCD(msf_abs.ff);
        }
    }

    const unsigned int audioMode = g_audioCtrlMode;
    if (audioMode == audioControlModes::LEVELMETER)
    {
        // Sony CXD2510Q/CXD2545Q Level Meter (PCT=1):
        // bits 0..14 = 15-bit unsigned absolute PCM peak (LSB-first),
        // bit 15 = L/R channel flag (1 = Left, 0 = Right), toggled per read.
        const uint8_t side = m_audioLevelSide;
        m_audioLevelSide ^= 1u;
        __dmb();
        const uint32_t packed = m_audioPeakLR;
        uint16_t peak15 = g_audioMuted
                              ? 0u
                              : static_cast<uint16_t>(side ? (packed & 0xFFFFu) : (packed >> 16));
        if (g_audioAttenuated)
        {
            peak15 >>= 2;  // -12 dB attenuation
        }
        subqdata.crc = static_cast<uint16_t>((peak15 & 0x7FFFu) | (static_cast<uint16_t>(side) << 15));
    }
    else if (audioMode == audioControlModes::PEAKMETER)
    {
        // Sony CXD2510Q/CXD2545Q Peak Meter (PCT=2):
        // bits 0..14 = highest absolute PCM peak held across both channels, bit 15 = 0.
        __dmb();
        uint16_t peak15 = g_audioMuted ? 0u : (m_audioPeakMax & 0x7FFFu);
        if (g_audioAttenuated)
        {
            peak15 >>= 2;  // -12 dB attenuation
        }
        subqdata.crc = peak15;
    }
    else
    {
#if PICOSTATION_SUBQ_CRC
        // CRC-16/CCITT over the first 10 subcode bytes, with the PSX's final
        // inversion, transmitted MSB first.
        uint16_t crc = 0;
        for (size_t i = 0; i < 10; i++)
        {
            crc = (crc << 8) ^ crc16_lut[((crc >> 8) ^ subqdata.raw[i]) & 0xFF];
        }
        crc ^= 0xFFFF;

        subqdata.raw[10] = static_cast<uint8_t>(crc >> 8);
        subqdata.raw[11] = static_cast<uint8_t>(crc & 0xFF);
#else
        // Official RC5 behavior. Enable PICOSTATION_SUBQ_CRC=1 only for testing.
        subqdata.crc = 0xbeef;
#endif
    }

    return subqdata;
}

void __time_critical_func(picostation::DiscImage::resetAudioPeak)()
{
    m_audioPeakLR = 0;
    m_audioPeakMax = 0;
    m_audioLevelSide = 0;
    __dmb();
}

void __time_critical_func(picostation::DiscImage::updateAudioPeak)(const uint32_t *i2sWords)
{
    // CDDA sector = 588 stereo frames = 1176 16-bit signed PCM samples (L, R),
    // stored in I2S 32-bit words with the 16-bit sample in bits [23:8].
    uint16_t peakL = 0;
    uint16_t peakR = 0;

    for (size_t i = 0; i < c_cdSamplesSize * 2; i += 2)
    {
        const int32_t sL = static_cast<int16_t>(static_cast<uint16_t>(i2sWords[i] >> 8));
        const int32_t sR = static_cast<int16_t>(static_cast<uint16_t>(i2sWords[i + 1] >> 8));
        const uint32_t absL = (sL < 0) ? static_cast<uint32_t>(-sL) : static_cast<uint32_t>(sL);
        const uint32_t absR = (sR < 0) ? static_cast<uint32_t>(-sR) : static_cast<uint32_t>(sR);
        const uint16_t cL = (absL > 0x7FFFu) ? 0x7FFFu : static_cast<uint16_t>(absL);
        const uint16_t cR = (absR > 0x7FFFu) ? 0x7FFFu : static_cast<uint16_t>(absR);

        if (cL > peakL) peakL = cL;
        if (cR > peakR) peakR = cR;
    }

    m_audioPeakLR = static_cast<uint32_t>(peakL) | (static_cast<uint32_t>(peakR) << 16);

    const uint16_t sectorMax = (peakL > peakR) ? peakL : peakR;
    if (sectorMax > m_audioPeakMax)
    {
        m_audioPeakMax = sectorMax;
    }
    __dmb();
}

struct DirSizeEntry
{
    char *name;
    uint64_t size;
};

struct Context
{
    static constexpr size_t c_maxDirEntries = 128;
    TCHAR parentPath[c_maxFilePathLength + 1];
    DirSizeEntry dirCache[c_maxDirEntries];
    size_t dirCount = 0;
    size_t openedCount = 0;
    bool parseError = false;
};

static bool equalsIgnoreCaseAscii(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (tolower(static_cast<unsigned char>(*a)) != tolower(static_cast<unsigned char>(*b)))
        {
            return false;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static void buildDirSizeCache(Context &ctx)
{
    ctx.dirCount = 0;
    DIR dir;
    const TCHAR *dirPath = ctx.parentPath[0] ? ctx.parentPath : "/";
    if (f_opendir(&dir, dirPath) != FR_OK)
    {
        return;
    }

    FILINFO fno;
    while (ctx.dirCount < Context::c_maxDirEntries)
    {
        if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0)
        {
            break;
        }
        if (fno.fattrib & AM_DIR)
        {
            continue;
        }
        const size_t len = strlen(fno.fname) + 1u;
        char *copy = static_cast<char *>(malloc(len));
        if (!copy)
        {
            break;
        }
        memcpy(copy, fno.fname, len);
        ctx.dirCache[ctx.dirCount].name = copy;
        ctx.dirCache[ctx.dirCount].size = static_cast<uint64_t>(fno.fsize);
        ctx.dirCount++;
    }

    f_closedir(&dir);
}

static void freeDirSizeCache(Context &ctx)
{
    for (size_t i = 0; i < ctx.dirCount; i++)
    {
        free(ctx.dirCache[i].name);
        ctx.dirCache[i].name = NULL;
    }
    ctx.dirCount = 0;
}

static bool lookupDirSizeCache(const Context &ctx, const char *filename, uint64_t &outSize)
{
    for (size_t i = 0; i < ctx.dirCount; i++)
    {
        if (equalsIgnoreCaseAscii(ctx.dirCache[i].name, filename))
        {
            outSize = ctx.dirCache[i].size;
            return true;
        }
    }
    return false;
}

static void parser_cb(struct CueParser *parser, struct CueScheduler *scheduler, const char *error)
{
    if (error)
    {
        DEBUG_PRINT("parser error: %s\n", error);

        if (scheduler->opaque)
        {
            reinterpret_cast<Context *>(scheduler->opaque)->parseError = true;
        }
    }
}

static struct CueFile *__time_critical_func(fileopen)(struct CueFile *file, struct CueScheduler *scheduler, const char *filename)
{
    Context *context = reinterpret_cast<Context *>(scheduler->opaque);

    // A cue sheet can reference files in the same directory. Reserve enough
    // room for the longest possible parent path and file name; the previous
    // 256-byte buffer could overflow on long paths.
    TCHAR fullpath[(c_maxFilePathLength * 2) + 2];

    const int written = snprintf(fullpath, sizeof(fullpath), "%s%s%s",
                                 context->parentPath,
                                 context->parentPath[0] ? "/" : "",
                                 filename);

    if (written < 0 || static_cast<size_t>(written) >= sizeof(fullpath))
    {
        DEBUG_PRINT("cue file path too long: %s\n", filename);

        // The parser calls file->destroy() when open() fails, so make sure the
        // CueFile has a valid vtable even on this early error path.
        create_posix_file(file, "", FA_READ);
        return NULL;
    }

    // Track 1 (first FILE in .cue) is pinned open immediately for zero-latency
    // boot/data reads. All subsequent track files (Tracks 2..99, e.g. 57-track
    // Tomb Raider CDDA) use their single-pass directory fsize and open lazily on
    // first sector read — slashing MOUNT_FILE from seconds to ~15 ms!
    struct CueFile *res = NULL;
    uint64_t cachedSize = 0;
    if (context->openedCount > 0 && lookupDirSizeCache(*context, filename, cachedSize))
    {
        res = create_posix_file_lazy(file, fullpath, cachedSize);
    }
    else
    {
        res = create_posix_file(file, fullpath, FA_READ);
    }

    if (res)
    {
        context->openedCount++;
    }
    return res;
}

FRESULT __time_critical_func(picostation::DiscImage::load)(const TCHAR *targetCue)
{
    // To-do: Need alternate code paths here for parsing cue from alternate sources.
    if (!targetCue || !targetCue[0])
    {
        return FR_INVALID_NAME;
    }

    m_discLoading = true;
    __dmb();

    // Do not leave a previous image half-mounted if this one fails to parse.
    unload();

    memset(&m_cueDisc, 0, sizeof(m_cueDisc));

    struct CueScheduler scheduler;
    Scheduler_construct(&scheduler);
    Context context;
    getParentPath(targetCue, context.parentPath, sizeof(context.parentPath));
    buildDirSizeCache(context);
    scheduler.opaque = &context;

    struct CueFile cue;
    if (!create_posix_file(&cue, targetCue, FA_READ))
    {
        DEBUG_PRINT("create_posix_file failed for: %s.\n", targetCue);
        freeDirSizeCache(context);
        m_discLoading = false;
        __dmb();
        return FR_NO_FILE;
    }
    
    cue.cfilename = targetCue;

    struct CueParser parser;
    CueParser_construct(&parser, &m_cueDisc);
    CueParser_parse(&parser, &cue, &scheduler, fileopen, parser_cb);
    Scheduler_run(&scheduler);
    freeDirSizeCache(context);

    // Ownership of the track files now belongs to unload(). Do not call
    // CueParser_close() here: on a parser error it can close a file that is
    // still referenced by a track, leading to a double close later.
    if (context.parseError && parser.currentFile)
    {
        bool referenced = false;
        for (int i = 0; i <= m_cueDisc.trackCount && i < MAXTRACK; i++)
        {
            if (m_cueDisc.tracks[i].file == parser.currentFile)
            {
                referenced = true;
                break;
            }
        }

        if (!referenced)
        {
            if (parser.currentFile->opaque)
            {
                parser.currentFile->close(parser.currentFile, NULL, NULL);
            }
            free(parser.currentFile);
        }

        parser.currentFile = NULL;
    }

    cue.close(&cue, NULL, NULL);

    DEBUG_PRINT("Disc track count: %d\n", m_cueDisc.trackCount);

    const bool invalidCue = context.parseError ||
                            m_cueDisc.trackCount < 1 ||
                            m_cueDisc.trackCount > 99 ||
                            m_cueDisc.trackCount >= (MAXTRACK - 1);

    if (invalidCue)
    {
        DEBUG_PRINT("invalid cue (tracks=%d, error=%d)\n", m_cueDisc.trackCount, context.parseError);
        unload();
        m_discLoading = false;
        __dmb();
        return FR_INVALID_OBJECT;
    }

    for (int i = 1; i <= m_cueDisc.trackCount; i++)
    {
        if (!m_cueDisc.tracks[i].file || !m_cueDisc.tracks[i].file->opaque)
        {
            DEBUG_PRINT("track %d has no file\n", i);
            unload();
            m_discLoading = false;
            __dmb();
            return FR_INVALID_OBJECT;
        }

        // The third-party parser only validates indices for tracks before the
        // last one. A malformed last track would otherwise make generateSubQ()
        // read an unset index.
        if (m_cueDisc.tracks[i].indexCount < 1)
        {
            DEBUG_PRINT("track %d has no index\n", i);
            unload();
            m_discLoading = false;
            __dmb();
            return FR_INVALID_OBJECT;
        }

        if (m_cueDisc.tracks[i].indices[0] > m_cueDisc.tracks[i].indices[1] ||
            (i > 1 && m_cueDisc.tracks[i].indices[0] < m_cueDisc.tracks[i - 1].indices[1]))
        {
            DEBUG_PRINT("track %d has non-monotonic indices\n", i);
            unload();
            m_discLoading = false;
            __dmb();
            return FR_INVALID_OBJECT;
        }
    }

    // Lead-out: cueparser.c computes track->size relative to track->indices[0]
    // (including any pregap before INDEX 01), so indices[0] + size is the exact
    // end of the last track on both single-track and multi-track discs.
    const uint64_t leadOut64 = static_cast<uint64_t>(m_cueDisc.tracks[m_cueDisc.trackCount].indices[0]) +
                               m_cueDisc.tracks[m_cueDisc.trackCount].size;

    // A normal CD is at most 99:59:74 (449999 sectors) in 2-digit BCD MSF.
    // Reject corrupt cue sheets before they exceed BCD limits or inflate c_sectorMax.
    constexpr uint64_t c_maxLeadOut = (99ULL * 75ULL * 60ULL) + (59ULL * 75ULL) + 74ULL;
    if (leadOut64 == 0 || leadOut64 > c_maxLeadOut)
    {
        DEBUG_PRINT("invalid lead-out: %llu\n", static_cast<unsigned long long>(leadOut64));
        unload();
        m_discLoading = false;
        __dmb();
        return FR_INVALID_OBJECT;
    }

    m_cueDisc.tracks[m_cueDisc.trackCount + 1].file = NULL;
    m_cueDisc.tracks[m_cueDisc.trackCount + 1].trackType = m_cueDisc.tracks[m_cueDisc.trackCount].trackType;
    m_cueDisc.tracks[m_cueDisc.trackCount + 1].size = 0;
    m_cueDisc.tracks[m_cueDisc.trackCount + 1].fileOffset = static_cast<uint32_t>(leadOut64);
    m_cueDisc.tracks[m_cueDisc.trackCount + 1].indices[0] = static_cast<uint32_t>(leadOut64);
    m_cueDisc.tracks[m_cueDisc.trackCount + 1].indices[1] = static_cast<uint32_t>(leadOut64);

    m_hasData = false;
    DEBUG_PRINT("Track\tStart\tLength\tPregap\n");
    for (size_t i = 0; i <= static_cast<size_t>(m_cueDisc.trackCount + 1); i++)
    {
        if (m_cueDisc.tracks[i].trackType == CueTrackType::TRACK_TYPE_DATA)
        {
            m_hasData = true;
        }
        DEBUG_PRINT("%d\t%d\t%d\t%d\n", i, m_cueDisc.tracks[i].indices[0], m_cueDisc.tracks[i].size,
										   m_cueDisc.tracks[i].indices[1] - m_cueDisc.tracks[i].indices[0]);
    }
    
    c_sectorMax = static_cast<int>(leadOut64 + 4652);

    loadSbi(targetCue);

    __dmb();
    m_discLoading = false;
    __dmb();

    return FR_OK;
}

void __time_critical_func(picostation::DiscImage::loadSbi)(const TCHAR *targetCue)
{
    m_sbiCount = 0;
    __dmb();

    if (!targetCue || !targetCue[0])
    {
        return;
    }

    const size_t cueLen = strlen(targetCue);
    const char *lastSlash = strrchr(targetCue, '/');
    const char *lastBackslash = strrchr(targetCue, '\\');
    if (lastBackslash && (!lastSlash || lastBackslash > lastSlash))
    {
        lastSlash = lastBackslash;
    }

    const char *dot = strrchr(targetCue, '.');
    const size_t stemLen = (dot && (!lastSlash || dot > lastSlash))
                               ? static_cast<size_t>(dot - targetCue)
                               : cueLen;

    // stem + ".sbi" + NUL
    TCHAR sbiPath[c_maxFilePathLength + 5];
    if (stemLen + 4 >= sizeof(sbiPath))
    {
        return;
    }

    memcpy(sbiPath, targetCue, stemLen);
    memcpy(sbiPath + stemLen, ".sbi", 5);

    FIL sbiFile;
    size_t count = 0;
    UINT br = 0;

    if (FR_OK == f_open(&sbiFile, sbiPath, FA_READ))
    {
        uint8_t magic[4];
        if (FR_OK == f_read(&sbiFile, magic, sizeof(magic), &br) &&
            br == sizeof(magic) &&
            magic[0] == 'S' && magic[1] == 'B' && magic[2] == 'I' && magic[3] == 0)
        {
            uint8_t recHdr[4];
            uint8_t payload[10];

            while (FR_OK == f_read(&sbiFile, recHdr, sizeof(recHdr), &br) && br == sizeof(recHdr))
            {
                UINT payloadLen = 0;
                if (recHdr[3] == 1)
                {
                    payloadLen = 10;
                }
                else if (recHdr[3] == 2 || recHdr[3] == 3)
                {
                    payloadLen = 3;
                }
                else
                {
                    break;
                }

                if (FR_OK != f_read(&sbiFile, payload, payloadLen, &br) || br != payloadLen)
                {
                    break;
                }

                uint32_t mm = 0, ss = 0, ff = 0;
                if (!fromBCD(recHdr[0], mm) || !fromBCD(recHdr[1], ss) || !fromBCD(recHdr[2], ff) ||
                    ss >= 60u || ff >= 75u)
                {
                    continue;
                }

                // Redump LibCrypt .sbi entries are type 1 (invalid SubQ CRC -> CRCF = 0).
                if (recHdr[3] == 1 && count < c_maxSbiEntries)
                {
                    m_sbiSectors[count++] = (mm * 60u + ss) * 75u + ff;
                }
            }
        }

        f_close(&sbiFile);
    }
    else
    {
        // Fallback to PicoStation SD Manager / LibCrypt .lsd (15-byte entries:
        // 3-byte BCD MSF + 12-byte raw SubQ).
        memcpy(sbiPath + stemLen, ".lsd", 5);
        if (FR_OK != f_open(&sbiFile, sbiPath, FA_READ))
        {
            return;
        }

        uint8_t lsdRec[15];
        while (FR_OK == f_read(&sbiFile, lsdRec, sizeof(lsdRec), &br) && br == sizeof(lsdRec))
        {
            uint32_t mm = 0, ss = 0, ff = 0;
            if (!fromBCD(lsdRec[0], mm) || !fromBCD(lsdRec[1], ss) || !fromBCD(lsdRec[2], ff) ||
                ss >= 60u || ff >= 75u)
            {
                continue;
            }

            if (count < c_maxSbiEntries)
            {
                m_sbiSectors[count++] = (mm * 60u + ss) * 75u + ff;
            }
        }

        f_close(&sbiFile);
    }

    if (count == 0)
    {
        return;
    }

    // Insertion sort + deduplicate so isSbiSector() can binary-search in O(log N).
    for (size_t i = 1; i < count; i++)
    {
        const uint32_t key = m_sbiSectors[i];
        size_t j = i;
        while (j > 0 && m_sbiSectors[j - 1] > key)
        {
            m_sbiSectors[j] = m_sbiSectors[j - 1];
            j--;
        }
        m_sbiSectors[j] = key;
    }

    size_t uniqueCount = 0;
    for (size_t i = 0; i < count; i++)
    {
        if (uniqueCount == 0 || m_sbiSectors[i] != m_sbiSectors[uniqueCount - 1])
        {
            m_sbiSectors[uniqueCount++] = m_sbiSectors[i];
        }
    }

    __dmb();
    m_sbiCount = uniqueCount;
    DEBUG_PRINT("Loaded %u SBI sectors from %s\n", static_cast<unsigned>(uniqueCount), sbiPath);
}

bool __time_critical_func(picostation::DiscImage::isSbiSector)(const int sector) const
{
    const size_t count = m_sbiCount;
    if (count == 0 || sector < c_leadIn)
    {
        return false;
    }

    __dmb();
    const uint32_t target = static_cast<uint32_t>(sector - c_leadIn);
    size_t lo = 0;
    size_t hi = count;
    while (lo < hi)
    {
        const size_t mid = lo + ((hi - lo) >> 1);
        if (m_sbiSectors[mid] < target)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }

    return (lo < count) && (m_sbiSectors[lo] == target);
}

void __time_critical_func(picostation::DiscImage::unload)()
{
    DEBUG_PRINT("Close:\nTrack\tStart\tLength\tPregap\n");

    const int trackCount = m_cueDisc.trackCount;

    for (int i = 1; i <= trackCount && i < MAXTRACK; i++)
    {
        struct CueFile *file = m_cueDisc.tracks[i].file;

        if (!file)
        {
            continue;
        }

        // Several tracks can share one physical file. Close and free it only
        // once, no matter how the tracks are ordered.
        bool alreadyReleased = false;
        for (int j = 1; j < i; j++)
        {
            if (m_cueDisc.tracks[j].file == file)
            {
                alreadyReleased = true;
                break;
            }
        }

        if (alreadyReleased)
        {
            continue;
        }

        DEBUG_PRINT("%d\t%d\t%d\t%d\n", i, m_cueDisc.tracks[i].indices[0], m_cueDisc.tracks[i].size,
                                         m_cueDisc.tracks[i].indices[1] - m_cueDisc.tracks[i].indices[0]);

        if (file->opaque)
        {
            file->close(file, NULL, NULL);
        }

        free(file);
    }

    for (int i = 0; i < MAXTRACK; i++)
    {
        m_cueDisc.tracks[i].file = NULL;
    }

    m_cueDisc.trackCount = 0;
    m_sbiCount = 0;
    resetAudioPeak();
    __dmb();
    m_hasData = false;
}

void __time_critical_func(picostation::DiscImage::makeDummyCue)()
{
    // Create a dummy cue disc with a single data track, as well as lead-in and lead-out tracks.

    constexpr uint32_t c_sectorCount = (98 * 75 * 60) + (57 * 75) + 74;  // 98:57:74(mm:ss:ff) leaving room for 2 sec pre-gap and lead-in

    // Make sure no stale file handles from a previously loaded image survive.
    unload();

    memset(&m_cueDisc, 0, sizeof(m_cueDisc));

    m_cueDisc.trackCount = 1;

    // Lead-in track
    m_cueDisc.tracks[0].trackType = CueTrackType::TRACK_TYPE_UNKNOWN;
    m_cueDisc.tracks[0].indices[0] = 0;
    m_cueDisc.tracks[0].indices[1] = 0;
    m_cueDisc.tracks[0].size = 0;

    // Data track
    m_cueDisc.tracks[1].trackType = CueTrackType::TRACK_TYPE_DATA;
    m_cueDisc.tracks[1].indices[0] = 0;
    m_cueDisc.tracks[1].indices[1] = 0;
    m_cueDisc.tracks[1].size = c_sectorCount;

    // Lead-out track
    m_cueDisc.tracks[2].trackType = CueTrackType::TRACK_TYPE_DATA;
    m_cueDisc.tracks[2].fileOffset = m_cueDisc.tracks[1].indices[1] + m_cueDisc.tracks[1].size;
    m_cueDisc.tracks[2].indices[0] = m_cueDisc.tracks[2].fileOffset;
    m_cueDisc.tracks[2].indices[1] = m_cueDisc.tracks[2].indices[0];

    m_hasData = true;

    DEBUG_PRINT("Track\tStart\tLength\tPregap\n");
    for (size_t i = 0; i <= static_cast<size_t>(m_cueDisc.trackCount + 1); i++)
    {
        DEBUG_PRINT("%d\t%d\t%d\t%d\n", i, m_cueDisc.tracks[i].indices[0], m_cueDisc.tracks[i].size,
                    m_cueDisc.tracks[i].indices[1] - m_cueDisc.tracks[i].indices[0]);
    }
    
	c_sectorMax = 333000;  // 74:00:00
}

void __time_critical_func(picostation::DiscImage::readSector)(void *buffer, const int sector, DataLocation location, const uint16_t *scramling)
{
    switch (location)
    {
        case DataLocation::SDCard:
            readSectorSD(buffer, sector, scramling);
            break;
        
        case DataLocation::RAM:
            readSectorRAM(buffer, sector, scramling);
            break;
        
        default:
            break;
    }
}

void __time_critical_func(picostation::DiscImage::readSectorRAM)(void *buffer, const int sector, const uint16_t *scramling)
{
    const int adjustedSector = sector - c_preGap;

    // adjustedSector is signed. The old code multiplied it into a size_t,
    // which made the ">= 0" test meaningless for negative values.
    if (adjustedSector >= 0 && loaderImageSize >= c_cdSamplesBytes)
    {
        const size_t targetOffset = static_cast<size_t>(adjustedSector) * c_cdSamplesBytes;

        if (targetOffset <= loaderImageSize - c_cdSamplesBytes)
        {
            scramble_data((uint32_t *) buffer, (uint16_t *) &loaderImage[targetOffset], scramling, 1176);
            return;
        }
    }

    buildSector(sector, static_cast<uint32_t *>(buffer), NULL, scramling, false);
}

void __time_critical_func(picostation::DiscImage::readSectorSD)(void *buffer, const int sector, const uint16_t *scramling)
{
	FRESULT fr = FR_INVALID_OBJECT;
	UINT br = 0;

	const int adjustedSector = sector - c_preGap;
    const bool haveDataTrack = (m_cueDisc.trackCount >= 1) &&
                               (m_cueDisc.tracks[1].trackType == CueTrackType::TRACK_TYPE_DATA);

    // When bootsector patching is enabled, substitute the full PS1 license +
    // PlayStation logo area (LBA 0..11, 12 sectors) from loaderImage.
    // Japanese (SCEI) BIOSes verify BOTH the license text (LBA 4) AND the 3D
    // PlayStation logo data (LBA 5..11) byte-for-byte; patching only 0..4 hangs
    // fan-translated/patched discs that replace the PS1 logo (e.g. PaRappa BRA).
    if (!skip_bootsector && haveDataTrack && adjustedSector >= 0 && adjustedSector < 12)
	{
        const size_t loaderOffset = static_cast<size_t>(adjustedSector) * c_cdSamplesBytes;

        if (loaderImageSize >= c_cdSamplesBytes && loaderOffset <= loaderImageSize - c_cdSamplesBytes)
        {
            scramble_data((uint32_t *) buffer, (uint16_t *) &loaderImage[loaderOffset], scramling, 1176);
            return;
        }
	}

    if (adjustedSector < 0)
	{
		if (haveDataTrack)
		{
			buildSector(sector, static_cast<uint32_t *>(buffer), NULL, scramling, true);
		}
		else
		{
			// The I2S buffer holds one 32-bit word per source byte, so it is
			// twice the size of the 2352-byte CD sector.
			memset(buffer, 0, c_cdSamplesBytes * 2);
		}
		return;
	}

    bool sectorRead = false;
    CueTrackType locatedType = CueTrackType::TRACK_TYPE_UNKNOWN;

    for (size_t i = 1; i <= static_cast<size_t>(m_cueDisc.trackCount); i++)
    {
        if (adjustedSector >= static_cast<int>(m_cueDisc.tracks[i + 1].indices[0]))
        {
            continue;
        }

        locatedType = m_cueDisc.tracks[i].trackType;
        struct CueFile *file = m_cueDisc.tracks[i].file;

        if (!file || !file->opaque)
        {
            break;
        }

        const int64_t seekBytes =
            (static_cast<int64_t>(adjustedSector) - static_cast<int64_t>(m_cueDisc.tracks[i].fileOffset)) *
            static_cast<int64_t>(c_cdSamplesBytes);

        // A negative offset means the requested sector is before this track's
        // file data (a gap). An offset past EOF means the cue's implied track
        // length is longer than the file (a gap too). In both cases, let the
        // caller synthesise a pregap sector instead of reading stale data or
        // letting FatFs walk past the end of the cluster chain.
        if (seekBytes < 0 ||
            static_cast<uint64_t>(seekBytes) + c_cdSamplesBytes > posix_file_get_size(file))
        {
            break;
        }

        if (!posix_file_ensure_open(file))
        {
            DEBUG_PRINT("lazy open failed for track %u\n", static_cast<unsigned>(i));
            break;
        }

        FIL *trackFp = posix_file_get_fil(file);
        if (!trackFp)
        {
            break;
        }
        if (!trackFp->cltbl &&
            (static_cast<FSIZE_t>(seekBytes) < trackFp->fptr ||
             static_cast<FSIZE_t>(seekBytes) > trackFp->fptr + 262144u))
        {
            posix_file_ensure_fastseek(file);
        }

        fr = f_lseek(trackFp, static_cast<FSIZE_t>(seekBytes));
        if (FR_OK != fr)
        {
            DEBUG_PRINT("f_lseek error: (%d)\n", fr);
            break;
        }

        if (skip_edc || locatedType != CueTrackType::TRACK_TYPE_DATA)
        {
            fr = f_read_scramble(trackFp, buffer, c_cdSamplesBytes, &br,
                                 scramling, locatedType == CueTrackType::TRACK_TYPE_DATA);
        }
        else
        {
            fr = f_read(trackFp, s_userData, c_cdSamplesBytes, &br);

            if (FR_OK == fr && br == c_cdSamplesBytes)
            {
                eccedc_generate((uint8_t *) s_userData);
                scramble_data((uint32_t *) buffer, s_userData, scramling, c_cdSamplesBytes / 2);
            }
        }

        if (FR_OK == fr && br == c_cdSamplesBytes)
        {
            sectorRead = true;
            if (locatedType != CueTrackType::TRACK_TYPE_DATA)
            {
                const unsigned int audioMode = g_audioCtrlMode;
                if (audioMode == audioControlModes::LEVELMETER || audioMode == audioControlModes::PEAKMETER)
                {
                    updateAudioPeak(static_cast<const uint32_t *>(buffer));
                }
            }
            else
            {
                m_audioPeakLR = 0;
                m_audioPeakMax = 0;
                __dmb();
            }
        }
        else
        {
            DEBUG_PRINT("read error: fr=%d br=%u\n", fr, br);
        }

        break;
    }

    if (!sectorRead)
    {
        m_audioPeakLR = 0;
        m_audioPeakMax = 0;
        __dmb();
        // Never leave partially-read/stale data in the output buffer.
        if (locatedType == CueTrackType::TRACK_TYPE_DATA ||
            (locatedType == CueTrackType::TRACK_TYPE_UNKNOWN && haveDataTrack))
        {
            buildSector(sector, static_cast<uint32_t *>(buffer), NULL, scramling, true);
        }
        else
        {
            // Full 1176-word I2S buffer, not just the 2352-byte source size.
            memset(buffer, 0, c_cdSamplesBytes * 2);
        }
    }
}

