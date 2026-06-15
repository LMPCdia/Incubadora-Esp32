/** Descriptive File Name

  @Company
    HGE

  @File Name
    onewire.c

  @Summary
    Onewire protocol.

  @Description

*/

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "ONEWire.h"

//static void (*mySetPinInputFcn)(void) = NULL;
//static void (*mySetPinOutputFcn)(void) = NULL;
//static void (*myWritePinBitFcn)(uint8_t value) = NULL;
//static uint8_t (*myReadPinBitFcn)(void) = NULL;
//static int (*myDelayusFcn)(int usDelay) = NULL;
//
//static uint8_t oneWireTaskStatus = ONEWIREERROR;
//
//static enum eTaskStatus{
//    IDLE = 0,
//    RESETSTATE0,
//    RESETSTATE1,
//    RESETSTATE2,
//    RESETSTATE3,
//    WRITEBITSTATE0,
//    WRITEBITSTATE1,
//    WRITEBITSTATE2,
//    WRITEBITSTATE3,
//    WRITEBITSTATE4,
//    READBITSTATE0,
//    READBITSTATE1,
//    READBITSTATE2,
//    READBITSTATE3,
//    READBITSTATE4,
//} taskStatus;
//
//static struct sTaskData{
//    uint8_t     byteValue;
//    uint8_t     bitIndex;
//    uint32_t    usLastTime;
//    uint32_t    usWaitFor;
//} taskData;

//static uint8_t isPresent = 0;
//static uint8_t bitRed = 0;
//static uint8_t setCurrentus = 0;

#define IDLE			0
#define RESETSTATE0		1
#define RESETSTATE1		2
#define RESETSTATE2		3
#define RESETSTATE3		4
#define WRITEBITSTATE0	5
#define WRITEBITSTATE1	6
#define WRITEBITSTATE2	7
#define WRITEBITSTATE3	8
#define READBITSTATE0	9
#define READBITSTATE1	10
#define READBITSTATE2	11


void ONEWire_Init(_sOWHandle *aOW){
	aOW->taskData.status = ONEWIRE_ST_ERROR;

	if(aOW->DELAYus == NULL)
		return;
	if(aOW->ReadPinBit == NULL)
		return;
	if(aOW->SETPinInput == NULL)
		return;
	if(aOW->WritePinBit == NULL)
		return;
	if(aOW->SETPinOutput == NULL)
		return;

    memset(&aOW->taskData, 0, sizeof(aOW->taskData));
    
    aOW->taskData.stateTask = IDLE;

    aOW->taskData.status = ONEWIRE_ST_READY;
}

_eONEWIREStatus ONEWireReset(_sOWHandle *aOW){
	if(aOW->taskData.status == ONEWIRE_ST_ERROR)
        return ONEWIRE_ST_ERROR;

	aOW->SETPinOutput();

    aOW->WritePinBit(0);
    
    aOW->DELAYus(480);

    aOW->SETPinInput();
    
    aOW->DELAYus(80);
    
    aOW->taskData.flags.bit.isPresent = aOW->ReadPinBit();
    
    aOW->DELAYus(400);
    
    return ONEWIRE_ST_OK;
}

_eONEWIREStatus ONEWireWriteBit(_sOWHandle *aOW, uint8_t bitValue){
	if(aOW->taskData.status == ONEWIRE_ST_ERROR)
        return ONEWIRE_ST_ERROR;

	aOW->SETPinOutput();

    aOW->WritePinBit(0);
    
    aOW->DELAYus(3);
    
    if(bitValue)
    	aOW->SETPinInput();

    aOW->DELAYus(57);

	aOW->SETPinInput();

    aOW->DELAYus(3);
    
    return ONEWIRE_ST_OK;
}

_eONEWIREStatus ONEWireReadBit(_sOWHandle *aOW, uint8_t *bitValue){
	if(aOW->taskData.status == ONEWIRE_ST_ERROR)
        return ONEWIRE_ST_ERROR;

	aOW->SETPinOutput();

    aOW->WritePinBit(0);
    
    aOW->DELAYus(1);
    
    aOW->SETPinInput();
    
    aOW->DELAYus(9);
    
    *bitValue = aOW->ReadPinBit();
    
    aOW->DELAYus(48);

    return ONEWIRE_ST_OK;
}

_eONEWIREStatus ONEWireReadByte(_sOWHandle *aOW, uint8_t *byteValue){
	if(aOW->taskData.status == ONEWIRE_ST_ERROR)
        return ONEWIRE_ST_ERROR;

	uint8_t i = 8;
    uint8_t byte = 0;
    uint8_t bitValue;

	while (i--) {
		byte >>= 1;
        ONEWireReadBit(aOW, &bitValue);
        if(bitValue)
            byte |= 0x80;
	}

    *byteValue = byte;
    
	return ONEWIRE_ST_OK;
}

_eONEWIREStatus ONEWireWriteByte(_sOWHandle *aOW, uint8_t byte){
	if(aOW->taskData.status == ONEWIRE_ST_ERROR)
        return ONEWIRE_ST_ERROR;
    
    uint8_t i = 8;
    
	while (i--) {
        ONEWireWriteBit(aOW, byte & 0x01);
		byte >>= 1;
	}
    
    return ONEWIRE_ST_OK;
}

uint8_t ONEWireGetCurrentPinValue(_sOWHandle *aOW){
    return aOW->ReadPinBit();
}


