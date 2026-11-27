#define _GNU_SOURCE
#include "pico_audio.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ff.h"
#include "hardware/sync.h"
#include "logging.h"
#include "pico/time.h"
#include "picostation.h"
#include "values.h"

extern "C" {
#include "libhelix-mp3/mp3dec.h"
}

#if DEBUG_I2S
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

namespace picostation {

PicoAudio g_picoAudio;

// Deterministic bump-allocator backed by idle pioSamples[2..31] (~141 KB) while
// PicoPlayer is active. Zero heap allocations, zero OOM, CACHED_SECS=32 preserved!
static uint8_t *s_arenaBase = nullptr;
static size_t   s_arenaCap  = 0;
static size_t   s_arenaUsed = 0;

static void *arenaAlloc(size_t sz) {
    sz = (sz + 3u) & ~static_cast<size_t>(3u);
    if (!s_arenaBase || s_arenaUsed + sz > s_arenaCap) {
        return nullptr;
    }
    void *ptr = s_arenaBase + s_arenaUsed;
    s_arenaUsed += sz;
    memset(ptr, 0, sz);
    return ptr;
}

extern "C" void *pico_audio_helix_malloc(size_t sz) {
    return arenaAlloc(sz);
}

extern "C" void pico_audio_helix_free(void *ptr) {
    (void)ptr;
}

static inline uint8_t toBcdByte(int v) {
    if (v < 0) return 0;
    if (v > 99) return 0x99;
    return static_cast<uint8_t>(((v / 10) << 4) | (v % 10));
}

static void sectorToBcdMsf(int sector, uint8_t &mm, uint8_t &ss, uint8_t &ff) {
    if (sector < 0) sector = 0;
    mm = toBcdByte(sector / (75 * 60));
    ss = toBcdByte((sector / 75) % 60);
    ff = toBcdByte(sector % 75);
}

static bool endsWithCase(const char *str, const char *suffix) {
    size_t slen = strlen(str);
    size_t xlen = strlen(suffix);
    if (slen < xlen) return false;
    return strcasecmp(str + slen - xlen, suffix) == 0;
}

static void copyCleanString(char *dst, size_t dstSize, const char *src, size_t srcLen) {
    if (!dst || dstSize == 0) return;
    size_t out = 0;
    for (size_t i = 0; i < srcLen && src[i] != '\0' && out + 1 < dstSize; i++) {
        uint8_t c = static_cast<uint8_t>(src[i]);
        if (c < 0x20u) continue;
        dst[out++] = static_cast<char>(c);
    }
    while (out > 0 && dst[out - 1] == ' ') out--;
    dst[out] = '\0';
}

static void extractBaseTitle(const char *filename, char *dst, size_t dstSize) {
    const char *dot = strrchr(filename, '.');
    size_t len = dot ? static_cast<size_t>(dot - filename) : strlen(filename);
    copyCleanString(dst, dstSize, filename, len);
}

static uint32_t readLe32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static uint16_t readLe16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t readBe32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

static uint32_t readSynchsafe32(const uint8_t *p) {
    return ((static_cast<uint32_t>(p[0]) & 0x7Fu) << 21) |
           ((static_cast<uint32_t>(p[1]) & 0x7Fu) << 14) |
           ((static_cast<uint32_t>(p[2]) & 0x7Fu) << 7) |
           (static_cast<uint32_t>(p[3]) & 0x7Fu);
}

static void parseId3v2TextFrame(const uint8_t *payload, uint32_t len, char *dst, size_t dstSize) {
    if (len < 2 || dst[0] != '\0') return;
    uint8_t enc = payload[0];
    if (enc == 0 || enc == 3) {
        copyCleanString(dst, dstSize, reinterpret_cast<const char *>(payload + 1), len - 1);
    } else if (enc == 1 || enc == 2) {
        size_t pos = 1;
        bool le = (enc == 1);
        if (enc == 1 && len >= 3) {
            if (payload[1] == 0xFF && payload[2] == 0xFE) { le = true; pos = 3; }
            else if (payload[1] == 0xFE && payload[2] == 0xFF) { le = false; pos = 3; }
        }
        size_t out = 0;
        while (pos + 1 < len && out + 1 < dstSize) {
            uint16_t ch = le ? static_cast<uint16_t>(payload[pos] | (payload[pos + 1] << 8))
                             : static_cast<uint16_t>((payload[pos] << 8) | payload[pos + 1]);
            pos += 2;
            if (ch == 0) break;
            if (ch >= 0x20u && ch <= 0xFFu) {
                dst[out++] = static_cast<char>(ch);
            }
        }
        while (out > 0 && dst[out - 1] == ' ') out--;
        dst[out] = '\0';
    }
}

void PicoAudio::readId3TagsFromOpenFile(uint32_t fileSize, char *title, size_t tSz, char *artist, size_t aSz, char *album, size_t alSz) {
    // Reads ID3v2 / ID3v1 from already-open m_arena->fp using arena scratch (0 stack & 0 BSS!)
    if (!m_arena || fileSize < 128) return;

    uint8_t *tagScratch = m_arena->tagScratch;
    constexpr size_t tagScratchSize = sizeof(m_arena->tagScratch);
    uint8_t hdr[10];
    UINT br = 0;
    if (f_lseek(&m_arena->fp, 0) == FR_OK &&
        f_read(&m_arena->fp, hdr, 10, &br) == FR_OK && br == 10 && memcmp(hdr, "ID3", 3) == 0) {
        const uint8_t verMajor = hdr[3];
        const uint32_t tagSize = readSynchsafe32(hdr + 6);
        uint32_t dataOffset = 10u + tagSize;
        if (dataOffset > fileSize) dataOffset = fileSize;

        if (verMajor == 3 || verMajor == 4) {
            uint32_t limit = dataOffset < 8192u ? dataOffset : 8192u;
            uint32_t pos = 10;
            // Skip ID3v2 extended header if flag bit 6 (0x40) is set
            if ((hdr[5] & 0x40u) != 0u && pos + 4u < dataOffset) {
                uint8_t extSz[4];
                if (f_lseek(&m_arena->fp, pos) == FR_OK &&
                    f_read(&m_arena->fp, extSz, 4, &br) == FR_OK && br == 4) {
                    uint32_t eLen = (verMajor == 4) ? readSynchsafe32(extSz) : (readBe32(extSz) + 4u);
                    if (eLen >= 4u && pos + eLen < dataOffset) {
                        pos += eLen;
                    }
                }
            }
            while (pos + 10 < limit) {
                uint8_t fhdr[10];
                if (f_lseek(&m_arena->fp, pos) != FR_OK) break;
                if (f_read(&m_arena->fp, fhdr, 10, &br) != FR_OK || br != 10) break;
                if (fhdr[0] == 0) break;
                uint32_t fsize = (verMajor == 4) ? readSynchsafe32(fhdr + 4) : readBe32(fhdr + 4);
                pos += 10;
                if (fsize == 0 || pos + fsize > dataOffset) break;

                if (fsize <= tagScratchSize &&
                    (memcmp(fhdr, "TIT2", 4) == 0 ||
                     memcmp(fhdr, "TPE1", 4) == 0 ||
                     memcmp(fhdr, "TALB", 4) == 0)) {
                    if (f_read(&m_arena->fp, tagScratch, fsize, &br) == FR_OK && br == fsize) {
                        if (memcmp(fhdr, "TIT2", 4) == 0)      parseId3v2TextFrame(tagScratch, fsize, title, tSz);
                        else if (memcmp(fhdr, "TPE1", 4) == 0) parseId3v2TextFrame(tagScratch, fsize, artist, aSz);
                        else if (memcmp(fhdr, "TALB", 4) == 0) parseId3v2TextFrame(tagScratch, fsize, album, alSz);
                    }
                }
                pos += fsize;
            }
        }
    }

    if (title[0] == '\0' || artist[0] == '\0' || album[0] == '\0') {
        if (f_lseek(&m_arena->fp, fileSize - 128u) == FR_OK &&
            f_read(&m_arena->fp, tagScratch, 128, &br) == FR_OK && br == 128 &&
            memcmp(tagScratch, "TAG", 3) == 0) {
            if (title[0] == '\0')  copyCleanString(title,  tSz,  reinterpret_cast<const char *>(tagScratch + 3), 30);
            if (artist[0] == '\0') copyCleanString(artist, aSz,  reinterpret_cast<const char *>(tagScratch + 33), 30);
            if (album[0] == '\0')  copyCleanString(album,  alSz, reinterpret_cast<const char *>(tagScratch + 63), 30);
        }
    }
}

bool PicoAudio::probeWav(const char *path, TrackEntry &entry) {
    if (!m_arena) return false;
    if (f_open(&m_arena->fp, path, FA_READ) != FR_OK) return false;

    uint8_t hdr[12];
    UINT br = 0;
    if (f_read(&m_arena->fp, hdr, 12, &br) != FR_OK || br != 12 ||
        memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        f_close(&m_arena->fp);
        return false;
    }

    bool gotFmt = false, gotData = false;
    uint16_t audioFmt = 0, channels = 0, bits = 0;
    uint32_t sampleRate = 0, dataOff = 0, dataBytes = 0;

    while (!gotFmt || !gotData) {
        uint8_t chdr[8];
        if (f_read(&m_arena->fp, chdr, 8, &br) != FR_OK || br != 8) break;
        uint32_t csize = readLe32(chdr + 4);
        uint32_t cpos = static_cast<uint32_t>(f_tell(&m_arena->fp));

        if (memcmp(chdr, "fmt ", 4) == 0 && csize >= 16) {
            uint8_t fmtBuf[16];
            if (f_read(&m_arena->fp, fmtBuf, 16, &br) == FR_OK && br == 16) {
                audioFmt   = readLe16(fmtBuf + 0);
                channels   = readLe16(fmtBuf + 2);
                sampleRate = readLe32(fmtBuf + 4);
                bits       = readLe16(fmtBuf + 14);
                gotFmt = true;
            }
        } else if (memcmp(chdr, "data", 4) == 0) {
            dataOff = cpos;
            dataBytes = csize;
            gotData = true;
        }
        uint32_t nextPos = cpos + csize + (csize & 1u);
        if (f_lseek(&m_arena->fp, nextPos) != FR_OK) break;
    }
    const uint32_t fileSize = static_cast<uint32_t>(f_size(&m_arena->fp));
    f_close(&m_arena->fp);

    if (!gotFmt || !gotData || audioFmt != 1 || bits != 16 ||
        (channels != 1 && channels != 2) || sampleRate != 44100u ||
        dataOff >= fileSize) {
        return false;
    }

    // Clamp WAV data chunk size to real file size (prevents 0xFFFFFFFF streaming WAVs)
    const uint32_t maxAvail = fileSize - dataOff;
    if (dataBytes > maxAvail) dataBytes = maxAvail;
    if (dataBytes < 2352u) return false;

    entry.format       = PICO_AUDIO_FMT_WAV;
    entry.channels     = static_cast<uint8_t>(channels);
    entry.sampleRateHz = static_cast<uint16_t>(sampleRate);
    entry.bitrateKbps  = static_cast<uint16_t>((sampleRate * channels * 16u) / 1000u);
    entry.dataOffset   = dataOff;
    entry.dataBytes    = dataBytes;
    const uint32_t bytesPerSector = (channels == 2) ? 2352u : 1176u;
    uint32_t lenSecs = (dataBytes + bytesPerSector - 1u) / bytesPerSector;
    if (lenSecs < 75u) lenSecs = 75u;
    if (lenSecs > 330000u) lenSecs = 330000u;
    entry.lengthSectors = lenSecs;
    return true;
}

bool PicoAudio::probeMp3(const char *path, TrackEntry &entry) {
    if (!m_arena) return false;
    if (f_open(&m_arena->fp, path, FA_READ) != FR_OK) return false;

    const uint32_t fileSize = static_cast<uint32_t>(f_size(&m_arena->fp));
    if (fileSize < 128) {
        f_close(&m_arena->fp);
        return false;
    }

    uint32_t dataOffset = 0;
    uint8_t hdr[10];
    UINT br = 0;
    if (f_read(&m_arena->fp, hdr, 10, &br) == FR_OK && br == 10 && memcmp(hdr, "ID3", 3) == 0) {
        const uint32_t tagSize = readSynchsafe32(hdr + 6);
        dataOffset = 10u + tagSize;
        if (dataOffset >= fileSize) dataOffset = 0;
    }

    // Reuse m_arena->metaBuf (2324 B) as temporary scan buffer (0 stack usage!)
    bool validFrame = false;
    int bitrate = 192000, samprate = 44100, nChans = 2;
    uint32_t xingFrames = 0;
    uint32_t samplesPerFrame = 1152;
    uint32_t firstFrameBytes = 0;
    uint8_t *scanBuf = reinterpret_cast<uint8_t *>(m_arena->metaBuf);

    static const uint16_t kBrMpeg1L3[16] = {
        0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
    };
    static const uint16_t kBrMpeg2L3[16] = {
        0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0
    };
    static const uint16_t kSrMpeg1[4]  = { 44100, 48000, 32000, 0 };
    static const uint16_t kSrMpeg2[4]  = { 22050, 24000, 16000, 0 };
    static const uint16_t kSrMpeg25[4] = { 11025, 12000,  8000, 0 };

    if (f_lseek(&m_arena->fp, dataOffset) == FR_OK &&
        f_read(&m_arena->fp, scanBuf, 2048, &br) == FR_OK && br >= 4) {
        int syncOff = MP3FindSyncWord(scanBuf, static_cast<int>(br));
        if (syncOff >= 0 && syncOff + 4 <= static_cast<int>(br)) {
            const uint8_t *h = scanBuf + syncOff;
            const int verBits   = (h[1] >> 3) & 0x03;
            const int layerBits = (h[1] >> 1) & 0x03;
            const int brIdx     = (h[2] >> 4) & 0x0F;
            const int srIdx     = (h[2] >> 2) & 0x03;
            const int padBit    = (h[2] >> 1) & 0x01;
            const int chMode    = (h[3] >> 6) & 0x03;

            if (verBits != 1 && layerBits == 1 && brIdx > 0 && brIdx < 15 && srIdx < 3) {
                uint16_t kbps = (verBits == 3) ? kBrMpeg1L3[brIdx] : kBrMpeg2L3[brIdx];
                uint16_t sr   = (verBits == 3) ? kSrMpeg1[srIdx]
                              : (verBits == 2) ? kSrMpeg2[srIdx]
                                               : kSrMpeg25[srIdx];
                if (kbps > 0 && sr > 0) {
                    bitrate  = static_cast<int>(kbps) * 1000;
                    samprate = static_cast<int>(sr);
                    nChans   = (chMode == 3) ? 1 : 2;
                    samplesPerFrame = (verBits == 3) ? 1152u : 576u;
                    firstFrameBytes = (verBits == 3)
                                          ? ((144u * static_cast<uint32_t>(bitrate)) / static_cast<uint32_t>(samprate) + static_cast<uint32_t>(padBit))
                                          : ((72u * static_cast<uint32_t>(bitrate)) / static_cast<uint32_t>(samprate) + static_cast<uint32_t>(padBit));
                    dataOffset += static_cast<uint32_t>(syncOff);
                    validFrame = true;

                    // Parse Xing / Info / VBRI header for exact VBR/CBR frame count!
                    const uint32_t sideInfo = (verBits == 3) ? ((nChans == 1) ? 17u : 32u)
                                                             : ((nChans == 1) ? 9u : 17u);
                    const uint32_t xOff = static_cast<uint32_t>(syncOff) + 4u + sideInfo;
                    if (xOff + 12u <= br &&
                        (memcmp(scanBuf + xOff, "Xing", 4) == 0 ||
                         memcmp(scanBuf + xOff, "Info", 4) == 0)) {
                        const uint32_t flags = readBe32(scanBuf + xOff + 4u);
                        if ((flags & 0x01u) != 0u) {
                            xingFrames = readBe32(scanBuf + xOff + 8u);
                        }
                        if (firstFrameBytes >= 24u && dataOffset + firstFrameBytes < fileSize) {
                            dataOffset += firstFrameBytes;
                        }
                    } else {
                        const uint32_t vOff = static_cast<uint32_t>(syncOff) + 36u;
                        if (vOff + 18u <= br && memcmp(scanBuf + vOff, "VBRI", 4) == 0) {
                            xingFrames = readBe32(scanBuf + vOff + 14u);
                            if (firstFrameBytes >= 24u && dataOffset + firstFrameBytes < fileSize) {
                                dataOffset += firstFrameBytes;
                            }
                        }
                    }
                }
            }
        }
    }
    if (validFrame) {
        readId3TagsFromOpenFile(fileSize, entry.title, sizeof(entry.title),
                                entry.artist, sizeof(entry.artist),
                                entry.album, sizeof(entry.album));
    }

    // Subtract trailing 128-byte ID3v1 tag from audio byte count if present
    uint32_t audioEnd = fileSize;
    if (validFrame && fileSize > dataOffset + 128u) {
        uint8_t tail[3];
        if (f_lseek(&m_arena->fp, fileSize - 128u) == FR_OK &&
            f_read(&m_arena->fp, tail, 3, &br) == FR_OK && br == 3 &&
            memcmp(tail, "TAG", 3) == 0) {
            audioEnd = fileSize - 128u;
        }
    }

    const uint32_t audioBytes = (audioEnd > dataOffset) ? (audioEnd - dataOffset) : 0u;

    // For headerless VBR MP3s (no Xing/Info/VBRI), sample 5 interior offsets to
    // estimate true average bitrate instead of trusting a low-bitrate intro frame!
    if (validFrame && xingFrames == 0 && audioBytes > 8192u) {
        uint64_t brSum = 0;
        uint32_t brCnt = 0;
        static const uint8_t kPct[5] = { 15, 30, 50, 70, 85 };
        for (int s = 0; s < 5; s++) {
            uint32_t probePos = dataOffset + static_cast<uint32_t>((static_cast<uint64_t>(audioBytes) * kPct[s]) / 100u);
            if (probePos + 1024u > audioEnd) continue;
            if (f_lseek(&m_arena->fp, probePos) != FR_OK) continue;
            if (f_read(&m_arena->fp, scanBuf, 1024u, &br) != FR_OK || br < 8u) continue;
            int off = MP3FindSyncWord(scanBuf, static_cast<int>(br));
            if (off < 0 || off + 4 > static_cast<int>(br)) continue;
            const uint8_t *h = scanBuf + off;
            const int vb = (h[1] >> 3) & 0x03;
            const int lb = (h[1] >> 1) & 0x03;
            const int bi = (h[2] >> 4) & 0x0F;
            const int si = (h[2] >> 2) & 0x03;
            if (vb != 1 && lb == 1 && bi > 0 && bi < 15 && si < 3) {
                uint16_t kbps = (vb == 3) ? kBrMpeg1L3[bi] : kBrMpeg2L3[bi];
                uint16_t sr   = (vb == 3) ? kSrMpeg1[si]
                              : (vb == 2) ? kSrMpeg2[si]
                                          : kSrMpeg25[si];
                if (kbps > 0 && sr == static_cast<uint16_t>(samprate)) {
                    brSum += static_cast<uint64_t>(kbps) * 1000ULL;
                    brCnt++;
                }
            }
        }
        if (brCnt >= 2) {
            bitrate = static_cast<int>(brSum / brCnt);
        }
    }

    f_close(&m_arena->fp);
    if (!validFrame || audioBytes < 128u) return false;

    uint64_t secs75 = 0;
    if (xingFrames > 0) {
        secs75 = (static_cast<uint64_t>(xingFrames) * static_cast<uint64_t>(samplesPerFrame) * 75ULL) /
                 static_cast<uint64_t>(samprate);
        if (secs75 >= 75ULL) {
            bitrate = static_cast<int>((static_cast<uint64_t>(audioBytes) * 8ULL * 75ULL) / secs75);
        }
    } else {
        secs75 = (static_cast<uint64_t>(audioBytes) * 8ULL * 75ULL) / static_cast<uint64_t>(bitrate);
    }
    if (secs75 < 75ULL) secs75 = 75ULL;
    if (secs75 > 330000ULL) secs75 = 330000ULL;

    entry.format        = PICO_AUDIO_FMT_MP3;
    entry.channels      = static_cast<uint8_t>(nChans);
    entry.bitrateKbps   = static_cast<uint16_t>(bitrate / 1000);
    entry.sampleRateHz  = static_cast<uint16_t>(samprate);
    entry.dataOffset    = dataOffset;
    entry.dataBytes     = audioBytes;
    entry.lengthSectors = static_cast<uint32_t>(secs75);
    return true;
}

bool PicoAudio::ensureArena(bool zeroAll) {
    if (!s_arenaBase || s_arenaCap < sizeof(ArenaState) + 32768u) {
        m_arena = nullptr;
        return false;
    }
    m_arena = reinterpret_cast<ArenaState *>(s_arenaBase);
    if (zeroAll) {
        memset(m_arena, 0, sizeof(ArenaState));
    }
    s_arenaUsed = (sizeof(ArenaState) + 3u) & ~static_cast<size_t>(3u);
    return true;
}

static uint32_t fnv1aFileSig(const char *fname, uint32_t fsize) {
    uint32_t h = 2166136261u;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(fname); *p; p++) {
        uint8_t c = *p;
        if (c >= 'A' && c <= 'Z') c = static_cast<uint8_t>(c + ('a' - 'A'));
        h ^= c;
        h *= 16777619u;
    }
    h ^= 0xFFu;
    h *= 16777619u;
    for (int shift = 0; shift < 32; shift += 8) {
        h ^= static_cast<uint8_t>((fsize >> shift) & 0xFFu);
        h *= 16777619u;
    }
    return h;
}

