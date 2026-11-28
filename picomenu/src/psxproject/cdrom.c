#include "stdatomic.h"
#include "ps1/cdrom.h"
#include "cdrom.h"

#include "string.h"

#include "ps1/registers.h"
#include "filesystem.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "delay.h"
#include "system.h"
#include "../logging.h"

#if DEBUG_CDROM
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

volatile bool waitingForInt1;
volatile bool waitingForInt2;
volatile bool waitingForInt3;
volatile bool waitingForInt4;
volatile bool waitingForInt5;
volatile bool cdromBusy = false;
static volatile bool s_waitingPauseInt2 = false;

bool cdromDataReady;

void  *cdromReadDataPtr;
size_t cdromReadDataSectorSize;
volatile int cdromReadDataNumSectors;

uint8_t cdromResponse[16];
uint8_t cdromRespLength;
uint8_t cdromStatus;

uint8_t cdromLastReadPurpose;

#define toBCD(i) (((i) / 10 * 16) | ((i) % 10))

#define CDROM_BUSY (CDROM_HSTS & CDROM_HSTS_BUSYSTS)

void initCDROM(void) {
    BIU_DEV5_CTRL = 0x00020943; // Configure bus
    DMA_DPCR |= DMA_DPCR_CH_ENABLE(DMA_CDROM); // Enable DMA

    CDROM_ADDRESS = 1;
    CDROM_HCLRCTL = 0 // Acknowledge all IRQs
        | CDROM_HCLRCTL_CLRINT0
        | CDROM_HCLRCTL_CLRINT1
        | CDROM_HCLRCTL_CLRINT2;
    CDROM_HINTMSK_W = 0 // Enable all IRQs
        | CDROM_HCLRCTL_CLRINT0
        | CDROM_HCLRCTL_CLRINT1
        | CDROM_HCLRCTL_CLRINT2;

    CDROM_ADDRESS = 0;
    CDROM_HCHPCTL = 0; // Clear pending requests

    CDROM_ADDRESS = 2;
    CDROM_ATV0 = 128; // Send left audio channel to SPU left channel
    CDROM_ATV1 = 0;

    CDROM_ADDRESS = 3;
    CDROM_ATV2 = 128; // Send right audio channel to SPU right channel
    CDROM_ATV3 = 0;
    CDROM_ADPCTL = CDROM_ADPCTL_CHNGATV;
}

void issueCDROMCommand(uint8_t cmd, const uint8_t *arg, size_t argLength) {
    /* Wait briefly for HC05 busy flag to clear (bounded — never hang forever) */
    for (int w = 0; w < 2000 && CDROM_BUSY; w++) {
        delayMicroseconds(100);
    }

    /* Atomic critical section around bank switching, parameter FIFO writes and
     * command dispatch so an incoming CDROM IRQ can NEVER change CDROM_ADDRESS
     * or race with waitingForInt flags! */
    bool irqWasEnabled = disableInterrupts();

    waitingForInt1 = true;
    waitingForInt2 = true;
    waitingForInt3 = true;
    waitingForInt4 = true;
    waitingForInt5 = true;
    cdromStatus = 0;
    cdromDataReady = false;

    /* Ensure CD-ROM IRQ mask is always armed and drain any stale IRQ/result */
    CDROM_ADDRESS = 1;
    CDROM_HINTMSK_W = CDROM_HCLRCTL_CLRINT0 | CDROM_HCLRCTL_CLRINT1 | CDROM_HCLRCTL_CLRINT2;
    if (CDROM_HINTSTS & 0x07u) {
        CDROM_HCLRCTL = CDROM_HCLRCTL_CLRINT0 | CDROM_HCLRCTL_CLRINT1 | CDROM_HCLRCTL_CLRINT2;
        while (CDROM_HSTS & CDROM_HSTS_RSLRRDY) {
            (void) CDROM_RESULT;
        }
    }
    CDROM_HCLRCTL = CDROM_HCLRCTL_CLRPRM; // Clear parameter buffer
    delayMicroseconds(20);

    for (int w = 0; w < 500 && CDROM_BUSY; w++) {
        delayMicroseconds(50);
    }

    CDROM_ADDRESS = 0;
    for (; argLength > 0; argLength--)
        CDROM_PARAMETER = *(arg++);

    CDROM_COMMAND = cmd;

    if (irqWasEnabled) {
        enableInterrupts();
    }
}