//TASK functions
void ONEWireTask(_sOWHandle *aOW, uint32_t usCurrentTime){
    uint32_t aux;

    if(aOW->taskData.flags.bit.setCurrentus){
    	aOW->taskData.flags.bit.setCurrentus = 0;
    	aOW->taskData.usLastTime = usCurrentTime;
    }

    if(aOW->taskData.usWaitFor){
        aux = usCurrentTime - aOW->taskData.usLastTime;
        if(aux >= aOW->taskData.usWaitFor)
        	aOW->taskData.usWaitFor = 0;
        else
            return;
    }

    aOW->taskData.usLastTime = usCurrentTime;
    
    switch(aOW->taskData.stateTask){
        case IDLE:
        	aOW->taskData.status = ONEWIRE_ST_READY;
            break;
        case RESETSTATE0:
        	aOW->SETPinOutput();
            aOW->WritePinBit(0);
            aOW->taskData.usWaitFor = 480;
            aOW->taskData.stateTask = RESETSTATE1;
            break;
        case RESETSTATE1:
        	aOW->SETPinInput();
            aOW->taskData.usWaitFor = 70;
            aOW->taskData.stateTask = RESETSTATE2;
            break;
        case RESETSTATE2:
        	aOW->taskData.flags.bit.isPresent = aOW->ReadPinBit();
            aOW->taskData.usWaitFor = 410;
            aOW->taskData.stateTask = RESETSTATE3;
            break;
        case RESETSTATE3:
        	aOW->taskData.stateTask = IDLE;
        	aOW->taskData.status = ONEWIRE_ST_READY;
            break;
        case WRITEBITSTATE0:
        	aOW->SETPinOutput();
        	aOW->WritePinBit(0);
        	aOW->DELAYus(2);
        	aOW->taskData.stateTask = WRITEBITSTATE1;
            break;
        case WRITEBITSTATE1:
            if(aOW->taskData.byteValue & aOW->taskData.bitIndex)
                aOW->SETPinInput();
            aOW->taskData.usWaitFor = 60;
            aOW->taskData.stateTask = WRITEBITSTATE2;
            break;
        case WRITEBITSTATE2:
        	aOW->SETPinInput();
            aOW->taskData.usWaitFor = 10;
            aOW->taskData.bitIndex <<= 1;
            if(aOW->taskData.bitIndex)
            	aOW->taskData.stateTask = WRITEBITSTATE0;
            else
            	aOW->taskData.stateTask = WRITEBITSTATE3;
            break;
        case WRITEBITSTATE3:
        	aOW->taskData.stateTask = IDLE;
            aOW->taskData.status = ONEWIRE_ST_READY;
            break;
        case READBITSTATE0:
        	aOW->SETPinOutput();
        	aOW->WritePinBit(0);
        	aOW->DELAYus(1);
        	aOW->SETPinInput();
            aOW->taskData.usWaitFor = 5;
            aOW->taskData.stateTask = READBITSTATE1;
            break;
        case READBITSTATE1:
        	aOW->taskData.flags.bit.bitValue = aOW->ReadPinBit();
            aOW->taskData.byteValue >>= 1;
            if(aOW->taskData.flags.bit.bitValue)
                aOW->taskData.byteValue |= 0x80;
            aOW->taskData.usWaitFor = 60;
            aOW->taskData.stateTask = READBITSTATE2;
            break;
        case READBITSTATE2:
            aOW->taskData.bitIndex >>= 1;
            if(aOW->taskData.bitIndex)
            	aOW->taskData.stateTask = READBITSTATE0;
            else{
            	aOW->taskData.stateTask = IDLE;
            	aOW->taskData.status = ONEWIRE_ST_READY;
            }
            break;
        default:
        	aOW->taskData.stateTask = IDLE;
        	aOW->taskData.status = ONEWIRE_ST_READY;
    }
                
}

_eONEWIREStatus ONEWireReadByteTask(_sOWHandle *aOW){
    if(aOW->taskData.status == ONEWIRE_ST_BUSY)
        return ONEWIRE_ST_BUSY;

    aOW->taskData.byteValue = 0;
    aOW->taskData.bitIndex = 0x80;
    aOW->taskData.stateTask = READBITSTATE0;
    aOW->taskData.flags.bit.setCurrentus = 1;
    aOW->taskData.status = ONEWIRE_ST_BUSY;
    
    return ONEWIRE_ST_OK;
}

_eONEWIREStatus ONEWireWriteByteTask(_sOWHandle *aOW, uint8_t byteValue){
    if(aOW->taskData.status == ONEWIRE_ST_BUSY)
        return ONEWIRE_ST_BUSY;
    
    aOW->taskData.bitIndex = 1;
    aOW->taskData.byteValue = byteValue;
    aOW->taskData.stateTask = WRITEBITSTATE0;
    aOW->taskData.flags.bit.setCurrentus = 1;
    aOW->taskData.status = ONEWIRE_ST_BUSY;
    
    return ONEWIRE_ST_OK;
}

_eONEWIREStatus ONEWireResetTask(_sOWHandle *aOW){
    if(aOW->taskData.status == ONEWIRE_ST_BUSY)
        return ONEWIRE_ST_BUSY;
    
    aOW->SETPinOutput();
    aOW->WritePinBit(1);
    aOW->taskData.usWaitFor = 3;
    aOW->taskData.stateTask = RESETSTATE0;
    aOW->taskData.flags.bit.isPresent = 1;
    aOW->taskData.flags.bit.setCurrentus = 1;
    aOW->taskData.status = ONEWIRE_ST_BUSY;
    
    return ONEWIRE_ST_OK;
}

_eONEWIREStatus ONEWireGetStatusTask(_sOWHandle *aOW){
    return aOW->taskData.status;
}

uint8_t ONEWireGetLastByteReadTask(_sOWHandle *aOW){
    return  aOW->taskData.byteValue;
}


uint8_t ONEWireIsPresent(_sOWHandle *aOW){
    if(aOW->taskData.flags.bit.isPresent)
        return 0;
    return 1;
}








/* *****************************************************************************
 End of File
 */