bool PicoAudio::tryLoadIndex() {
    if (!m_arena) return false;

    // 1) Compute order-independent directory signature with ZERO f_open() calls (~5 ms)
    uint16_t dirFileCount = 0;
    uint32_t dirHashXor = 0;
    uint32_t dirHashSum = 0;
    if (f_opendir(&m_arena->scanDir, "/PicoPlayer") != FR_OK) {
        return false;
    }
    while (true) {
        if (f_readdir(&m_arena->scanDir, &m_arena->scanFno) != FR_OK || m_arena->scanFno.fname[0] == '\0') break;
        if ((m_arena->scanFno.fattrib & (AM_DIR | AM_HID)) != 0 || m_arena->scanFno.fname[0] == '.') continue;
        if (endsWithCase(m_arena->scanFno.fname, ".mp3") ||
            endsWithCase(m_arena->scanFno.fname, ".wav") ||
            endsWithCase(m_arena->scanFno.fname, ".cov")) {
            const uint32_t sig = fnv1aFileSig(m_arena->scanFno.fname, static_cast<uint32_t>(m_arena->scanFno.fsize));
            dirHashXor ^= sig;
            dirHashSum += sig;
            dirFileCount++;
        }
    }
    f_closedir(&m_arena->scanDir);
    if (dirFileCount == 0) return false;

    // 2) Open /PicoPlayer/index.pp and validate header + live directory signature
    if (f_open(&m_arena->fp, "/PicoPlayer/index.pp", FA_READ) != FR_OK) {
        return false;
    }
    IndexHeader hdr;
    UINT br = 0;
    bool ok = (f_read(&m_arena->fp, &hdr, sizeof(hdr), &br) == FR_OK && br == sizeof(hdr) &&
               memcmp(hdr.magic, "PPT1", 4) == 0 &&
               hdr.version == 1u &&
               hdr.trackCount >= 1u && hdr.trackCount <= kPicoAudioMaxTracks &&
               hdr.dirFileCount == dirFileCount &&
               hdr.dirHashXor == dirHashXor &&
               hdr.dirHashSum == dirHashSum);

    if (ok) {
        const UINT wantBytes = static_cast<UINT>(hdr.trackCount) * static_cast<UINT>(sizeof(TrackEntry));
        ok = (f_read(&m_arena->fp, m_arena->tracks, wantBytes, &br) == FR_OK && br == wantBytes);
    }
    f_close(&m_arena->fp);
    if (!ok) return false;

    // 3) Defensive sanitization & Red Book TOC sector bounds
    constexpr uint32_t kMaxVirtualDiscSectors = 449800u;
    uint32_t curSector = 0;
    uint16_t fitted = 0;
    m_hasMp3 = false;

    for (uint16_t i = 0; i < hdr.trackCount; i++) {
        TrackEntry &tr = m_arena->tracks[i];
        tr.filename[sizeof(tr.filename) - 1] = '\0';
        tr.title[sizeof(tr.title) - 1]       = '\0';
        tr.artist[sizeof(tr.artist) - 1]     = '\0';
        tr.album[sizeof(tr.album) - 1]       = '\0';

        if (tr.filename[0] == '\0' ||
            (tr.format != PICO_AUDIO_FMT_MP3 && tr.format != PICO_AUDIO_FMT_WAV) ||
            (tr.channels != 1u && tr.channels != 2u) ||
            tr.sampleRateHz < 8000u || tr.sampleRateHz > 48000u ||
            (tr.format == PICO_AUDIO_FMT_WAV && tr.sampleRateHz != 44100u) ||
            tr.lengthSectors < 75u || tr.lengthSectors > 330000u ||
            tr.dataBytes < 128u) {
            return false;
        }
        if (curSector + tr.lengthSectors > kMaxVirtualDiscSectors) {
            break;
        }
        tr.startSector = curSector;
        curSector += tr.lengthSectors;
        if (tr.format == PICO_AUDIO_FMT_MP3) m_hasMp3 = true;
        fitted++;
    }
    if (fitted == 0) return false;

    m_trackCount = fitted;
    m_totalSectors = curSector;
    prepareMetaPage(0);
    return true;
}

