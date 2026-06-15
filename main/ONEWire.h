/* ************************************************************************** */
/** Descriptive File Name

  @Company
    HGE

  @File Name
    ONEWire.h

  @Summary
    ONEWire protocol.

  @Description
 */
/* ************************************************************************** */

#ifndef _ONEWIRE_H    /* Guard against multiple inclusion */
#define _ONEWIRE_H


#include <stdint.h>

typedef enum{
    ONEWIRE_ST_OK       = -1,
    ONEWIRE_ST_ERROR    = 0,
    ONEWIRE_ST_READY    = 1,
    ONEWIRE_ST_BUSY     = 2,
}_eONEWIREStatus;

#define ONEWIREERROR            0
#define ONEWIREREADY            1
#define ONEWIREBUSY             2
#define ONEWIREISPRESENT        3
#define ONEWIREREADBYTEREADY    4


typedef struct{
    void (*SETPinInput)(void);              /*<Pointer to a function that sets a pin as input*/
    void (*SETPinOutput)(void);             /*<Pointer to a function that sets a pin as output*/
    void (*WritePinBit)(uint8_t value);     /*<Pointer to a function that write a pin value*/
    uint8_t (*ReadPinBit)(void);            /*<Pointer to a function that read a pin value*/
    int (*DELAYus)(int usDelay);            /*<Pointer to a function that count 1 us*/

    struct {								/*<Internal USE. NOT modify*/
        uint8_t     	byteValue;
        uint8_t     	bitIndex;
        uint32_t    	usLastTime;
        uint32_t    	usWaitFor;
        _eONEWIREStatus status;
        union{
        	struct{
            	uint8_t isPresent:		1;
            	uint8_t bitValue: 		1;
            	uint8_t setCurrentus:	1;
            	uint8_t padding:		5;
        	} bit;
        	uint8_t bye;
        } flags;
       uint8_t  		stateTask;
    } taskData;
}_sOWHandle;



/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ONEWire_Init
 * Initialize the ONEWire bus. This function must be called first.
 * @param [in] aOW: Struct that define a ONEWire Bus
 */
void ONEWire_Init(_sOWHandle *aOW);

/**
 * @brief ONEWireReset
 * Reset ONEWire bus.
 * @return
 */
_eONEWIREStatus ONEWireReset(_sOWHandle *aOW);

/**
 * @brief ONEWireWriteBit
 * Write a bit to ONEWire bus.
 * @param [in] bitValue: bit value to write.
 * @return
 */
_eONEWIREStatus ONEWireWriteBit(_sOWHandle *aOW, uint8_t bitValue);

/**
 * @brief ONEWireReadBit
 * Read a bit from ONEWire bus.
 * @param [out] bitValue: address where the bit read is saved.
 * @return
 */
_eONEWIREStatus ONEWireReadBit(_sOWHandle *aOW, uint8_t *bitValue);

/**
 * @brief ONEWireReadByte
 * Read a byte from ONEWire bus.
 * @param [out] byte: address where the byte read is saved.
 * @return
 */
_eONEWIREStatus ONEWireReadByte(_sOWHandle *aOW, uint8_t *byte);

/**
 * @brief ONEWireWriteByte
 * Write a byte to ONEWire bus.
 * @param [in] byte: byte to wire.
 * @return
 */
_eONEWIREStatus ONEWireWriteByte(_sOWHandle *aOW, uint8_t byte);

/**
 * @brief ONEWireIsPresent
 * @return
 */
uint8_t ONEWireIsPresent(_sOWHandle *aOW);

/**
 * @brief ONEWireGetCurrentPinValue
 * @return
 */
uint8_t ONEWireGetCurrentPinValue(_sOWHandle *aOW);

//Functions for performing non-blocking operations on the ONEWIRE bus
/**
 * @brief ONEWireTask
 * Perfom all the task needed. This function must to be called in principal loop all the time.
 * @param [in] usCurrentTime: these are the current usec.
 */
void ONEWireTask(_sOWHandle *aOW, uint32_t usCurrentTime);

/**
 * @brief ONEWireReadByteTask
 * @return
 */
_eONEWIREStatus ONEWireReadByteTask(_sOWHandle *aOW);

/**
 * @brief ONEWireWriteByteTask
 * @param byteValue
 * @return
 */
_eONEWIREStatus ONEWireWriteByteTask(_sOWHandle *aOW, uint8_t byteValue);

/**
 * @brief ONEWireResetTask
 * @return
 */
_eONEWIREStatus ONEWireResetTask(_sOWHandle *aOW);

/**
 * @brief ONEWireGetStatusTask
 * @return
 */
_eONEWIREStatus ONEWireGetStatusTask(_sOWHandle *aOW);

/**
 * @brief ONEWireGetLastByteReadTask
 * @return
 */
uint8_t ONEWireGetLastByteReadTask(_sOWHandle *aOW);



    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */

/* *****************************************************************************
 End of File
 */
