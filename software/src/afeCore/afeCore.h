/*
 * File:        afeCore.h
 * Author:      Juho Rantsi
 * Created:     01.04.2026
 * Description: 
        afeCore is a library / module for accessing all of the boards analog 
        inputs and their hardware and virtual functions that are needed in 
        esp32-oscilloscope project. E.g input sampling, calibration etc...
 *
 * Licensed under the GNU General Public License version 2.
 * See the accompanying LICENSE file for full details.
 *
 *******************************************************************************
 *******************************************************************************
 
 Version history:
 
    01.04.2026 JR   
        - Library created

*/

#include <stdint.h>

#include <stdbool.h>

#ifndef AFE_CORE_H
#define AFE_CORE_H

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

typedef enum
{
    // No error 
    NO_ERR = 0,
    // Given trigger level out of bounds
    TRIGGER_LEVEL_BOUNDS,
    // Given trigger length out of bounds
    TRIGGER_LENGTH_BOUNDS,
    // Given channel is not valid
    TRIGGER_CHANNEL_INVALID,
    // Function parameter was NULL
    NULL_PTR_ERR,

} afeErr_t;

typedef enum
{
    RISING_EDGE_TRIGGER,
    FALLING_EDGE_TRIGGER,
    BOTH_EDGE_TRIGGER,

} afeTrigType_t;

typedef enum
{
    NO_TRIGGER = 0,
    SINGLE_TRIGGER,
    REPEAT_TRIGGER,
    AUTO_TRIGGER,
} afeTrigMode_t;

typedef enum
{
    RANGE_5V = 0,
    RANGE_15V,
    LAST_RANGE
} afeRange_t;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Used to calibrate both analog frontends.
// Takes control over the screen and returns when the calibration is done.
extern void afeCore_calibrate(void);

// Initializes afeCore and configures hardware timerX for use in this library
extern void afeCore_init(void);

// Deinitializes afeCore, which includes removing the active task 
// and stops timerX
extern void afeCore_deinit(void);

// Prints an appropriate error message to console.
extern void afeCore_logError( afeErr_t err );

// Sets the trigger voltage level 
extern afeErr_t afeCore_setTriggerLevel( double voltage );

// Sets the channel that is used to trigger on
extern afeErr_t afeCore_setTriggerChannel( uint32_t channel );

// Sets the trigger mode used. ( Single, Auto, Repeat... )
extern afeErr_t afeCore_setTriggerMode( afeTrigMode_t mode );
extern afeTrigMode_t afeCore_getTriggerMode(void);

// Sets the trigger type used. ( e.g. edge type (rising, falling...), etc... )
extern afeErr_t afeCore_setTriggerType( afeTrigType_t type );
extern afeTrigType_t afeCore_getTriggerType(void);

// Sets the trigger holdoff between subsequent triggers in milliseconds.
extern afeErr_t afeCore_setTriggerHoldoff( uint32_t holdoff_ms );

// Sets the amount of time over which samples are saved before and after trigger
extern afeErr_t afeCore_setTriggerLength( uint32_t postTriggerLength_ms, 
                                   uint32_t preTriggerLength_ms );

// Sets the input range for the given channel. ( x0.1, x0.3 )
extern afeErr_t afeCore_setChannelRange( afeRange_t range, uint32_t channel );
extern afeRange_t afeCore_getChannelRange( uint32_t channel );

// Returns the current sample rate in samples per second.
extern uint32_t afeCore_getSampleRate(void);

// Takes the buffers where samples are copied to and the number of wanted 
// samples. Both buffers should be the same size and their length should 
// not exceed the given n. Returns the number of samples actually copied
extern uint32_t afeCore_getNewestSamples( uint16_t *ch1_buffer, 
                                          uint16_t *ch2_buffer, 
                                          uint32_t n );

// Copies samples from sample buffer to the given trigger buffer from
// (triggerIndex - preTriggerLength) to (triggerIndex + postTriggerLength) or
// until the end of the given buffer is hit, whichever comes first.
// Takes the buffers where samples are copied to and the size of the buffers.
// Both buffers should be the same size and their length should not exceed 
// the given n. Returns the number of samples actually copied.
extern uint32_t afeCore_getTriggerBuffer( uint16_t *ch1_buffer, 
                                          uint16_t *ch2_buffer, 
                                          uint32_t n );

// Takes in a sample and converts it to voltage. 
// Returns the voltage as double
extern double afeCore_convertSampleToVoltage( uint16_t sample );

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#endif