uint16_t PicoAudio::scanFolder() {
    deactivate();
    m_trackCount = 0;
    m_totalSectors = 0;
    m_hasMp3 = false;

    if (!ensureArena(true)) {
        return 0;
    }

    // Fast path: load PicoManager-generated /PicoPlayer/index.pp in < 20 ms
    // whenever its live directory hash matches the SD folder 100%!
    if (tryLoadIndex()) {
        return m_trackCount;
    }

    if (f_opendir(&m_arena->scanDir, "/PicoPlayer") != FR_OK) {
        return 0;
    }

    while (m_trackCount < kPicoAudioMaxTracks) {
        if (f_readdir(&m_arena->scanDir, &m_arena->scanFno) != FR_OK || m_arena->scanFno.fname[0] == '\0') break;
        if ((m_arena->scanFno.fattrib & (AM_DIR | AM_HID)) != 0 || m_arena->scanFno.fname[0] == '.') continue;

        const bool isMp3 = endsWithCase(m_arena->scanFno.fname, ".mp3");
        const bool isWav = endsWithCase(m_arena->scanFno.fname, ".wav");
        if (!isMp3 && !isWav) continue;

        TrackEntry &entry = m_arena->tracks[m_trackCount];
        if (strlen(m_arena->scanFno.fname) >= sizeof(entry.filename)) continue;
        memset(&entry, 0, sizeof(entry));
        snprintf(entry.filename, sizeof(entry.filename), "%s", m_arena->scanFno.fname);

        char fullPath[160];
        snprintf(fullPath, sizeof(fullPath), "/PicoPlayer/%s", entry.filename);

        bool ok = isWav ? probeWav(fullPath, entry) : probeMp3(fullPath, entry);
        if (!ok) continue;
        if (entry.title[0] == '\0') {
            extractBaseTitle(entry.filename, entry.title, sizeof(entry.title));
        }
        if (entry.album[0] == '\0') {
            snprintf(entry.album, sizeof(entry.album), "SD:/PICOPLAYER/");
        }
        if (isMp3) m_hasMp3 = true;
        m_trackCount++;
    }
    f_closedir(&m_arena->scanDir);

    bool hasFolderArt = false;
    if (f_stat("/PicoPlayer/cover.cov", &m_arena->scanFno) == FR_OK && m_arena->scanFno.fsize >= 16896) {
        hasFolderArt = true;
    }
    for (uint16_t i = 0; i < m_trackCount; i++) {
        TrackEntry &entry = m_arena->tracks[i];
        if (hasFolderArt) {
            entry.hasArt = 254; // Shared folder cover.cov group (transfer once!)
            continue;
        }
        char covPath[160];
        const char *dot = strrchr(entry.filename, '.');
        size_t stemLen = dot ? static_cast<size_t>(dot - entry.filename) : strlen(entry.filename);
        if (stemLen > 120) stemLen = 120;
        snprintf(covPath, sizeof(covPath), "/PicoPlayer/%.*s.cov", static_cast<int>(stemLen), entry.filename);
        entry.hasArt = (f_stat(covPath, &m_arena->scanFno) == FR_OK && m_arena->scanFno.fsize >= 16896) ? 1 : 0;
    }

    for (uint16_t i = 1; i < m_trackCount; i++) {
        m_arena->sortKey = m_arena->tracks[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0 && strcasecmp(m_arena->tracks[j].filename, m_arena->sortKey.filename) > 0) {
            m_arena->tracks[j + 1] = m_arena->tracks[j];
            j--;
        }
        m_arena->tracks[j + 1] = m_arena->sortKey;
    }

    // Enforce Red Book 2-digit BCD MSF limit (99:59:74 = 449,999 sectors incl. 150 pregap)
    constexpr uint32_t kMaxVirtualDiscSectors = 449800u;
    uint32_t curSector = 0;
    uint16_t fittedTracks = 0;
    for (uint16_t i = 0; i < m_trackCount; i++) {
        if (curSector + m_arena->tracks[i].lengthSectors > kMaxVirtualDiscSectors) {
            break;
        }
        m_arena->tracks[i].startSector = curSector;
        curSector += m_arena->tracks[i].lengthSectors;
        fittedTracks++;
    }
    m_trackCount = fittedTracks;
    m_totalSectors = curSector;
    prepareMetaPage(0);
    return m_trackCount;
}

