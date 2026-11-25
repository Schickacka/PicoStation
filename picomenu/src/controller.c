/*
 * ps1-bare-metal - (C) 2023 spicyjpeg
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 *
 * This example implements a simple controller tester, showing how to send
 * commands to and obtain data from connected controllers.
 *
 * Compared to other consoles where the state of all inputs is sometimes easily
 * accessible through registers, the PS1 has its controllers and memory cards
 * interfaced via a serial bus (similar to SPI). Both communicate using a simple
 * packet-based protocol, listening for request packets sent by the console and
 * replying with appropriate responses. Each packet consists of an address, a
 * command and a series of parameters, while responses will typically contain
 * information about the controller and the current state of its buttons in
 * addition to any data returned by the command.
 *
 * Communication is done over the SIO0 serial interface, similar to the SIO1
 * interface we used in the hello world example. All ports share the same bus,
 * so an addressing system is used to specify which device shall respond to each
 * packet. Understanding this example may require some basic familiarity with
 * serial ports and their usage, although I tried to add as many comments as I
 * could to explain what is going on under the hood.
 */
 
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ps1/registers.h"
#include "controller.h"
#include "psxproject/system.h"
#include "logging.h"

#if DEBUG_CONTROLLER
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

void initControllerBus(void) {
     // Reset the serial interface, initialize it with the settings used by
     // controllers and memory cards (250000bps, 8 data bits) and configure it to
     // send a signal to the interrupt controller whenever the DSR input is
     // pulsed (see below).
     SIO_CTRL(0) = SIO_CTRL_RESET;
 
     SIO_MODE(0) = 0
         | SIO_MODE_BAUD_DIV1
         | SIO_MODE_DATA_8;
     SIO_BAUD(0) = F_CPU / 250000;
     SIO_CTRL(0) = 0
         | SIO_CTRL_TX_ENABLE
         | SIO_CTRL_RX_ENABLE
         | SIO_CTRL_DSR_IRQ_ENABLE;
}
 
