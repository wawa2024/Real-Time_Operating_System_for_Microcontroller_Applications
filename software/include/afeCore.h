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

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#ifndef AFE_CORE_H
#define AFE_CORE_H

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// CH1 => ADC2, CH2 => ADC1

// Pin numbers for the analog frontends
#define CH1_RANGE_SEL       26
// Physical pin
//#define CH1_VOLTAGE       25
// ADC channel
#define CH1_VOLTAGE         ADC_CHANNEL_8
//#define CH1_VOLTAGE       ADC2_CHANNEL_8

#define CH2_RANGE_SEL       21
// Physical pin
//#define CH2_VOLTAGE       33
// ADC channel
#define CH2_VOLTAGE         ADC_CHANNEL_5
//#define CH2_VOLTAGE       ADC1_CHANNEL_5

// Number of values stored in ch1 and ch2 sample buffer
#define SAMPLE_BUFFER_SIZE  10

#define SAMPLE_RATE         1000

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
    // Given trigger type is not valid
    TRIGGER_TYPE_INVALID,
    // Given channel is not valid
    CHANNEL_INVALID,
    // Given range is not valid
    RANGE_INVALID,
    // Function parameter was NULL
    NULL_PTR_ERR,

} afeErr_t;

typedef enum
{
    RISING_EDGE_TRIGGER,
    FALLING_EDGE_TRIGGER,
    BOTH_EDGE_TRIGGER,
    LAST_TRIGGER_TYPE
} afeTrigType_t;

typedef enum
{
    NO_TRIGGER = 0,
    SINGLE_TRIGGER,
    REPEAT_TRIGGER,
    AUTO_TRIGGER,
    LAST_TRIGGER_MODE
} afeTrigMode_t;

typedef enum
{
    RANGE_5V = 0,
    RANGE_15V,
    LAST_RANGE
} afeRange_t;

typedef enum
{
    CHANNEL_1 = 0,
    CHANNEL_2,
    LAST_CHANNEL
} afeChannel_t;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

typedef struct
{
    int32_t zeroOffset[LAST_RANGE];
    // Different scaling values for each range
    // and for positive and negative values
    float pScaling[LAST_RANGE];
    float nScaling[LAST_RANGE];
} afeChannelCal_t;

//////////////////////////////////////
//////////////////////////////////////

typedef struct
{
    struct
    {
        uint32_t level;
        afeTrigMode_t mode;
        afeTrigType_t type;
        uint32_t selectedChannel;
        uint32_t postTrigLen;
        uint32_t preTrigLen;
        bool isTriggered;
        uint32_t triggerIndex;
        uint32_t holdoff;
    } trigger;

    const uint32_t sampleRate;

    afeRange_t ch1_range;
    afeRange_t ch2_range;

    uint32_t currentSampleIndex;
    uint16_t ch1_sampleBuffer[SAMPLE_BUFFER_SIZE];
    uint16_t ch2_sampleBuffer[SAMPLE_BUFFER_SIZE];

    // caliration values for ch1 and ch2
    afeChannelCal_t ch1_cal;
    afeChannelCal_t ch2_cal;

    adc_oneshot_unit_handle_t ch1_handle;
    adc_oneshot_unit_handle_t ch2_handle;

    adc_cali_handle_t ch1_calHandle;
    adc_cali_handle_t ch2_calHandle;

    bool isCh1Disabled;

    bool isInitialized;

} afeCore_t;


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
    extern "C" {
#endif

// Initializes afeCore and configures hardware timerX for use in this library
extern void afeCore_init(void);

// Deinitializes afeCore, which includes removing the active task 
// and stops timerX
extern void afeCore_deinit(void);

extern bool afeCore_isInitialized(void);

extern adc_oneshot_unit_handle_t afeCore_getChannelAdcHandle( afeChannel_t channel );

// Used to disable and enable channel 1 when WIFI is used
extern void afeCore_enableChannel1(void);
extern void afeCore_disableChannel1(void);
extern bool afeCore_isChannel1Disabled(void);

// Prints an appropriate error message to console.
extern void afeCore_logError( afeErr_t err );

// Sets the trigger voltage level 
extern afeErr_t afeCore_setTriggerLevel( double voltage );

// Sets the channel that is used to trigger on
extern afeErr_t afeCore_setTriggerChannel( afeChannel_t channel );

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
extern afeErr_t afeCore_setChannelRange( afeRange_t range, 
                                         afeChannel_t channel );
extern afeRange_t afeCore_getChannelRange( afeChannel_t channel );

// Returns the current sample rate in samples per second.
extern uint32_t afeCore_getSampleRate(void);

// Takes the buffers where samples are copied to and the number of wanted 
// samples. Both buffers should be the same size and their length should 
// not exceed the given n. Returns the number of samples actually copied
extern uint32_t afeCore_getNewestSamples( int32_t *ch1_buffer, 
                                          int32_t *ch2_buffer, 
                                          uint32_t n );

// Copies samples from sample buffer to the given trigger buffer from
// (triggerIndex - preTriggerLength) to (triggerIndex + postTriggerLength) or
// until the end of the given buffer is hit, whichever comes first.
// Takes the buffers where samples are copied to and the size of the buffers.
// Both buffers should be the same size and their length should not exceed 
// the given n. Returns the number of samples actually copied.
extern uint32_t afeCore_getTriggerBuffer( int32_t *ch1_buffer, 
                                          int32_t *ch2_buffer, 
                                          uint32_t n );

// Takes in a sample and converts it to voltage. 
// Returns the voltage as double ( does not apply calibration nor inversion )
extern double afeCore_convertSampleToVoltage( int32_t sample, afeRange_t range );

// Takes in a sample and converts it to voltage. 
// Returns the voltage as float ( Applies calibration to the sample )
extern float afeCore_sample2VoltageCal( int32_t sample, afeChannel_t channel );

//////////////////////////////////////////////////////////////////
// Functions below this should only be used inside afeCalib.cpp //
//////////////////////////////////////////////////////////////////

// Used to calibrate both analog frontends.
extern void afeCore_calibrationTask( void* pvParameter );

extern double afeCore_getCalibratedVoltage( afeChannel_t channel );

extern void afeCore_resetCalibration(void);

extern void writeCalibrationData(void);

// Sets the calibration zero offset for the specified channel
extern afeErr_t afeCore_setZeroOffset( int32_t offset, afeChannel_t channel );
extern afeErr_t afeCore_getZeroOffset( int32_t *offset, afeChannel_t channel );

// Sets the calibration scaling for the specified channel
extern afeErr_t afeCore_setScaling( float positive, float negative, 
                                    afeChannel_t channel );
extern afeErr_t afeCore_getScaling( float *positive, float *negative, 
                                    afeChannel_t channel );

#ifdef __cplusplus
}
#endif

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#endif