void PicoAudio::setScratchArena(void *base, size_t capacity) {
    s_arenaBase = static_cast<uint8_t *>(base);
    s_arenaCap  = capacity;
    s_arenaUsed = 0;
}

bool PicoAudio::activate() {
    closeTrackFile();
    if (m_trackCount == 0 || !m_arena || !ensureArena(false)) {
        m_active = false;
        return false;
    }
    m_mp3Dec = nullptr;
    if (m_hasMp3) {
        m_mp3Dec = MP3InitDecoder();
        if (!m_mp3Dec) {
            DEBUG_PRINT("PicoAudio: MP3InitDecoder arena OOM\n");
            m_active = false;
            return false;
        }
    }
    m_pcmCount = 0;
    m_mp3BytesLeft = 0;
    m_mp3ReadPtr = m_arena->pb.mp3InBuf;
    m_openTrackIdx = -1;
    m_expectedRelSector = 0;
    __dmb();
    m_active = true;
    return true;
}

void PicoAudio::deactivate() {
    m_active = false;
    __dmb();
    // Drain any in-flight Core 0 send_subq() alarm callback (< 5 us) before
    // releasing m_arena / pioSamples[2..31] back to PS1 game sector cache.
    busy_wait_us(50);
    closeTrackFile();
    m_mp3Dec = nullptr;
    m_arena = nullptr;
    m_trackCount = 0;
    m_totalSectors = 0;
    s_arenaUsed = 0;
    m_pcmCount = 0;
    m_mp3BytesLeft = 0;
    m_mp3ReadPtr = nullptr;
    m_resampPhase = 0;
    m_resampPrevL = 0;
    m_resampPrevR = 0;
}

