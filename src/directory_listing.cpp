#define _GNU_SOURCE
#include "directory_listing.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "global.h"
#include "ff.h"
#include "listingBuilder.h"
#include "logging.h"

#if DEBUG_FILEIO
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

namespace picostation
{

namespace
{
    char currentDirectory[c_maxFilePathLength + 1];
    listingBuilder* fileListing;
}  // namespace

static FIL cover_fp;
static FIL cheat_fp;
static FIL cfg_fp;
static FIL bg_fp;
static FATFS s_fatFS;
static bool sd_present = false;
static bool s_dirCountValid = false;
static uint16_t s_cachedDirCount = 0;
static uint16_t s_transferBuf[1162];

static bool __time_critical_func(matchesListingFilter)(const FILINFO &entry)
{
    if (entry.fattrib & AM_HID)
    {
        return false;
    }
    if (entry.fattrib & AM_DIR)
    {
        // Hide internal firmware update folder from the root PicoMenu listing
        if (currentDirectory[0] == '\0' && strcasecmp(entry.fname, "UPDATE") == 0)
        {
            return false;
        }
        return true;
    }
    return strcasestr(entry.fname, ".cue") != nullptr;
}

void __time_critical_func(DirectoryListing::init)()
{
    sd_present = false;
    
    memset(&cover_fp, 0, sizeof(FIL));
    memset(&cheat_fp, 0, sizeof(FIL));
    memset(&cfg_fp, 0, sizeof(FIL));
    memset(&bg_fp, 0, sizeof(FIL));
    
    FRESULT fr = f_mount(&s_fatFS, "", 1);
	
	if (FR_OK == fr)
	{
		sd_present = true;
		//panic("f_mount error: (%d)\n", fr);
	}
    
    fileListing = new listingBuilder();
    gotoRoot();
}

void __time_critical_func(DirectoryListing::gotoRoot)()
{ 
    currentDirectory[0] = '\0';
    s_dirCountValid = false;
}

bool __time_critical_func(DirectoryListing::gotoDirectory)(const uint32_t index)
{ 
    char newFolder[c_maxFilePathLength + 1];
    bool result = getDirectoryEntry(index, newFolder); 
    
    if (result)
    {
        combinePaths(currentDirectory, newFolder, currentDirectory);
        s_dirCountValid = false;
    }
    
    DEBUG_PRINT("gotoDirectory: %s\n", currentDirectory);
    return result;
}

bool __time_critical_func(DirectoryListing::getPath)(const uint32_t index, char* filePath)
{ 
    char newFolder[c_maxFilePathLength + 1];
    bool result = getDirectoryEntry(index, newFolder); 
    
    if (result)
    {
        combinePaths(currentDirectory, newFolder, filePath);
    }
    
    return result;
}

void __time_critical_func(DirectoryListing::gotoParentDirectory)()
{
    s_dirCountValid = false;
    uint32_t length = strnlen(currentDirectory, c_maxFilePathLength);
    if (length == 0)
    {
        return;
    }

    uint32_t position = length - 1;

    while (position > 0)
    {
        if (currentDirectory[position] == '/')
        {
            currentDirectory[position] = '\0';
            return;
        }
        
        currentDirectory[position] = '\0';
        position--;
    }

    currentDirectory[0] = '\0';
}

bool __time_critical_func(DirectoryListing::getDirectoryEntries)(const uint32_t offset)
{
    DIR dir;
    FILINFO currentEntry;
    FILINFO nextEntry;
    
    if (!sd_present)
	{
		fileListing->clear();
		fileListing->addString("LST1", 1);
		fileListing->addString("No SD Card", 1);
		fileListing->addTerminator(0, 1);
		return true;
	}
    
    FRESULT res = f_opendir(&dir, currentDirectory);
    
    if (res != FR_OK)
    {
        DEBUG_PRINT("f_opendir error: (%d)\n", res);
        return false;
    }

    fileListing->clear();
    fileListing->addString("LST1", 1);

    uint16_t fileEntryCount = 0;
    uint16_t filesProcessed = 0;
    bool hasNext = false;
	currentEntry.fname[0] = '\0';
	
    res = f_readdir(&dir, &currentEntry);
    if (res == FR_OK && currentEntry.fname[0] != '\0')
    {
        res = f_readdir(&dir, &nextEntry);
        hasNext = (res == FR_OK && nextEntry.fname[0] != '\0');
        
        while (true)
        {
            if (matchesListingFilter(currentEntry))
            {
                if (filesProcessed >= offset)
                {
                    if (fileListing->addString(currentEntry.fname, currentEntry.fattrib & AM_DIR ? 1 : 0) == false)
                    {
                        break;
                    }
                    fileEntryCount++;
                }
                
                filesProcessed++;
                
                if (filesProcessed >= 4096)
                {
                    hasNext = 0;
                }
            }
            
            if (hasNext == 0)
            {
                break;
            }
            
            currentEntry = nextEntry;
            res = f_readdir(&dir, &nextEntry);
            hasNext = (res == FR_OK && nextEntry.fname[0] != '\0');
        }
    }
    
    if (offset == 0)
    {
		uint16_t totalCount = getDirectoryEntriesCount();
		if (totalCount)
		{
			fileListing->addTerminator(hasNext ? 1 : 0, totalCount);
		}
		else
		{
			fileListing->addTerminator(0, 0xFEFE);
		}
        DEBUG_PRINT("file count: %d\n", totalCount);
    }
    else if (fileEntryCount)
    {
		fileListing->addTerminator(hasNext ? 1 : 0, fileEntryCount);
		DEBUG_PRINT("Entry count: %d\n", fileEntryCount);
    }
    else
	{
		DEBUG_PRINT("Empty directory\n");
		fileListing->addTerminator(0, 0xFEFE);
	}
    
    f_closedir(&dir);
    return true;
}

uint16_t __time_critical_func(DirectoryListing::getDirectoryEntriesCount)()
{
    if (s_dirCountValid)
    {
        return s_cachedDirCount;
    }

    DIR dir;
    FILINFO currentEntry;
    FILINFO nextEntry;
    
	if (!sd_present)
	{
		return 0;
	}
    
    FRESULT res = f_opendir(&dir, currentDirectory);
    
    if (res != FR_OK)
    {
        DEBUG_PRINT("f_opendir error: (%d)\n", res);
        return 0;
    }

    uint16_t fileEntryCount = 0;
    bool hasNext = false;

    res = f_readdir(&dir, &currentEntry);
    
    if (res == FR_OK && currentEntry.fname[0] != '\0')
    {
        res = f_readdir(&dir, &nextEntry);
        hasNext = (res == FR_OK && nextEntry.fname[0] != '\0');
        
        while (true)
        {
            if (matchesListingFilter(currentEntry))
            {
				fileEntryCount++;
				
				if (fileEntryCount >= 4096)
				{
					hasNext = 0;
				}
            }
            
            if (hasNext == 0)
            {
                break;
            }
            
            currentEntry = nextEntry;
            res = f_readdir(&dir, &nextEntry);
            hasNext = (res == FR_OK && nextEntry.fname[0] != '\0');
        }
    }
    
    f_closedir(&dir);
    s_cachedDirCount = fileEntryCount;
    s_dirCountValid = true;
    return fileEntryCount;
}

uint16_t* __time_critical_func(DirectoryListing::getFileListingData)()
{
    return fileListing->getData();
}

// Private

void __time_critical_func(DirectoryListing::combinePaths)(const char* filePath1, const char* filePath2, char* newPath)
{ 
    if (!filePath1 || !filePath2 || !newPath)
    {
        return;
    }

    // Build the result directly into newPath (which may alias filePath1) and
    // always keep it NUL-terminated. The previous strncpy/strncat chain could
    // run past the end of a 256-byte buffer for long paths.
    constexpr size_t c_maxLen = c_maxFilePathLength;

    size_t length = strnlen(filePath1, c_maxLen);
    if (length >= c_maxLen)
    {
        length = c_maxLen - 1;
    }

    memmove(newPath, filePath1, length);
    newPath[length] = '\0';

    if (length > 0 && (length + 1) < c_maxLen)
    {
        newPath[length++] = '/';
    }

    const size_t remaining = c_maxLen - length;
    const size_t secondLength = strnlen(filePath2, remaining);

    memcpy(newPath + length, filePath2, secondLength);
    newPath[length + secondLength] = '\0';
}

bool __time_critical_func(DirectoryListing::getDirectoryEntry)(const uint32_t index, char* filePath)
{
    if (index >= 4096)
    {
        return false;
    }

    DIR dir;
    FILINFO currentEntry;
    FILINFO nextEntry;
    FRESULT res = f_opendir(&dir, currentDirectory);
    
    if (res != FR_OK)
    {
        DEBUG_PRINT("f_opendir error: (%d)\n", res);
        return false;
    }

    uint32_t filesProcessed = 0;

    res = f_readdir(&dir, &currentEntry);
    
    if (res == FR_OK && currentEntry.fname[0] != '\0')
    {
        res = f_readdir(&dir, &nextEntry);
        bool hasNext = (res == FR_OK && nextEntry.fname[0] != '\0');
        
        while (true)
        {
            if (matchesListingFilter(currentEntry))
            {
                if (filesProcessed == index)
                {
                    const size_t nameLength = strnlen(currentEntry.fname, c_maxFilePathLength);
                    memcpy(filePath, currentEntry.fname, nameLength);
                    filePath[nameLength] = '\0';
                    f_closedir(&dir);
                    return true;
                }
                
                filesProcessed++;
            }
            
            if (!hasNext)
            {
                break;
            }
            
            currentEntry = nextEntry;
            res = f_readdir(&dir, &nextEntry);
            hasNext = (res == FR_OK && nextEntry.fname[0] != '\0');
        }
    }

    f_closedir(&dir);
    return false;
}

void __time_critical_func(DirectoryListing::openCover)(const uint32_t index)
{
    char filePath[c_maxFilePathLength + 1];
    
    if (cover_fp.obj.fs)
	{
		f_close(&cover_fp);
	}
	
    if (!sd_present)
    {
		return;
	}
	
    if (!getPath(index, filePath))
    {
        return;
    }

    size_t len = strnlen(filePath, c_maxFilePathLength);

    if (len >= 4 && strcasecmp(filePath + len - 4, ".cue") == 0) // is cue file
	{
        len -= 4;
	}
	
    if (len + 4 > c_maxFilePathLength)
    {
        return;
    }

    memcpy(filePath + len, ".cov", 5);
    
	DEBUG_PRINT("openCover: %s\n", filePath);

	f_open(&cover_fp, filePath, FA_READ);
}

uint16_t* __time_critical_func(DirectoryListing::readCover)(const uint32_t part)
{
	UINT readed = 0;
	
	memset(s_transferBuf, 0, sizeof(s_transferBuf));

	if (!cover_fp.obj.fs)
	{
		DEBUG_PRINT("readCover: not found\n");
		s_transferBuf[0] = 'D' | ('E' << 8);
		s_transferBuf[1] = 'A' | ('D' << 8);
		return s_transferBuf;
	}
	
	f_lseek(&cover_fp, part * 2114);
	
	f_read(&cover_fp, &s_transferBuf[3], 2114, &readed);
	s_transferBuf[0] = 'P' | ('A' << 8);
	s_transferBuf[1] = 'R' | ('T' << 8);
	s_transferBuf[2] = static_cast<uint16_t>(part);
	
	DEBUG_PRINT("readCover: part %lu\n", static_cast<unsigned long>(part));
	
	return s_transferBuf;
}

void __time_critical_func(DirectoryListing::openCheat)(const uint32_t index)
{
    char filePath[c_maxFilePathLength + 1];

    if (cheat_fp.obj.fs)
    {
        f_close(&cheat_fp);
    }

    if (!sd_present)
    {
        return;
    }

    if (!getPath(index, filePath))
    {
        return;
    }

    const size_t len = strnlen(filePath, c_maxFilePathLength);
    if (len < 4 || filePath[len - 4] != '.')
    {
        return;
    }

    memcpy(filePath + len - 4, ".cht", 5);
    DEBUG_PRINT("openCheat: %s\n", filePath);
    f_open(&cheat_fp, filePath, FA_READ);
}

uint16_t* __time_critical_func(DirectoryListing::readCheat)(const uint32_t part)
{
    UINT readed = 0;

    memset(s_transferBuf, 0, sizeof(s_transferBuf));

    if (!cheat_fp.obj.fs)
    {
        s_transferBuf[0] = 'C' | ('H' << 8);
        s_transferBuf[1] = 'T' | ('0' << 8);
        return s_transferBuf;
    }

    f_lseek(&cheat_fp, static_cast<FSIZE_t>(part) * 2048u);
    f_read(&cheat_fp, &s_transferBuf[3], 2048, &readed);
    s_transferBuf[0] = 'C' | ('H' << 8);
    s_transferBuf[1] = 'T' | ('1' << 8);
    s_transferBuf[2] = static_cast<uint16_t>(readed);
    return s_transferBuf;
}

void __time_critical_func(DirectoryListing::openBg)(void)
{
    if (bg_fp.obj.fs)
    {
        f_close(&bg_fp);
    }

    if (!sd_present)
    {
        return;
    }

    DEBUG_PRINT("openBg\n");
    f_open(&bg_fp, "background.raw", FA_READ);
}

uint16_t* __time_critical_func(DirectoryListing::readBg)(const uint32_t part)
{
    UINT readed = 0;

    memset(s_transferBuf, 0, sizeof(s_transferBuf));

    if (!bg_fp.obj.fs)
    {
        s_transferBuf[0] = 'D' | ('E' << 8);
        s_transferBuf[1] = 'A' | ('D' << 8);
        return s_transferBuf;
    }

    f_lseek(&bg_fp, static_cast<FSIZE_t>(part) * 2316u);
    f_read(&bg_fp, &s_transferBuf[4], 2316, &readed);
    s_transferBuf[0] = 'P' | ('A' << 8);
    s_transferBuf[1] = 'R' | ('T' << 8);
    s_transferBuf[2] = static_cast<uint16_t>(part);
    s_transferBuf[3] = static_cast<uint16_t>(readed);
    return s_transferBuf;
}

void __time_critical_func(DirectoryListing::openCfg)(void)
{
    if (cfg_fp.obj.fs)
	{
		f_close(&cfg_fp);
	}
	
    if (!sd_present)
    {
		return;
	}
	
	DEBUG_PRINT("openCfg\n");

	f_open(&cfg_fp, "config.ini", FA_READ);
}

uint16_t* __time_critical_func(DirectoryListing::readCfg)(void)
{
	UINT readed = 0;
	
	memset(s_transferBuf, 0, sizeof(s_transferBuf));
	
	if (!cfg_fp.obj.fs)
	{
		DEBUG_PRINT("readCfg: not found\n");
		s_transferBuf[0] = 'D' | ('E' << 8);
		s_transferBuf[1] = 'A' | ('D' << 8);
		return s_transferBuf;
	}
	
	f_lseek(&cfg_fp, 0);
	
	f_read(&cfg_fp, &s_transferBuf[138], 2048, &readed);
	s_transferBuf[0] = 'C' | ('F' << 8);
	s_transferBuf[1] = 'G' | ('1' << 8);
	s_transferBuf[2] = static_cast<uint16_t>(readed);
	
	DEBUG_PRINT("readCfg: readed %u\n", readed);
	
	return s_transferBuf;
}

}  // namespace picostation

