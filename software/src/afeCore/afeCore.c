/*
 * File:        afeCore.c
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

#include "afeCore.h"

// #include "esp_adc/adc_continuous.h"   
// #include "esp_adc/adc_cali.h"         
// #include "esp_adc/adc_cali_scheme.h"  
// #include "esp_adc/adc_oneshot.h"      

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#ifndef LOCAL
    #define LOCAL   static inline
#endif

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// CH1 => ADC2, CH2 => ADC1

// Pin numbers for the analog frontends
#define CH1_RANGE_SEL   26
#define CH1_VOLTAGE     25

#define CH2_RANGE_SEL   21
// Physical pin
//#define CH2_VOLTAGE     33
// ADC channel
#define CH2_VOLTAGE     ADC_CHANNEL_5

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// static adc_continuous_handle_t s_adc_handle;

// #define SAMPLE_BUFF_SIZE   4096

// uint8_t buf[SAMPLE_BUFF_SIZE] = {0};
// size_t bytes_read = 0;

//adc_continuous_read( handle, buffer, sizeof(buffer), &bytes_read, portMAX_DELAY );

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void afeCore_init()
{
    // adc_continuous_handle_cfg_t handle_cfg = 
    // {
    //     .max_store_buf_size = SAMPLE_BUFF_SIZE,
    //     .conv_frame_size    = SAMPLE_BUFF_SIZE,
    // };
    // ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_cfg, &s_adc_handle));

    // adc_digi_pattern_config_t pattern = 
    // {
    //     // ADC attenuation: 12dB => full range 0-3.3V
    //     .atten     = ADC_ATTEN_DB_12,
    //     // Enabled channels
    //     .channel   = CH2_VOLTAGE,
    //     // ADC1 only
    //     .unit      = ADC_UNIT_1,
    //     // 12-bit mode
    //     .bit_width = ADC_BITWIDTH_12,
    // };

    // adc_continuous_config_t dig_cfg = 
    // {
    //     // 200 ksps
    //     .sample_freq_hz = 200000,        
    //     // Only one adc is used (ADC1)
    //     .conv_mode      = ADC_CONV_SINGLE_UNIT_1,
    //     // 12-bit conversion + 4-bit channel ID
    //     .format         = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
    //     // Only one pattern
    //     .pattern_num    = 1,
    //     .adc_pattern    = &pattern,
    // };
    // ESP_ERROR_CHECK(adc_continuous_config(s_adc_handle, &dig_cfg));

    // // 4) Start
    // ESP_ERROR_CHECK(adc_continuous_start(s_adc_handle));

}

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
    RANGE_5V,
    RANGE_15V,
} afeRange_t;

#define SAMPLE_BUFFER_SIZE

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

} afeCore_t;

// Interrupt service routine used to sample the adcs
LOCAL void hardwareTimerISR();

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