void PicoAudio::closeTrackFile() {
    if (m_arena) {
        if (m_fpOpen) {
            m_arena->fp.cltbl = nullptr;
            f_close(&m_arena->fp);
            m_fpOpen = false;
        }
        if (m_artOpen) {
            f_close(&m_arena->artFp);
            m_artOpen = false;
        }
    } else {
        m_fpOpen = false;
        m_artOpen = false;
    }
    m_openTrackIdx = -1;
    m_pcmCount = 0;
    m_mp3BytesLeft = 0;
    m_mp3ReadPtr = m_arena ? m_arena->pb.mp3InBuf : nullptr;
}

bool PicoAudio::openTrackFile(uint16_t trackIdx, uint32_t relSector) {
    if (!m_arena || trackIdx >= m_trackCount) return false;
    const TrackEntry &tr = m_arena->tracks[trackIdx];

    if (!m_fpOpen || m_openTrackIdx != static_cast<int>(trackIdx)) {
        closeTrackFile();
        char fullPath[160];
        snprintf(fullPath, sizeof(fullPath), "/PicoPlayer/%s", tr.filename);
        if (f_open(&m_arena->fp, fullPath, FA_READ) != FR_OK) {
            return false;
        }
        m_fpOpen = true;
        m_openTrackIdx = static_cast<int>(trackIdx);

        // Enable FatFS O(1) fast-seek cluster link map so seeking anywhere in an
        // 8 MB MP3 takes ~10 us instead of walking the FAT chain for 30+ ms!
        // CRITICAL for exFAT: contiguous files (obj.stat & 2 = NoFatChain) have
        // ZERO entries in the FAT table; calling CREATE_LINKMAP on them aborts
        // with FR_INT_ERR and poisons fp.err permanently! Synthesize a 1-extent
        // CLMT in RAM instead (matching third_party/posix_file.c).
        m_arena->fp.cltbl = nullptr;
#if FF_FS_EXFAT
        if (m_arena->fp.obj.fs && m_arena->fp.obj.fs->fs_type == FS_EXFAT &&
            (m_arena->fp.obj.stat & 2u) != 0u && m_arena->fp.obj.sclust != 0u) {
            const DWORD clstBytes = static_cast<DWORD>(m_arena->fp.obj.fs->csize) * 512u;
            const DWORD totalClst = (clstBytes > 0u)
                ? static_cast<DWORD>((m_arena->fp.obj.objsize + clstBytes - 1u) / clstBytes)
                : 0u;
            if (totalClst > 0u) {
                m_arena->pb.clmt[0] = 4u;
                m_arena->pb.clmt[1] = totalClst;
                m_arena->pb.clmt[2] = m_arena->fp.obj.sclust;
                m_arena->pb.clmt[3] = 0u;
                m_arena->fp.cltbl = m_arena->pb.clmt;
            }
        } else
#endif
        {
            m_arena->pb.clmt[0] = PlaybackBuffers::kClmtSize;
            m_arena->fp.cltbl = m_arena->pb.clmt;
            if (f_lseek(&m_arena->fp, CREATE_LINKMAP) != FR_OK) {
                m_arena->fp.cltbl = nullptr;
                m_arena->fp.err = 0; // Never leave fp.err poisoned!
            }
        }
    }

    if (tr.format == PICO_AUDIO_FMT_MP3) {
        // Rewind arena right past ArenaState so MP3InitDecoder() reuses the exact
        // same 24 KB block on every track switch / seek with zero fragmentation!
        s_arenaUsed = (sizeof(ArenaState) + 3u) & ~static_cast<size_t>(3u);
        m_mp3Dec = MP3InitDecoder();
        if (!m_mp3Dec) return false;

        uint32_t byteOff = tr.dataOffset;
        if (relSector > 0 && tr.lengthSectors > 0) {
            uint64_t est = (static_cast<uint64_t>(relSector) * tr.dataBytes) / tr.lengthSectors;
            const uint32_t maxOff = (tr.dataBytes > 256u) ? (tr.dataBytes - 256u) : 0u;
            if (est > maxOff) est = maxOff;
            byteOff += static_cast<uint32_t>(est);
        }
        f_lseek(&m_arena->fp, byteOff);
        m_mp3BytesLeft = 0;
        m_mp3ReadPtr = m_arena->pb.mp3InBuf;
        m_pcmCount = 0;
        m_resampPhase = 0;
        m_resampPrevL = 0;
        m_resampPrevR = 0;

        // Pre-fill mp3InBuf once on track open/seek so the first decode slot
        // has compressed data already in RAM with zero SD read latency!
        UINT br = 0;
        if (f_read(&m_arena->fp, m_arena->pb.mp3InBuf, PlaybackBuffers::kInBufSize, &br) == FR_OK && br > 0) {
            m_mp3BytesLeft = static_cast<int>(br);
        }
    }
    m_expectedRelSector = relSector;
    return true;
}

