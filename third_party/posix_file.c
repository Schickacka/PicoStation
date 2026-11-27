/*

MIT License

Copyright (c) 2020 PCSX-Redux authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "posix_file.h"
#include "ff.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Pinned files (Track 1 data + .cue) own a heap-allocated FIL; lazy CDDA tracks
// (Tracks 2..99, e.g. 57-track Tomb Raider) share a single static s_lazyFil so
// each lazy track uses only 24 bytes of heap instead of 584 bytes!
typedef struct PosixFil {
    FIL *fp;              // Points to owned FIL (if pinned) or &s_lazyFil (if open lazy)
    uint8_t clmtTried;
    uint8_t isOpen;
    uint8_t pinned;       // Track 1 (data) & .cue stay pinned open permanently
    uint64_t cachedSize;  // Exact size from single-pass f_readdir() for lazy tracks
    char *path;           // Heap-allocated full path for lazy open/reopen
} PosixFil;

// Bounded lazy audio handle cache: keep at most 1 non-pinned track open at a time
// so 57..99-track CDDA games (e.g. Tomb Raider) mount in ~15 ms with tiny RAM use!
static PosixFil *s_activeLazyFile = NULL;
static FIL s_lazyFil;

static void synthesize_exfat_clmt(PosixFil *pf) {
    FIL *fp = pf->fp;
    if (!fp) return;
    fp->cltbl = NULL;
#if FF_FS_EXFAT
    if (fp->obj.objsize > 65536u && fp->obj.sclust != 0 &&
        fp->obj.fs && fp->obj.fs->fs_type == FS_EXFAT && fp->obj.stat == 2)
    {
#if FF_MAX_SS != FF_MIN_SS
        const FSIZE_t sectorSize = (FSIZE_t)fp->obj.fs->ssize;
#else
        const FSIZE_t sectorSize = (FSIZE_t)FF_MAX_SS;
#endif
        const FSIZE_t clusterBytes = (FSIZE_t)fp->obj.fs->csize * sectorSize;
        if (clusterBytes > 0)
        {
            const DWORD clusterCount = (DWORD)((fp->obj.objsize + clusterBytes - 1u) / clusterBytes);
            DWORD *cltbl = (DWORD *)malloc(4 * sizeof(DWORD));
            if (cltbl)
            {
                cltbl[0] = 4;
                cltbl[1] = clusterCount;
                cltbl[2] = fp->obj.sclust;
                cltbl[3] = 0;
                fp->cltbl = cltbl;
            }
            pf->clmtTried = 1;
        }
    }
#endif
}

static void close_fil_handle(PosixFil *pf) {
    if (!pf || !pf->isOpen)
    {
        return;
    }
    FIL *fp = pf->fp;
    if (fp)
    {
        if (fp->cltbl)
        {
            free(fp->cltbl);
            fp->cltbl = NULL;
        }
        f_close(fp);
    }
    if (!pf->pinned)
    {
        pf->fp = NULL;
    }
    pf->isOpen = 0;
    pf->clmtTried = 0;
}

static void posix_destroy(struct CueFile *file) { (void)file; }

static void posix_close(struct CueFile *file, struct CueScheduler *scheduler, void (*cb)(struct CueFile *, struct CueScheduler *)) {
    PosixFil *pf = (PosixFil *)file->opaque;

    if (!pf)
    {
        return;
    }

    if (s_activeLazyFile == pf)
    {
        s_activeLazyFile = NULL;
    }

    close_fil_handle(pf);
    if (pf->pinned && pf->fp)
    {
        free(pf->fp);
        pf->fp = NULL;
    }
    if (pf->path)
    {
        free(pf->path);
        pf->path = NULL;
    }
    free(pf);
    file->opaque = NULL;

    if (scheduler)
    {
        File_schedule_close(file, scheduler, cb);
    }
}

uint64_t posix_file_get_size(const struct CueFile *file) {
    if (!file || !file->opaque)
    {
        return 0;
    }
    const PosixFil *pf = (const PosixFil *)file->opaque;
    return (pf->isOpen && pf->fp) ? (uint64_t)f_size(pf->fp) : pf->cachedSize;
}

FIL *posix_file_get_fil(struct CueFile *file) {
    if (!file || !file->opaque)
    {
        return NULL;
    }
    PosixFil *pf = (PosixFil *)file->opaque;
    return pf->isOpen ? pf->fp : NULL;
}

bool posix_file_ensure_open(struct CueFile *file) {
    if (!file || !file->opaque)
    {
        return false;
    }

    PosixFil *pf = (PosixFil *)file->opaque;
    if (pf->isOpen && pf->fp)
    {
        return true;
    }

    if (!pf->path)
    {
        return false;
    }

    if (s_activeLazyFile && s_activeLazyFile != pf && !s_activeLazyFile->pinned)
    {
        close_fil_handle(s_activeLazyFile);
        s_activeLazyFile = NULL;
    }

    FIL *fp = pf->pinned ? pf->fp : &s_lazyFil;
    if (!fp)
    {
        return false;
    }
    memset(fp, 0, sizeof(FIL));
    pf->clmtTried = 0;
    if (f_open(fp, pf->path, FA_READ) != FR_OK)
    {
        return false;
    }

    pf->fp = fp;
    pf->isOpen = 1;
    pf->cachedSize = (uint64_t)f_size(fp);
    synthesize_exfat_clmt(pf);
    if (!pf->pinned)
    {
        s_activeLazyFile = pf;
    }
    return true;
}

static void posix_size(struct CueFile *file, struct CueScheduler *scheduler, int compressed,
                       void (*cb)(struct CueFile *, struct CueScheduler *, uint64_t)) {
    (void)compressed;
    uint64_t size = posix_file_get_size(file);
    File_schedule_size(file, scheduler, size, cb);
}

static void posix_read(struct CueFile *file, struct CueScheduler *scheduler, uint32_t amount, uint64_t cursor,
                       uint8_t *buffer,
                       void (*cb)(struct CueFile *, struct CueScheduler *, int error, uint32_t amount, uint8_t *buffer)) {
    if (!posix_file_ensure_open(file))
    {
        File_schedule_read(file, scheduler, 1, 0, buffer, cb);
        return;
    }

    FIL *f = posix_file_get_fil(file);
    if (!f)
    {
        File_schedule_read(file, scheduler, 1, 0, buffer, cb);
        return;
    }
    size_t r = 0;
    f_lseek(f, cursor);
    int ret = f_read(f, buffer, amount, &r);

    File_schedule_read(file, scheduler, ret == 0 ? 0 : 1, (uint32_t)r, buffer, cb);
}

static void posix_write(struct CueFile *file, struct CueScheduler *scheduler, uint32_t amount, uint64_t cursor,
                        const uint8_t *buffer,
                        void (*cb)(struct CueFile *, struct CueScheduler *, int error, uint32_t amount)) {
#if FF_FS_READONLY == 0
    if (!posix_file_ensure_open(file))
    {
        File_schedule_write(file, scheduler, 1, 0, cb);
        return;
    }
    FIL *f = posix_file_get_fil(file);
    if (!f)
    {
        File_schedule_write(file, scheduler, 1, 0, cb);
        return;
    }
    size_t r = 0;
    f_lseek(f, cursor);
    int ret = f_write(f, buffer, amount, &r);

    File_schedule_write(file, scheduler, ret == 0 ? 0 : 1, (uint32_t)r, cb);
#else
    (void)file;
    (void)scheduler;
    (void)amount;
    (void)cursor;
    (void)buffer;
    (void)cb;
#endif
}

void posix_file_ensure_fastseek(struct CueFile *file) {
    if (!file || !file->opaque)
    {
        return;
    }

    PosixFil *pf = (PosixFil *)file->opaque;
    if (!pf->isOpen || !pf->fp)
    {
        return;
    }
    FIL *fp = pf->fp;
    if (fp->cltbl || pf->clmtTried || fp->obj.objsize <= 65536u || fp->obj.sclust == 0)
    {
        return;
    }

    pf->clmtTried = 1;

    static const size_t c_probeEntries = 64;
    static const size_t c_maxCltblEntries = 512;
    DWORD probe[64];
    probe[0] = (DWORD)c_probeEntries;
    fp->cltbl = probe;

    FRESULT r = f_lseek(fp, CREATE_LINKMAP);
    if (r == FR_OK)
    {
        const size_t count = probe[0];
        if (count >= 2 && count <= c_probeEntries)
        {
            DWORD *cltbl = (DWORD *)malloc(count * sizeof(DWORD));
            if (cltbl)
            {
                memcpy(cltbl, probe, count * sizeof(DWORD));
                fp->cltbl = cltbl;
            }
            else
            {
                fp->cltbl = NULL;
            }
        }
        else
        {
            fp->cltbl = NULL;
        }
    }
    else if (r == FR_NOT_ENOUGH_CORE)
    {
        const size_t count = probe[0];
        if (count >= 2 && count <= c_maxCltblEntries)
        {
            DWORD *cltbl = (DWORD *)malloc(count * sizeof(DWORD));
            if (cltbl)
            {
                memset(cltbl, 0, count * sizeof(DWORD));
                cltbl[0] = (DWORD)count;
                fp->cltbl = cltbl;
                r = f_lseek(fp, CREATE_LINKMAP);
                if (r != FR_OK)
                {
                    free(cltbl);
                    fp->cltbl = NULL;
                }
            }
            else
            {
                fp->cltbl = NULL;
            }
        }
        else
        {
            fp->cltbl = NULL;
        }
    }
    else
    {
        fp->cltbl = NULL;
    }
}

struct CueFile *create_posix_file(struct CueFile *file, const char *filename, uint8_t mode) {
    /* The cue parser calls file->destroy() on failure, so make every vtable
       entry valid before attempting to open the file. */
    file->opaque = NULL;
    file->destroy = posix_destroy;
    file->close = posix_close;
    file->size = posix_size;
    file->read = posix_read;
    file->write = posix_write;
    file->cfilename = NULL;
    file->filename = NULL;
    file->references = 1;

    PosixFil *pf = (PosixFil *)malloc(sizeof(PosixFil));
    if (!pf)
    {
        return NULL;
    }
    memset(pf, 0, sizeof(PosixFil));
    FIL *fp = (FIL *)malloc(sizeof(FIL));
    if (!fp)
    {
        free(pf);
        return NULL;
    }
    memset(fp, 0, sizeof(FIL));

    if (f_open(fp, filename, mode) != FR_OK)
    {
        free(fp);
        free(pf);
        return NULL;
    }

    pf->fp = fp;
    pf->isOpen = 1;
    pf->pinned = 1;
    pf->cachedSize = (uint64_t)f_size(fp);
    pf->path = NULL;
    synthesize_exfat_clmt(pf);

    file->opaque = pf;
    return file;
}

struct CueFile *create_posix_file_lazy(struct CueFile *file, const char *filename, uint64_t cachedSize) {
    file->opaque = NULL;
    file->destroy = posix_destroy;
    file->close = posix_close;
    file->size = posix_size;
    file->read = posix_read;
    file->write = posix_write;
    file->cfilename = NULL;
    file->filename = NULL;
    file->references = 1;

    if (!filename || !filename[0])
    {
        return NULL;
    }

    PosixFil *pf = (PosixFil *)malloc(sizeof(PosixFil));
    if (!pf)
    {
        return NULL;
    }
    memset(pf, 0, sizeof(PosixFil));

    const size_t len = strlen(filename) + 1u;
    pf->path = (char *)malloc(len);
    if (!pf->path)
    {
        free(pf);
        return NULL;
    }
    memcpy(pf->path, filename, len);

    pf->isOpen = 0;
    pf->pinned = 0;
    pf->cachedSize = cachedSize;

    file->opaque = pf;
    return file;
}
