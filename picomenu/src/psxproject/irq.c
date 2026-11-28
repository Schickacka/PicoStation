#include "irq.h"

#include <stddef.h>

#include "ps1/cdrom.h"
#include "cdrom.h"
//#include "spu.h"
#include "stream.h"

#include "ps1/registers.h"
#include "delay.h"
#include "system.h"

volatile bool vblank = false;
extern uint8_t cdromRespLength;

// Sets the global vblank variable to true.
void handleVSyncIRQ(void){
    vblank = true;
}

void handleCDROMIRQ(void) {
    /* Save active register bank (0..3) so interrupting issueCDROMCommand() can
     * NEVER leave Bank 1 selected and corrupt CDROM_HINTMSK_W (0x1F801802)! */
    uint8_t savedBank = CDROM_HSTS & CDROM_HSTS_RA_BITMASK;

    CDROM_ADDRESS = 1;

    uint8_t irqType = CDROM_HINTSTS & (0
        | CDROM_HINT_INT0
        | CDROM_HINT_INT1
        | CDROM_HINT_INT2);

    // If a new sector is available, request a sector buffer read.
    if (irqType == CDROM_IRQ_DATA_READY) {
        CDROM_ADDRESS = 0;
        CDROM_HCHPCTL = 0;
        CDROM_HCHPCTL = CDROM_HCHPCTL_BFRD;
    }

    CDROM_ADDRESS = 1;
    CDROM_HCLRCTL = 0 // Acknowledge all IRQs (NEVER assert CLRPRM inside ISR!)
        | CDROM_HCLRCTL_CLRINT0
        | CDROM_HCLRCTL_CLRINT1
        | CDROM_HCLRCTL_CLRINT2;
    delayMicroseconds(3);

    cdromRespLength = 0;

    while (CDROM_HSTS & CDROM_HSTS_RSLRRDY) {
        uint8_t b = CDROM_RESULT;
        if (cdromRespLength < 16) {
            cdromResponse[cdromRespLength++] = b;
        }
    }

    switch (irqType) {
        case CDROM_IRQ_DATA_READY:
            cdromINT1();
            break;
        case CDROM_IRQ_COMPLETE:
            cdromINT2();
            break;
        case CDROM_IRQ_ACKNOWLEDGE:
            cdromINT3();
            break;
        case CDROM_IRQ_DATA_END:
            cdromINT4();
            break;
        case CDROM_IRQ_ERROR :
            cdromINT5();
            break;
    }

    CDROM_ADDRESS = savedBank;
}


// This is the first step to handling the IRQ.
// It will acknowledge the interrupt on the COP0 side, and call the relevant handler for the device.
void interruptHandlerFunction(void *arg){
    if(acknowledgeInterrupt(IRQ_VSYNC)){
        handleVSyncIRQ();
    }
    if(acknowledgeInterrupt(IRQ_CDROM)){
        handleCDROMIRQ();
    }
    if(acknowledgeInterrupt(IRQ_SPU)){
        stream_handleInterrupt(&stream);
    }
}

void initIRQ(void){
    installExceptionHandler();
    // This is the function that is called when an interrupt is raised.
    // You can also pass an argument to this handler.
    setInterruptHandler(interruptHandlerFunction, NULL);
    // The IRQ mask specifies which interrupt sources are actually allowed to raise an interrupt.
    IRQ_MASK = (1 << IRQ_VSYNC) | (1 << IRQ_CDROM) | (1 << IRQ_SPU);
    enableInterrupts();
}

void waitForVblank(void){
    while(!vblank){
        // Do nothing
    }
    vblank = false;
}