void PicoAudio::prepareMetaPage(uint32_t page) {
    // 100% RAM-only — ZERO SD I/O! Safe to call from I2S sector delivery at any time.
    if (!m_arena) return;
    memset(m_arena->metaBuf, 0, sizeof(m_arena->metaBuf));
    uint8_t *out = reinterpret_cast<uint8_t *>(m_arena->metaBuf);
    memcpy(out, "PPA1", 4);
    const uint16_t start = static_cast<uint16_t>(page * kPicoAudioTracksPerSector);
    uint16_t count = 0;
    if (start < m_trackCount) {
        uint16_t rem = static_cast<uint16_t>(m_trackCount - start);
        count = (rem > kPicoAudioTracksPerSector) ? kPicoAudioTracksPerSector : rem;
    }
    out[4]  = static_cast<uint8_t>(m_trackCount & 0xFFu);
    out[5]  = static_cast<uint8_t>(m_trackCount >> 8);
    out[6]  = static_cast<uint8_t>(page & 0xFFu);
    out[7]  = static_cast<uint8_t>(page >> 8);
    out[8]  = static_cast<uint8_t>(count & 0xFFu);
    out[9]  = static_cast<uint8_t>(count >> 8);

    PicoAudioWireTrack *dst = reinterpret_cast<PicoAudioWireTrack *>(out + 12);
    for (uint16_t i = 0; i < count; i++) {
        const TrackEntry &tr = m_arena->tracks[start + i];
        memset(&dst[i], 0, sizeof(PicoAudioWireTrack));
        snprintf(dst[i].title,  sizeof(dst[i].title),  "%s", tr.title);
        snprintf(dst[i].artist, sizeof(dst[i].artist), "%s", tr.artist);
        snprintf(dst[i].album,  sizeof(dst[i].album),  "%s", tr.album);
        // Pack deduplicated cover group ID (2..254) in unused artist[31] byte
        // (TrackEntry::artist is 28 B, so artist[28..31] are always 0).
        dst[i].artist[31]      = static_cast<char>(tr.hasArt);
        dst[i].format          = static_cast<uint8_t>(tr.format | (tr.hasArt ? 0x80u : 0x00u));
        dst[i].trackNum      = static_cast<uint8_t>(start + i + 1);
        dst[i].bitrateKbps   = tr.bitrateKbps;
        dst[i].sampleRateHz  = tr.sampleRateHz;
        dst[i].lengthSectors = tr.lengthSectors;
    }
}

uint16_t *PicoAudio::getMetaBuffer(uint32_t page) {
    if (!m_arena) return nullptr;
    prepareMetaPage(page);
    return m_arena->metaBuf;
}

void PicoAudio::openArt(uint16_t trackIdx) {
    if (!m_arena) return;
    if (m_artOpen) {
        f_close(&m_arena->artFp);
        m_artOpen = false;
    }
    if (trackIdx >= m_trackCount) return;

    const TrackEntry &tr = m_arena->tracks[trackIdx];
    char covPath[160];
    const char *dot = strrchr(tr.filename, '.');
    size_t stemLen = dot ? static_cast<size_t>(dot - tr.filename) : strlen(tr.filename);
    if (stemLen > 120) stemLen = 120;
    snprintf(covPath, sizeof(covPath), "/PicoPlayer/%.*s.cov", static_cast<int>(stemLen), tr.filename);

    if (f_open(&m_arena->artFp, covPath, FA_READ) == FR_OK) {
        m_artOpen = true;
        return;
    }
    if (f_open(&m_arena->artFp, "/PicoPlayer/cover.cov", FA_READ) == FR_OK) {
        m_artOpen = true;
    }
}

uint16_t *PicoAudio::readArtPart(uint32_t part) {
    if (!m_arena) return nullptr;
    memset(m_arena->metaBuf, 0, sizeof(m_arena->metaBuf));
    if (!m_artOpen || part >= 8) {
        m_arena->metaBuf[0] = 'D' | ('E' << 8);
        m_arena->metaBuf[1] = 'A' | ('D' << 8);
        return m_arena->metaBuf;
    }
    UINT br = 0;
    f_lseek(&m_arena->artFp, static_cast<FSIZE_t>(part * 2114u));
    f_read(&m_arena->artFp, &m_arena->metaBuf[3], 2114, &br);
    m_arena->metaBuf[0] = 'P' | ('A' << 8);
    m_arena->metaBuf[1] = 'R' | ('T' << 8);
    m_arena->metaBuf[2] = static_cast<uint16_t>(part);
    return m_arena->metaBuf;
}

