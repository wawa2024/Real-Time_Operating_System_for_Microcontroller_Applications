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

#ifndef AFE_CORE_H
#define AFE_CORE_H

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Used to calibrate both analog frontends.
// Takes control over the screen and returns when the calibration is done.
void afeCore_calibrate(void);

// Initializes afeCore and configures hardware timerX for use in this library
void afeCore_init(void);

// Deinitializes afeCore, which includes removing the active task 
// and stops timerX
void afeCore_deinit(void);

// Prints an appropriate error message to console.
void afeCore_logError( afeErr_t err );

// Sets the trigger voltage level 
afeErr_t afeCore_setTriggerLevel( double voltage );

// Sets the channel that is used to trigger on
afeErr_t afeCore_setTriggerChannel( uint32_t channel );

// Sets the trigger mode used. ( Single, Auto, Repeat... )
afeErr_t afeCore_setTriggerMode( afeTrigMode_t mode );
afeTrigMode_t afeCore_getTriggerMode(void);

// Sets the trigger type used. ( e.g. edge type (rising, falling...), etc... )
afeErr_t afeCore_setTriggerType( afeTrigType_t type );
afeTrigType_t afeCore_getTriggerType(void);

// Sets the trigger holdoff between subsequent triggers in milliseconds.
afeErr_t afeCore_setTriggerHoldoff( uint32_t holdoff_ms );

// Sets the amount of time over which samples are saved before and after trigger
afeErr_t afeCore_setTriggerLength( uint32_t postTriggerLength_ms, 
                                   uint32_t preTriggerLength_ms );

// Sets the input range for the given channel. ( x0.1, x0.3 )
afeErr_t afeCore_setChannelRange( afeRange_t range, uint32_t channel );
afeRange_t afeCore_getChannelRange( uint32_t channel );

// Returns the current sample rate in samples per second.
uint32_t afeCore_getSampleRate(void);

// Takes the buffers where samples are copied to and the number of wanted 
// samples. Both buffers should be the same size and their length should 
// not exceed the given n. Returns the number of samples actually copied
uint32_t afeCore_getNewestSamples( uint16_t *ch1_buffer, uint16_t *ch2_buffer, 
                                   uint32_t n );

// Copies samples from sample buffer to the given trigger buffer from
// (triggerIndex - preTriggerLength) to (triggerIndex + postTriggerLength) or
// until the end of the given buffer is hit, whichever comes first.
// Takes the buffers where samples are copied to and the size of the buffers.
// Both buffers should be the same size and their length should not exceed 
// the given n. Returns the number of samples actually copied.
uint32_t afeCore_getTriggerBuffer( uint16_t *ch1_buffer, uint16_t *ch2_buffer, 
                                   uint32_t n );

// Takes in a sample and converts it to voltage. 
// Returns the voltage as double
double afeCore_convertSampleToVoltage( uint16_t sample );

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#endif