bool waitForAcknowledge(int timeout) {
     // Controllers and memory cards will acknowledge bytes received by sending
     // short pulses over the DSR line, which will be forwarded by the serial
     // interface to the interrupt controller.
     for (; timeout > 0; timeout -= 10) {
         if (IRQ_STAT & (1 << IRQ_SIO0)) {
             // Exact RC5 FUN_8001c400 order: acknowledge SIO0 FIRST so DSR IRQ
             // deasserts, THEN clear IRQ_STAT so it cannot immediately re-latch!
             SIO_CTRL(0) |= SIO_CTRL_ACKNOWLEDGE;
             IRQ_STAT     = ~(1 << IRQ_SIO0);

             return true;
         }

         delayMicroseconds(10);
     }

     return false;
}
 
 #define DTR_DELAY       150
 #define DTR_PRE_DELAY   10
 #define DTR_POST_DELAY  10
 #define DTR_PACKET_DELAY 200
 #define DSR_TIMEOUT     150
 
 void selectPort(int port) {
     // Set or clear the bit that controls which set of controller and memory
     // card ports is going to have its DTR (port select) signal asserted. The
     // actual serial bus is shared between all ports, however devices will not
     // process packets if DTR is not asserted on the port they are plugged into.
     if (port)
         SIO_CTRL(0) |= SIO_CTRL_CS_PORT_2;
     else
         SIO_CTRL(0) &= ~SIO_CTRL_CS_PORT_2;
}
 
 uint8_t exchangeByte(uint8_t value) {
     // Wait until the interface is ready to accept a byte to send, then wait for
     // it to finish receiving the byte sent by the device.
     while (!(SIO_STAT(0) & SIO_STAT_TX_NOT_FULL))
         __asm__ volatile("");
 
     SIO_DATA(0) = value;
 
     while (!(SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY))
         __asm__ volatile("");
 
     return SIO_DATA(0);
}
 
 int exchangePacket(
     DeviceAddress address, const uint8_t *request, uint8_t *response,
     int reqLength, int maxRespLength
 ) {
     // Protect SIO0 byte timing from CD-ROM IRQs (cdromINT1 + PAUSE can burn
     // >200 us, causing a dropped poll frame that re-triggers D-pad edges).
     bool reenable = disableInterrupts();

     delayMicroseconds(DTR_PRE_DELAY);
     IRQ_STAT     = ~(1 << IRQ_SIO0);
     SIO_CTRL(0) |= SIO_CTRL_DTR | SIO_CTRL_ACKNOWLEDGE;
     delayMicroseconds(DTR_DELAY);
 
     int respLength = 0;
 
     SIO_DATA(0) = address;
 
     if (waitForAcknowledge(DSR_TIMEOUT)) {
         while (SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY)
             SIO_DATA(0);
 
         while (respLength < maxRespLength) {
             if (reqLength > 0) {
                 *(response++) = exchangeByte(*(request++));
                 reqLength--;
             } else {
                 *(response++) = exchangeByte(0);
             }
 
             respLength++;
 
             if (!waitForAcknowledge(DSR_TIMEOUT))
                 break;
         }
     }
 
     // Release DTR, allowing the device to go idle.
     delayMicroseconds(DTR_DELAY);
     SIO_CTRL(0) &= ~SIO_CTRL_DTR;
     delayMicroseconds(DTR_POST_DELAY);

     if (reenable) {
         enableInterrupts();
     }
     return respLength;
}
 
 uint16_t getButtonPress(int port) {
     // Build the request packet.
     uint8_t request[4], response[32];

 
     request[0] = CMD_POLL; // Command
     request[1] = 0x00;     // Multitap address
     request[2] = 0x00;     // Rumble motor control 1
     request[3] = 0x00;     // Rumble motor control 2
 
     // Send the request to the specified controller port and grab the response.
     // Note that this is a relatively slow process and should be done only once
     // per frame, unless higher polling rates are desired.
     selectPort(port);
     int respLength = exchangePacket(
         ADDR_CONTROLLER, request, response, sizeof(request), sizeof(response)
     );
 
     if (respLength < 4 || response[1] != 0x5A) {
         // All controllers reply with at least 4 bytes of data.
    //     ptr += sprintf(ptr, "  No controller connected");
         return 0x00;
     }
     
     // Bytes 2 and 3 hold a bitfield representing the state all buttons. As each
     // bit is active low (i.e. a zero represents a button being pressed), the
     // entire field must be inverted.
     uint16_t buttons = (response[2] | (response[3] << 8)) ^ 0xffff;
     return buttons;
}

// Poll both Port 0 and Port 1 (matching RC5 FUN_8001c80c: Digital, DualShock, NeGcon, Mouse)
uint16_t getButtons(uint16_t openMask, uint16_t backMask) {
    static uint16_t s_lastPortButtons[2] = { 0, 0 };
    static uint8_t  s_missCount[2] = { 0, 0 };
    uint8_t request[4] = { CMD_POLL, 0x00, 0x00, 0x00 };
    uint8_t response[32];

    for (int port = 0; port < 2; port++) {
        if (port == 1) {
            delayMicroseconds(50);
        }
        memset(response, 0, sizeof(response));
        selectPort(port);
        int respLength = exchangePacket(
            ADDR_CONTROLLER, request, response, sizeof(request), sizeof(response)
        );
        if (respLength < 4 || response[1] != 0x5A) {
            // Tolerate a single transient missed poll (e.g. wireless/BlueRetro)
            // without injecting a fake release+repress edge that jumps lines.
            if (s_missCount[port] < 2) {
                s_missCount[port]++;
            } else {
                s_lastPortButtons[port] = 0;
            }
            continue;
        }
        s_missCount[port] = 0;

        uint16_t cur = 0;
        uint8_t devType = (uint8_t)(response[0] >> 4);
        if (devType == 0x4 || devType == 0x5 || devType == 0x7) {
            cur = (uint16_t)((response[2] | (response[3] << 8)) ^ 0xFFFFu);
        } else if (devType == 0x1 && respLength >= 6) {
            // PlayStation Mouse
            uint16_t m = 0;
            if ((response[3] & 0x04u) == 0) m |= backMask;
            if ((response[3] & 0x08u) == 0) m |= openMask;
            if (response[4] > 0x20u) {
                if ((int8_t)response[4] < 0) {
                    if (response[4] < 0xE0u) m |= BUTTON_MASK_LEFT;
                } else {
                    m |= BUTTON_MASK_RIGHT;
                }
            }
            if (response[5] > 0x20u) {
                if ((int8_t)response[5] < 0) {
                    if (response[5] < 0xE0u) m |= BUTTON_MASK_UP;
                } else {
                    m |= BUTTON_MASK_DOWN;
                }
            }
            cur = m;
        } else if (devType == 0x2 && respLength >= 8) {
            // NeGcon
            uint16_t n = (uint16_t)((response[2] | (response[3] << 8)) ^ 0xFFFFu);
            if ((int8_t)response[5] < 0) n |= BUTTON_MASK_X;
            if ((int8_t)response[6] < 0) n |= BUTTON_MASK_SQUARE;
            if ((int8_t)response[7] < 0) n |= BUTTON_MASK_L1;
            cur = n;
        }
        s_lastPortButtons[port] = cur;
    }

    return (uint16_t)(s_lastPortButtons[0] | s_lastPortButtons[1]);
}


