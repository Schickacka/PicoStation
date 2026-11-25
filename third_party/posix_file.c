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

typedef struct {
    FIL fp;
    uint8_t clmtTried;
} PosixFil;

static void posix_destroy(struct CueFile *file) {}

static void posix_close(struct CueFile *file, struct CueScheduler *scheduler, void (*cb)(struct CueFile *, struct CueScheduler *)) {
    PosixFil *pf = (PosixFil *)file->opaque;
    
    if (!pf)
	{
		return;
	}

    FIL *fp = &pf->fp;
    if (fp->cltbl)
    {
		free(fp->cltbl);
	}
    f_close(fp);
    free(pf);
    file->opaque = NULL;
    
    if (scheduler)
	{
		File_schedule_close(file, scheduler, cb);
	}
}

static void posix_size(struct CueFile *file, struct CueScheduler *scheduler, int compressed,
                       void (*cb)(struct CueFile *, struct CueScheduler *, uint64_t)) {
    FIL *f = (FIL *)file->opaque;
    uint64_t size = f_size(f);
    File_schedule_size(file, scheduler, size, cb);
}

static void posix_read(struct CueFile *file, struct CueScheduler *scheduler, uint32_t amount, uint64_t cursor,
                       uint8_t *buffer,
                       void (*cb)(struct CueFile *, struct CueScheduler *, int error, uint32_t amount, uint8_t *buffer)) {
    
    FIL *f = (FIL *)file->opaque;
    size_t r;
    f_lseek(f, cursor);
    int ret = f_read(f, buffer, amount, &r);
    
    File_schedule_read(file, scheduler, ret == 0 ? 0 : 1, r, buffer, cb);
}

static void posix_write(struct CueFile *file, struct CueScheduler *scheduler, uint32_t amount, uint64_t cursor,
                        const uint8_t *buffer,
                        void (*cb)(struct CueFile *, struct CueScheduler *, int error, uint32_t amount)) {
#if FF_FS_READONLY == 0
    FIL *f = (FIL *)file->opaque;
    size_t r;
    f_lseek(f, cursor);
    int ret = f_write(f, buffer, amount, &r);
    
    File_schedule_write(file, scheduler, ret == 0 ? 0 : 1, r, cb);
#endif
}

void posix_file_ensure_fastseek(struct CueFile *file) {
    if (!file || !file->opaque)
    {
        return;
    }

    PosixFil *pf = (PosixFil *)file->opaque;
    FIL *fp = &pf->fp;
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
    pf->clmtTried = 0;
    FIL *fp = &pf->fp;
	
    if(f_open(fp, filename, mode))
    {
		free(pf);
		return NULL;
	}
    
    /* Never block MOUNT_FILE walking FAT tables for 1..57 .bin files while the
       PS1 BIOS is timing out waiting for SCEX/Lead-In!
       - On contiguous exFAT files (stat == 2, NoFatChain), synthesize the
         4-DWORD CLMT in O(1) right now with zero SD reads.
       - On FAT32 / fragmented exFAT, defer CREATE_LINKMAP lazily to
         posix_file_ensure_fastseek() on the first non-trivial seek. */
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
    
    file->opaque = pf;
    file->destroy = posix_destroy;
    file->close = posix_close;
    file->size = posix_size;
    file->read = posix_read;
    file->write = posix_write;
    file->cfilename = NULL;
    file->filename = NULL;
    file->references = 1;
    return file->opaque ? file : NULL;
}