void waitForINT1(){
    for (int t = 0; t < 25000 && waitingForInt1 && waitingForInt5; t++) {
        delayMicroseconds(100);
    }
    if (waitingForInt1 && waitingForInt5) {
        waitingForInt5 = false;
    }
}

void waitForINT2(){
    for (int t = 0; t < 25000 && waitingForInt2 && waitingForInt5; t++) {
        delayMicroseconds(100);
    }
    if (waitingForInt2 && waitingForInt5) {
        waitingForInt5 = false;
    }
}

void waitForINT3(){
    for (int t = 0; t < 15000 && waitingForInt3 && waitingForInt5; t++) {
        delayMicroseconds(100);
    }
    if (waitingForInt3 && waitingForInt5) {
        waitingForInt5 = false;
    }
}

void waitForINT5(){
    while(waitingForInt5){
        __asm__ volatile("");
    }
}


/// @brief 
/// @param lba LBA of the sector to read
/// @param ptr Pointer to buffer to store read data
/// @param numSectors Number of sectors to read
/// @param sectorSize Size of sector (2048)
/// @param doubleSpeed Read at double speed
/// @param wait Block until read completed

void startCDROMRead(uint32_t lba, void *ptr, size_t numSectors, size_t sectorSize, bool doubleSpeed, bool wait)
{
    for (int w = 0; w < 5000 && cdromBusy; w++)
    {
        delayMicroseconds(100);
    }
    cdromBusy = true;
    s_waitingPauseInt2 = false;

    cdromReadDataPtr = ptr;
    cdromReadDataNumSectors = (int)numSectors;
    cdromReadDataSectorSize = sectorSize;

	uint8_t mode = 0;
    CDROMMSF     msf;

    if (sectorSize == 2340)
        mode |= CDROM_MODE_SIZE_2340 ;
    if (doubleSpeed)
        mode |= CDROM_MODE_SPEED_2X;

    cdrom_convertLBAToMSF(&msf, lba);
    delayMicroseconds(100);

    //DEBUG_PRINT("LBA Set: %d (%02x:%02x:%02x), issue setmode\n", lba, msf.minute, msf.second, msf.frame);
    issueCDROMCommand(CDROM_CMD_SETMODE, &mode, sizeof(mode));
    waitForINT3();
    //DEBUG_PRINT("Issue SETLOC\n");
    issueCDROMCommand(CDROM_CMD_SETLOC, (const uint8_t *)&msf, sizeof(msf));
    waitForINT3();
    //DEBUG_PRINT("Issue CDREAD\n");
    issueCDROMCommand(CDROM_CMD_READ_N, NULL, 0);
    waitForINT3();
	
	if (!waitingForInt5)
	{
        s_waitingPauseInt2 = false;
        cdromBusy = false;
		return;
	}
	
    if ( !waitingForInt1 ) DEBUG_PRINT(" what's going on!?\n");

    if (wait)
    {
        int timeout = 0;
        while (cdromBusy && waitingForInt5 && timeout < 25000)
        {
            // Wait until INT1 DMA + PAUSE INT2 (or INT5 error) complete cleanly
            delayMicroseconds(100);
            timeout++;
        }
        if (timeout >= 25000 && cdromBusy)
        {
            waitingForInt5 = false;
            s_waitingPauseInt2 = false;
            cdromBusy = false;
            return;
        }
        if (!waitingForInt5)
        {
            s_waitingPauseInt2 = false;
            cdromBusy = false;
            return;
        }
    }
    //DEBUG_PRINT("Finish read\n");
}