void sendPacketNoAcknowledge(
    DeviceAddress address, const uint8_t *request, int reqLength
) {
    // Exact RC5 FUN_8001cda8 timing (60 us/byte, DTR pre/post 10 us, DTR 150 us)
    // for 100% compatibility with OWMC, MemCard PRO, MemCard PRO 2, and SD2PSX!
    bool reenable = disableInterrupts();

    delayMicroseconds(DTR_PRE_DELAY);
    IRQ_STAT     = ~(1 << IRQ_SIO0);
    SIO_CTRL(0) |= SIO_CTRL_DTR | SIO_CTRL_ACKNOWLEDGE;
    delayMicroseconds(DTR_DELAY);

    exchangeByte((uint8_t)address);
    delayMicroseconds(BYTE_DELAY);

    for (; reqLength > 0; reqLength--) {
        exchangeByte(*(request++));
        delayMicroseconds(BYTE_DELAY);
    }

    delayMicroseconds(DTR_DELAY);
    SIO_CTRL(0) &= ~SIO_CTRL_DTR;
    delayMicroseconds(DTR_POST_DELAY);

    if (reenable) {
        enableInterrupts();
    }
}

uint8_t checkMCPpresent(void)
{
	uint8_t ret = 0;
	uint8_t request[5] = {CMD_GAME_ID_PING, 0, 0, 0, 0 };
	uint8_t response  [5];
	uint8_t requestID = CMD_CARD_IDENTIFY;
	uint8_t responseID[9];
	
	for (int i = 0; i < 2; i++)
	{
		memset(response  , 0, 5);
		memset(responseID, 0, 9);
		selectPort(i);
		int respLength = exchangePacket(ADDR_MEMORY_CARD, &requestID, responseID, 1, sizeof(responseID));
		
		respLength = exchangePacket(ADDR_MEMORY_CARD, request, response, sizeof(request), sizeof(response));
		
		if (respLength == 5 && response[3] == 0x27 && response[4] == 0xFF)
		{
			ret |= (1 << i);
		}
#if DEBUG_CONTROLLER
		DEBUG_PRINT("Port %d response: ", i);
		for (int n = 0; n < 5; n++)
		{
			DEBUG_PRINT("%02X ", response[n]);
		}
		DEBUG_PRINT("\n");
#endif
	}
	
	return ret;
}

void sendGameID(const char *str, uint8_t card) {
    (void)card;
    uint8_t request[64];
    size_t  strLen = strlen(str);
    if (strLen > 59) strLen = 59;

    // Exact RC5 FUN_8001cef0 wire format:
    //   [0x21, 0x00, strlen(str)+1, str..., '\0'] sent unconditionally to Port 0 & Port 1!
    request[0] = CMD_GAME_ID_SEND; // 0x21
    request[1] = 0x00;
    request[2] = (uint8_t)(strLen + 1);
    memcpy(&request[3], str, strLen);
    request[3 + strLen] = '\0';

    for (int i = 0; i < 2; i++) {
        selectPort(i);
        sendPacketNoAcknowledge(ADDR_MEMORY_CARD, request, (int)(strLen + 4));
    }
}