SubQ::Data PicoAudio::generateSubQ(const int sector) {
    SubQ::Data subq;
    memset(&subq, 0, sizeof(subq));
    subq.crc = 0xBEEF;

    // Snapshot arena pointer after memory barrier (Core 0 alarm vs Core 1 deactivate)
    const ArenaState *arena = m_arena;
    const uint16_t trackCount = m_trackCount;
    const uint32_t totalSectors = m_totalSectors;
    __dmb();

    if (!m_active || !arena || trackCount == 0) {
        subq.ctrladdr = 0x01;
        subq.tno = 0x01;
        subq.point = 0x01;
        return subq;
    }

    if (sector < 0) {
        // Lead-in: rawSec = 0..4499 (increasing). TOC track 1 starts at 00:02:00 (c_preGap=150).
        const int rawSec = (sector + c_leadIn >= 0) ? (sector + c_leadIn) : 0;
        const uint32_t totalEntries = 3u + static_cast<uint32_t>(trackCount);
        const uint32_t entryIdx = (static_cast<uint32_t>(rawSec) / 3u) % totalEntries;
        subq.ctrladdr = 0x01;
        subq.tno = 0x00;

        if (entryIdx == 0) {
            subq.point = 0xA0;
            subq.pmin = 0x01;
            subq.psec = 0x00;
            subq.pframe = 0x00;
        } else if (entryIdx == 1) {
            subq.point = 0xA1;
            subq.pmin = toBcdByte(trackCount);
            subq.psec = 0x00;
            subq.pframe = 0x00;
        } else if (entryIdx == 2) {
            subq.point = 0xA2;
            sectorToBcdMsf(static_cast<int>(totalSectors) + c_preGap, subq.pmin, subq.psec, subq.pframe);
        } else {
            const uint32_t t = entryIdx - 3u;
            subq.point = toBcdByte(static_cast<int>(t + 1));
            sectorToBcdMsf(static_cast<int>(arena->tracks[t].startSector) + c_preGap, subq.pmin, subq.psec, subq.pframe);
        }
        sectorToBcdMsf(rawSec, subq.min, subq.sec, subq.frame);
        subq.zero = 0x00;
        return subq;
    }

    // Track 1 pregap (00:00:00 .. 00:01:74): index 00 countdown, absolute MSF = sector
    if (sector < c_preGap) {
        subq.ctrladdr = 0x01;
        subq.tno = 0x01;
        subq.point = 0x00;
        sectorToBcdMsf(c_preGap - sector, subq.min, subq.sec, subq.frame);
        subq.zero = 0x00;
        sectorToBcdMsf(sector, subq.pmin, subq.psec, subq.pframe);
        return subq;
    }

    // Program area starts at sector == c_preGap (00:02:00 = LBA 0)
    const uint32_t progSec = static_cast<uint32_t>(sector - c_preGap);
    if (progSec >= totalSectors) {
        // Lead-out area (tno 0xAA), matching DiscImage::generateSubQ()
        subq.ctrladdr = 0x01;
        subq.tno = 0xAA;
        subq.point = 0x01;
        sectorToBcdMsf(static_cast<int>(progSec - totalSectors), subq.min, subq.sec, subq.frame);
        subq.zero = 0x00;
        sectorToBcdMsf(sector, subq.pmin, subq.psec, subq.pframe);
        return subq;
    }

    uint16_t tIdx = 0;
    for (uint16_t i = 0; i < trackCount; i++) {
        if (progSec >= arena->tracks[i].startSector) {
            tIdx = i;
        } else {
            break;
        }
    }

    subq.ctrladdr = 0x01;
    subq.tno = toBcdByte(static_cast<int>(tIdx + 1));
    subq.point = 0x01;
    const int rel = static_cast<int>(progSec - arena->tracks[tIdx].startSector);
    sectorToBcdMsf(rel, subq.min, subq.sec, subq.frame);
    subq.zero = 0x00;
    // sector is ALREADY absolute MSF (00:02:00 at LBA 0) — never add +150 twice!
    sectorToBcdMsf(sector, subq.pmin, subq.psec, subq.pframe);
    return subq;
}

void PicoAudio::decodeWavSector(uint32_t *i2sBuffer, const TrackEntry &tr, uint32_t relSector) {
    if (!m_arena) {
        memset(i2sBuffer, 0, c_cdSamplesBytes * 2);
        return;
    }
    PlaybackBuffers &pb = m_arena->pb;
    const uint32_t bytesPerSec = (tr.channels == 2) ? 2352u : 1176u;
    const uint32_t off = relSector * bytesPerSec;
    if (off >= tr.dataBytes) {
        memset(i2sBuffer, 0, c_cdSamplesBytes * 2);
        return;
    }

    const uint32_t targetPos = tr.dataOffset + off;
    if (f_tell(&m_arena->fp) != targetPos) {
        if (f_lseek(&m_arena->fp, targetPos) != FR_OK) {
            memset(i2sBuffer, 0, c_cdSamplesBytes * 2);
            return;
        }
    }

    UINT br = 0;
    uint32_t toRead = bytesPerSec;
    if (off + toRead > tr.dataBytes) toRead = tr.dataBytes - off;

    if (tr.channels == 2) {
        memset(pb.sectorPcm, 0, sizeof(pb.sectorPcm));
        f_read(&m_arena->fp, pb.sectorPcm, toRead, &br);
    } else {
        memset(pb.frameOut, 0, 588 * sizeof(int16_t));
        f_read(&m_arena->fp, pb.frameOut, toRead, &br);
        for (int i = 0; i < 588; i++) {
            pb.sectorPcm[i * 2]     = pb.frameOut[i];
            pb.sectorPcm[i * 2 + 1] = pb.frameOut[i];
        }
    }

    scramble_data(i2sBuffer, reinterpret_cast<uint16_t *>(pb.sectorPcm), nullptr, 1176);
}