void updateCDROM_TOC(void) {
	
	uint8_t session = 1;
	
	issueCDROMCommand(CDROM_CMD_SETSESSION, &session, sizeof(session));
	
	waitForINT3();
	waitForINT2();
}

int is_playstation_cd(void) {
	uint8_t tmpbuf[2048];
	memset(tmpbuf, 0, sizeof(tmpbuf));
	startCDROMRead(16, tmpbuf, 1, 2048, 1, 1);

	if (!waitingForInt5)
	{
		return 0;
	}

	if (!memcmp(&tmpbuf[8], "PLAYSTATION", 11))
	{
		return 1;
	}

	return 0;
	/*issueCDROMCommand(CDROM_CMD_GET_ID, NULL, 0);
	waitForINT3();
	
	if (!waitingForInt5)
	{
		return 0;
	}
	
	waitForINT2();
	
	if (!waitingForInt5)
	{
		return 0;
	}
	
	CDROMGetIDResult *res = (CDROMGetIDResult *) cdromResponse;
	
	if (res->type != 0x20)
	{
		return 0;
	}
	
	return 1;*/
}

// Data is ready to be read from the CDROM via DMA.
// This will read the data into cdromReadDataPtr.
// It will also pause the CDROM drive.

#include <stdio.h>
void cdromINT1(void){
    if (cdromReadDataNumSectors <= 0 || cdromReadDataPtr == NULL) {
        // Guard against stray INT1 firing before PAUSE takes effect on 2x CD-ROM:
        // discard unread sector buffer & re-issue PAUSE without DMA-overflowing RAM!
        CDROM_ADDRESS = 0;
        CDROM_HCHPCTL = 0;
        s_waitingPauseInt2 = true;
        issueCDROMCommand(CDROM_CMD_PAUSE, NULL, 0);
        waitingForInt1 = false;
        return;
    }

    DMA_MADR(DMA_CDROM) = (uint32_t) cdromReadDataPtr;
    DMA_BCR(DMA_CDROM)  = cdromReadDataSectorSize / 4;
    DMA_CHCR(DMA_CDROM) = DMA_CHCR_ENABLE | DMA_CHCR_TRIGGER;
    while (DMA_CHCR(DMA_CDROM) & DMA_CHCR_ENABLE) {
        __asm__ volatile("");
    }

    atomic_signal_fence(memory_order_acquire);
    cdromReadDataPtr = (void *) (
        (uintptr_t) cdromReadDataPtr + cdromReadDataSectorSize
    );
    if ((--cdromReadDataNumSectors) <= 0){
        s_waitingPauseInt2 = true;
        issueCDROMCommand(CDROM_CMD_PAUSE , NULL, 0);
    }
        
    atomic_signal_fence(memory_order_release);
    waitingForInt1 = false;
    return;
}

void cdromINT2(void){
    // Do something to handle this interrupt.
    waitingForInt2 = false;
    cdromDataReady = true;
    if (s_waitingPauseInt2) {
        s_waitingPauseInt2 = false;
        cdromBusy = false;
    }
    return;
}

// This is usually just reading the status. It may be more than one parameter, however I don't handle that.
void cdromINT3(void){
    cdromStatus = cdromResponse[0];
    waitingForInt3 = false;
    
    return;
}

void cdromINT4(void){
    // Do something to handle this interrupt.
    waitingForInt4 = false;
    return;
}

// This is the "Error" interrupt.
void cdromINT5(void){
    waitingForInt5 = false;
    s_waitingPauseInt2 = false;
    cdromBusy = false;
    return;
}

size_t file_load(const char *name, void *sectorBuffer){
	uint32_t modelLba;
	
	
	modelLba = getLbaToFile(name);
	if(!modelLba) {
		DEBUG_PRINT("File not found\n");

		return 1;
	} 
	else {
		DEBUG_PRINT("found file\n");
		DEBUG_PRINT("LBA: %d\n", modelLba);
	}

	startCDROMRead( modelLba, sectorBuffer, 1, 2048, true, true );

	return 0;
}