void PicoAudio::decodeMp3Sector(uint32_t *i2sBuffer, const TrackEntry &tr, uint32_t relSector) {
    (void)tr;
    (void)relSector;
    if (!m_mp3Dec || !m_arena) {
        memset(i2sBuffer, 0, c_cdSamplesBytes * 2);
        return;
    }
    PlaybackBuffers &pb = m_arena->pb;

    const bool decodedThisSector = (m_pcmCount < 1176);
    bool eofReached = false;
    int guard = 0;
    while (m_pcmCount < 1176 && guard < 16) {
        guard++;
        if (m_mp3BytesLeft < MAINBUF_SIZE && !eofReached) {
            if (m_mp3BytesLeft > 0 && m_mp3ReadPtr != pb.mp3InBuf) {
                memmove(pb.mp3InBuf, m_mp3ReadPtr, static_cast<size_t>(m_mp3BytesLeft));
            }
            m_mp3ReadPtr = pb.mp3InBuf;
            UINT br = 0;
            const UINT space = static_cast<UINT>(PlaybackBuffers::kInBufSize - static_cast<size_t>(m_mp3BytesLeft));
            if (space > 0 && f_read(&m_arena->fp, pb.mp3InBuf + m_mp3BytesLeft, space, &br) == FR_OK && br > 0) {
                m_mp3BytesLeft += static_cast<int>(br);
            } else {
                eofReached = true;
            }
        }
        if (m_mp3BytesLeft < 4) break;

        int off = MP3FindSyncWord(m_mp3ReadPtr, m_mp3BytesLeft);
        if (off < 0) {
            if (eofReached) {
                m_mp3BytesLeft = 0;
                m_mp3ReadPtr = pb.mp3InBuf;
                break;
            }
            if (m_mp3BytesLeft > 3) {
                memmove(pb.mp3InBuf, m_mp3ReadPtr + m_mp3BytesLeft - 3, 3);
                m_mp3BytesLeft = 3;
            }
            m_mp3ReadPtr = pb.mp3InBuf;
            continue;
        }
        m_mp3ReadPtr += off;
        m_mp3BytesLeft -= off;

        int err = MP3Decode(static_cast<HMP3Decoder>(m_mp3Dec), &m_mp3ReadPtr, &m_mp3BytesLeft, pb.frameOut, 0);
        if (err != ERR_MP3_NONE) {
            if (err == ERR_MP3_MAINDATA_UNDERFLOW) {
                continue;
            }
            if (err == ERR_MP3_INDATA_UNDERFLOW) {
                if (eofReached) break;
                if (m_mp3ReadPtr != pb.mp3InBuf && m_mp3BytesLeft > 0) {
                    memmove(pb.mp3InBuf, m_mp3ReadPtr, static_cast<size_t>(m_mp3BytesLeft));
                }
                m_mp3ReadPtr = pb.mp3InBuf;
                UINT br = 0;
                const UINT space = static_cast<UINT>(PlaybackBuffers::kInBufSize - static_cast<size_t>(m_mp3BytesLeft));
                if (space > 0 && f_read(&m_arena->fp, pb.mp3InBuf + m_mp3BytesLeft, space, &br) == FR_OK && br > 0) {
                    m_mp3BytesLeft += static_cast<int>(br);
                    continue;
                }
                eofReached = true;
                break;
            }
            if (m_mp3BytesLeft > 0) {
                m_mp3ReadPtr++;
                m_mp3BytesLeft--;
            }
            continue;
        }

        MP3FrameInfo fi;
        MP3GetLastFrameInfo(static_cast<HMP3Decoder>(m_mp3Dec), &fi);
        if (fi.outputSamps <= 0 || fi.samprate <= 0) continue;

        if (fi.samprate == 44100) {
            // Fast 1:1 native Red Book 44.1 kHz path (zero resampling overhead!)
            if (fi.nChans == 2) {
                size_t samps = static_cast<size_t>(fi.outputSamps);
                if (m_pcmCount + samps > PlaybackBuffers::kPcmRingCap) {
                    samps = PlaybackBuffers::kPcmRingCap - m_pcmCount;
                }
                memcpy(pb.pcmRing + m_pcmCount, pb.frameOut, samps * sizeof(int16_t));
                m_pcmCount += samps;
            } else {
                for (int i = 0; i < fi.outputSamps && m_pcmCount + 2 <= PlaybackBuffers::kPcmRingCap; i++) {
                    pb.pcmRing[m_pcmCount++] = pb.frameOut[i];
                    pb.pcmRing[m_pcmCount++] = pb.frameOut[i];
                }
            }
        } else {
            // Lightweight Q16 linear resampler for 48 kHz / 32 kHz / 22.05 kHz / etc. -> 44.1 kHz
            const uint32_t nFrames = static_cast<uint32_t>(fi.nChans == 2 ? (fi.outputSamps / 2) : fi.outputSamps);
            const uint32_t stepQ16 = static_cast<uint32_t>((static_cast<uint64_t>(fi.samprate) << 16) / 44100ULL);
            const uint32_t limitQ16 = nFrames << 16;
            uint32_t phase = m_resampPhase;
            while (phase < limitQ16 && m_pcmCount + 2 <= PlaybackBuffers::kPcmRingCap) {
                const uint32_t idx = phase >> 16;
                const int32_t frac = static_cast<int32_t>((phase & 0xFFFFu) >> 8); // 0..255
                int32_t l0, r0, l1, r1;
                if (fi.nChans == 2) {
                    l1 = pb.frameOut[idx * 2];
                    r1 = pb.frameOut[idx * 2 + 1];
                    l0 = (idx == 0) ? m_resampPrevL : pb.frameOut[(idx - 1u) * 2];
                    r0 = (idx == 0) ? m_resampPrevR : pb.frameOut[(idx - 1u) * 2 + 1];
                } else {
                    l1 = r1 = pb.frameOut[idx];
                    l0 = r0 = (idx == 0) ? m_resampPrevL : pb.frameOut[idx - 1u];
                }
                pb.pcmRing[m_pcmCount++] = static_cast<int16_t>(l0 + (((l1 - l0) * frac) >> 8));
                pb.pcmRing[m_pcmCount++] = static_cast<int16_t>(r0 + (((r1 - r0) * frac) >> 8));
                phase += stepQ16;
            }
            if (nFrames > 0) {
                if (fi.nChans == 2) {
                    m_resampPrevL = pb.frameOut[(nFrames - 1u) * 2];
                    m_resampPrevR = pb.frameOut[(nFrames - 1u) * 2 + 1];
                } else {
                    m_resampPrevL = m_resampPrevR = pb.frameOut[nFrames - 1u];
                }
            }
            m_resampPhase = (phase >= limitQ16) ? (phase - limitQ16) : 0u;
        }
    }

    if (m_pcmCount >= 1176) {
        memcpy(pb.sectorPcm, pb.pcmRing, 1176 * sizeof(int16_t));
        m_pcmCount -= 1176;
        if (m_pcmCount > 0) {
            memmove(pb.pcmRing, pb.pcmRing + 1176, m_pcmCount * sizeof(int16_t));
        }
    } else {
        memset(pb.sectorPcm, 0, sizeof(pb.sectorPcm));
        if (m_pcmCount > 0) {
            memcpy(pb.sectorPcm, pb.pcmRing, m_pcmCount * sizeof(int16_t));
            m_pcmCount = 0;
        }
    }

    scramble_data(i2sBuffer, reinterpret_cast<uint16_t *>(pb.sectorPcm), nullptr, 1176);

    // Split SD I/O from MP3 decode! On alternating sectors where pcmRing ALREADY
    // satisfied I2S without calling MP3Decode(), top up the 16 KB mp3InBuf in
    // 4 KB (8 SD-sector) chunks while Core 1 has ~13 ms of idle headroom —
    // keeping 10..16 KB buffered so MP3Decode() NEVER stalls on SD SPI!
    if (!decodedThisSector && !eofReached && m_mp3BytesLeft <= static_cast<int>(PlaybackBuffers::kInBufSize - 4096u)) {
        const size_t consumed = static_cast<size_t>(m_mp3ReadPtr - pb.mp3InBuf);
        if (consumed >= 4096u) {
            if (m_mp3BytesLeft > 0) {
                memmove(pb.mp3InBuf, m_mp3ReadPtr, static_cast<size_t>(m_mp3BytesLeft));
            }
            m_mp3ReadPtr = pb.mp3InBuf;
        }
        const size_t tailOff = static_cast<size_t>(m_mp3ReadPtr - pb.mp3InBuf) + static_cast<size_t>(m_mp3BytesLeft);
        if (tailOff + 4096u <= PlaybackBuffers::kInBufSize) {
            UINT br = 0;
            if (f_read(&m_arena->fp, pb.mp3InBuf + tailOff, 4096u, &br) == FR_OK && br > 0) {
                m_mp3BytesLeft += static_cast<int>(br);
            }
        }
    }
}

void PicoAudio::readSector(uint32_t *i2sBuffer, const int sector) {
    // sector == currentSector - c_leadIn: 0..149 is Track 1 pregap (silence),
    // program area LBA 0 starts at sector == c_preGap (150 = 00:02:00)!
    if (!m_active || !m_arena || m_trackCount == 0 || sector < c_preGap) {
        memset(i2sBuffer, 0, c_cdSamplesBytes * 2);
        return;
    }

    const uint32_t progSec = static_cast<uint32_t>(sector - c_preGap);
    if (progSec >= m_totalSectors) {
        memset(i2sBuffer, 0, c_cdSamplesBytes * 2);
        return;
    }

    uint16_t tIdx = 0;
    for (uint16_t i = 0; i < m_trackCount; i++) {
        if (progSec >= m_arena->tracks[i].startSector) {
            tIdx = i;
        } else {
            break;
        }
    }

    const TrackEntry &tr = m_arena->tracks[tIdx];
    const uint32_t relSector = progSec - tr.startSector;

    const bool needSeek = (tr.format == PICO_AUDIO_FMT_MP3) &&
                          (relSector < m_expectedRelSector || relSector > m_expectedRelSector + 4u);

    if (!m_fpOpen || m_openTrackIdx != static_cast<int>(tIdx) || needSeek) {
        if (!openTrackFile(tIdx, relSector)) {
            memset(i2sBuffer, 0, c_cdSamplesBytes * 2);
            return;
        }
    }

    if (tr.format == PICO_AUDIO_FMT_WAV) {
        decodeWavSector(i2sBuffer, tr, relSector);
    } else {
        decodeMp3Sector(i2sBuffer, tr, relSector);
    }
    m_expectedRelSector = relSector + 1u;
}

}  // namespace picostation
