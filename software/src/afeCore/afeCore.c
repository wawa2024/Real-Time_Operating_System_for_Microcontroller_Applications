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

    22.04.2026 JR
        - Added the ability to store calibration data in non volatile memory
        - Expanded afeCore_t and created a new typedef for calibration values

    24.04.2026 JR
        - Changed the buffer types for getNewestSamples and getTriggerBuffer
          from uint16_t to int32_t 
        - Implemented afeCore_convertSampleToVoltage
        - Added couple helper functions for calibration purposes

*/

#include <afeCore.h>

#include <graph_task.h>

// non volatile storage controller for calibration data
#include "nvs_flash.h"
#include "nvs.h"

#include "driver/gpio.h"

#include "driver/gptimer.h"
#include "soc/sens_reg.h"
#include "soc/sens_struct.h"
#include "esp_attr.h"

#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#ifndef LOCAL
    #define LOCAL   static inline
#endif

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

afeCore_t afeCore =
{
    .trigger = 
    {
        .level = 0,
        .mode = NO_TRIGGER,
        .type = RISING_EDGE_TRIGGER,
        .selectedChannel = CHANNEL_1,
        .postTrigLen = 0,
        .preTrigLen = 0,
        .isTriggered = false,
        .triggerIndex = 0,
        .holdoff = 0,
    },
    .sampleRate = SAMPLE_RATE,
    .ch1_range = RANGE_15V,
    .ch2_range = RANGE_15V, 
    .currentSampleIndex = 0,
    .ch1_sampleBuffer = {0},
    .ch2_sampleBuffer = {0},
    .ch1_cal = 
    {
        .zeroOffset[0] = 0,
        .zeroOffset[1] = 0,
        .pScaling[0] = 1.0,
        .pScaling[1] = 1.0,
        .nScaling[0] = 1.0,
        .nScaling[1] = 1.0,
    },
    .ch2_cal =
    {
        .zeroOffset[0] = 0,
        .zeroOffset[1] = 0,
        .pScaling[0] = 1.0,
        .pScaling[1] = 1.0,
        .nScaling[0] = 1.0,
        .nScaling[1] = 1.0,
    },
    .ch1_handle = 0,
    .ch2_handle = 0,
    .ch1_calHandle = 0,
    .ch2_calHandle = 0,
    .isCh1Disabled = false,
    .isInitialized = false,
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

static const size_t g_frame_size = 256;

// Called from ISR context when DMA has new ADC data
static bool IRAM_ATTR adc_convDoneCallback( adc_continuous_handle_t handle,
                                            const adc_continuous_evt_data_t *edata,
                                            void *user_ctx )
{
    // edata->size gives number of bytes available
    // edata->buf is the DMA buffer pointer
    const uint8_t *data = edata->conv_frame_buffer;
    size_t len = g_frame_size;


    // Example: iterate through samples
    for( size_t i = 0; i < len; i += sizeof(adc_digi_output_data_t) ) 
    {
        const adc_digi_output_data_t *sample =
            (const adc_digi_output_data_t *)&data[i];

        // Extract unit, channel, and raw value
        uint8_t unit = sample->type2.unit;
        uint8_t channel = sample->type2.channel;
        uint16_t value = sample->type2.data;

        // ADC 1
        if( unit == 0 )
        {
            add_sample( value, CHANNEL_2 );
        }
        // ADC 2
        else if( unit == 1 )
        {
            add_sample( value, CHANNEL_1 );
        }
        
    }

    // Return true to yield from ISR if needed (FreeRTOS)
    return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

extern TaskHandle_t adc;

// Interrupt service routine used to sample the adcs
static bool IRAM_ATTR timer2_isrCallback( gptimer_handle_t timer, 
                                          const gptimer_alarm_event_data_t *edata, 
                                          void *user_ctx)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if( adc != NULL )
    {
        vTaskNotifyGiveFromISR( adc, &xHigherPriorityTaskWoken );    
    }

    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );  
}

//////////////////////////////////////
//////////////////////////////////////

#define TIMER_GROUP         TIMER_GROUP_1
#define TIMER_NUM           TIMER_0

// 80 MHz / 80 = 1 MHz timer clock
#define TIMER_DIVIDER       80      
// 100 µs => 10 kHz  
#define TIMER_INTERVAL_US   100     

LOCAL void timer2_configure(void)
{
    gptimer_handle_t gptimer = NULL;

    gptimer_config_t config = 
    {
        // 80 MHz APB clock
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,   
        .direction = GPTIMER_COUNT_UP,
        // 1 MHz → 1 tick = 1 µs
        .resolution_hz = 1000000,             
    };

    gptimer_new_timer(&config, &gptimer);

    gptimer_event_callbacks_t cbs = { .on_alarm = timer2_isrCallback };
    gptimer_register_event_callbacks( gptimer, &cbs, NULL );

    // Set alarm to 50 µs => 20 kHz
    gptimer_alarm_config_t alarm_config = 
    {
        // 50 ticks = 50 µs
        .alarm_count = 50,       
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    gptimer_set_alarm_action( gptimer, &alarm_config );

    gptimer_enable( gptimer );
    gptimer_start( gptimer );
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// espidf nvs namespace where calibration data is stored
static const char nvs_calNamespace[] = "calibration";
static const char ch1_storageKey[] = "ch1";
static const char ch2_storageKey[] = "ch2";

//////////////////////////////////////
//////////////////////////////////////

LOCAL void readCalibrationData(void)
{
    nvs_handle_t handle;
    afeChannelCal_t *data;
    esp_err_t err;

    // Get namespace handle
    err = nvs_open(nvs_calNamespace, NVS_READONLY, &handle);
    
    if( err != ESP_OK ) { return; }

    for( uint32_t i = 0; i < 2; i++ )
    {
        // Data to be stored
        data = i == 0 ? &afeCore.ch1_cal : &afeCore.ch2_cal;
        size_t size = sizeof( afeChannelCal_t );

        // Key for nvs key value pair system
        const char * key = i == 0 ? ch1_storageKey : ch2_storageKey;

        // Read data
        err = nvs_get_blob( handle, key, data, &size );

        // No value stored yet
        if( err == ESP_ERR_NVS_NOT_FOUND )
        {
            // Set default values
            for( uint32_t i = 0; i < LAST_RANGE; i++ )
            {
                data->zeroOffset[i] = 0;
                data->pScaling[i] = 1.0;
                data->nScaling[i] = 1.0;
            }
        }
    }

    nvs_close( handle );
}

//////////////////////////////////////
//////////////////////////////////////

void writeCalibrationData(void)
{
    nvs_handle_t handle;
    afeChannelCal_t *data;

    // Get namespace handle
    nvs_open( nvs_calNamespace, NVS_READWRITE, &handle );

    for( uint32_t i = 0; i < 2; i++ )
    {
        // Data to be stored
        data = i == 0 ? &afeCore.ch1_cal : &afeCore.ch2_cal;
        size_t size = sizeof( afeChannelCal_t );

        // Key for nvs key value pair system
        const char * key = i == 0 ? ch1_storageKey : ch2_storageKey;

        // Prepare data
        nvs_set_blob( handle, key, data, size );
    }

    // Write both structs at once
    nvs_commit( handle );

    nvs_close( handle );
}

//////////////////////////////////////
//////////////////////////////////////

LOCAL void calibrationDataInit(void)
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();

    if( err == ESP_ERR_NVS_NO_FREE_PAGES
    ||  err == ESP_ERR_NVS_NEW_VERSION_FOUND )
    {
        // Try to reinitialize flash
        nvs_flash_erase();
        nvs_flash_init();
    }

    readCalibrationData();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Sets the calibration zero offset for the specified channel
// on the range that is currently enabled
afeErr_t afeCore_setZeroOffset( int32_t offset, afeChannel_t channel )
{
    switch( channel )
    {
        case CHANNEL_1: 
            afeCore.ch1_cal.zeroOffset[ afeCore.ch1_range ] = offset; 
            break;

        case CHANNEL_2:
            afeCore.ch2_cal.zeroOffset[ afeCore.ch2_range ] = offset; 
            break;

        default: return CHANNEL_INVALID; break;
    }
}

//////////////////////////////////////
//////////////////////////////////////

// Returns the zero offset for the specified channel 
// on the range that is currently enabled
afeErr_t afeCore_getZeroOffset( int32_t *offset, afeChannel_t channel )
{
    if( offset == NULL ) { return NULL_PTR_ERR; }

    switch( channel )
    {
        case CHANNEL_1: 
            *offset = afeCore.ch1_cal.zeroOffset[ afeCore.ch1_range ]; 
            break;

        case CHANNEL_2:
            *offset = afeCore.ch2_cal.zeroOffset[ afeCore.ch2_range ]; 
            break;

        default: 
            return CHANNEL_INVALID; 
            break;
    }

    return NO_ERR;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Sets the calibration scaling for the specified channel
afeErr_t afeCore_setScaling( float positive, float negative, 
                             afeChannel_t channel )
{
    switch( channel )
    {
        case CHANNEL_1: 
            afeCore.ch1_cal.pScaling[ afeCore.ch1_range ] = positive; 
            afeCore.ch1_cal.nScaling[ afeCore.ch1_range ] = negative; 
            break;

        case CHANNEL_2:
            afeCore.ch2_cal.pScaling[ afeCore.ch2_range ] = positive; 
            afeCore.ch2_cal.nScaling[ afeCore.ch2_range ] = negative; 
            break;

        default: return CHANNEL_INVALID; break;
    }
}

//////////////////////////////////////
//////////////////////////////////////

// Sets the calibration scaling for the specified channel
afeErr_t afeCore_getScaling( float *positive, float *negative, 
                             afeChannel_t channel )
{
    if( positive == NULL || negative == NULL ) { return NULL_PTR_ERR; }

    switch( channel )
    {
        case CHANNEL_1: 
            *positive = afeCore.ch1_cal.pScaling[ afeCore.ch1_range ]; 
            *negative = afeCore.ch1_cal.nScaling[ afeCore.ch1_range ]; 
            break;

        case CHANNEL_2:
            *positive = afeCore.ch2_cal.pScaling[ afeCore.ch2_range ]; 
            *negative = afeCore.ch2_cal.nScaling[ afeCore.ch2_range ]; 
            break;

        default: return CHANNEL_INVALID; break;
    }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void afeCore_resetCalibration(void)
{
    for( uint32_t i = 0; i < LAST_RANGE; i++ )
    {
        // CH1
        afeCore.ch1_cal.zeroOffset[i] = 0;
        afeCore.ch1_cal.pScaling[i] = 1.0;
        afeCore.ch1_cal.nScaling[i] = 1.0;
        // CH2
        afeCore.ch2_cal.zeroOffset[i] = 0;
        afeCore.ch2_cal.pScaling[i] = 1.0;
        afeCore.ch2_cal.nScaling[i] = 1.0;
    }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

double afeCore_getCalibratedVoltage( afeChannel_t channel )
{
    int sample = 0;
    double voltage = 0.0;

    switch( channel )
    {
        case CHANNEL_1:
            adc_oneshot_read( afeCore_getChannelAdcHandle(CHANNEL_1), CH1_VOLTAGE, &sample );
            sample += afeCore.ch1_cal.zeroOffset[ afeCore.ch1_range ];
            sample *= -1;
            sample *= sample < 0 ? afeCore.ch1_cal.nScaling[ afeCore.ch1_range ] : afeCore.ch1_cal.pScaling[ afeCore.ch1_range ];
            voltage = afeCore_convertSampleToVoltage( sample, afeCore.ch1_range );
            break;
        
        case CHANNEL_2:
            adc_oneshot_read( afeCore_getChannelAdcHandle(CHANNEL_2), CH2_VOLTAGE, &sample );
            sample += afeCore.ch2_cal.zeroOffset[ afeCore.ch2_range ];
            sample *= -1;
            sample *= sample < 0 ? afeCore.ch2_cal.nScaling[ afeCore.ch2_range ] : afeCore.ch2_cal.pScaling[ afeCore.ch2_range ];
            voltage = afeCore_convertSampleToVoltage( sample, afeCore.ch2_range );
            break;
    }

    return voltage;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

LOCAL void adc_init_oneshot(void)
{
    adc_oneshot_chan_cfg_t chan_cfg = 
    {
        // 12‑bit resolution 
        .bitwidth = ADC_BITWIDTH_DEFAULT,   
        // Full scale input (0-3.3V)
        .atten = ADC_ATTEN_DB_12,           
    };

    // ADC1 INIT ( ADC1 is configured in adc_init_continuous)
    // adc_oneshot_unit_init_cfg_t adc1_cfg = 
    // {
    //     .unit_id = ADC_UNIT_1,
    //     .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
    //     .ulp_mode = ADC_ULP_MODE_DISABLE,
    // };

    // adc_oneshot_new_unit( &adc1_cfg, &afeCore.ch2_handle );
    // adc_oneshot_config_channel( afeCore.ch2_handle, CH2_VOLTAGE, &chan_cfg );

    // ADC2 INIT
    adc_oneshot_unit_init_cfg_t adc2_cfg = 
    {
        .unit_id = ADC_UNIT_2,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_new_unit( &adc2_cfg, &afeCore.ch1_handle );
    adc_oneshot_config_channel( afeCore.ch1_handle, CH1_VOLTAGE, &chan_cfg );
}

//////////////////////////////////////
//////////////////////////////////////

LOCAL void adc_init_continuous(void)
{
    adc_continuous_handle_t handle;

    adc_continuous_handle_cfg_t handle_cfg = 
    {
        .max_store_buf_size = 1024,
        .conv_frame_size = g_frame_size,
    };

    adc_continuous_new_handle( &handle_cfg, &handle );

    adc_digi_pattern_config_t pattern[] = 
    {
        {
            .atten = ADC_ATTEN_DB_12,
            .channel = CH2_VOLTAGE,
            .unit = ADC_UNIT_1,
            .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
        },
    };

    adc_continuous_config_t dig_cfg = 
    {
        .pattern_num = 1,
        .sample_freq_hz = SAMPLE_RATE,
        .adc_pattern = pattern,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };

    adc_continuous_config(handle, &dig_cfg);

    adc_continuous_evt_cbs_t cbs = { .on_conv_done = adc_convDoneCallback };

    adc_continuous_register_event_callbacks( handle, &cbs, NULL );

    adc_continuous_start( handle );
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Initializes afeCore and configures hardware timerX for use in this library
void afeCore_init(void)
{
    calibrationDataInit();

    readCalibrationData();

    // Configure output pins
    gpio_config_t outputConfig = {
        .pin_bit_mask = (1ULL << CH1_RANGE_SEL) | (1ULL << CH2_RANGE_SEL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config( &outputConfig );

    afeCore_setChannelRange( RANGE_15V, CHANNEL_1 );
    afeCore_setChannelRange( RANGE_15V, CHANNEL_2 );

    adc_init_oneshot();
    adc_init_continuous();

    timer2_configure();

    afeCore.isInitialized = true;
}

//////////////////////////////////////
//////////////////////////////////////

// Deinitializes afeCore, which includes removing the active task
// and stops timerX
void afeCore_deinit(void)
{

}

//////////////////////////////////////
//////////////////////////////////////

bool afeCore_isInitialized(void)
{
    return afeCore.isInitialized;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void afeCore_enableChannel1(void)
{
    afeCore.isCh1Disabled = false;
}

//////////////////////////////////////
//////////////////////////////////////

void afeCore_disableChannel1(void)
{
    afeCore.isCh1Disabled = true;
}

//////////////////////////////////////
//////////////////////////////////////

bool afeCore_isChannel1Disabled(void)
{
    return afeCore.isCh1Disabled;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

adc_oneshot_unit_handle_t afeCore_getChannelAdcHandle( afeChannel_t channel )
{
    if( channel == CHANNEL_1 ) { return afeCore.ch1_handle; }
    else { return afeCore.ch2_handle; }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Prints an appropriate error message to console.
void afeCore_logError( afeErr_t err )
{

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Sets the trigger voltage level
afeErr_t afeCore_setTriggerLevel( double voltage )
{

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Sets the channel that is used to trigger on
afeErr_t afeCore_setTriggerChannel( afeChannel_t channel )
{

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Sets the trigger mode used. ( Single, Auto, Repeat... )
afeErr_t afeCore_setTriggerMode( afeTrigMode_t mode )
{

}

//////////////////////////////////////
//////////////////////////////////////

afeTrigMode_t afeCore_getTriggerMode(void)
{

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Sets the trigger type used. ( e.g. edge type (rising, falling...), etc... )
afeErr_t afeCore_setTriggerType( afeTrigType_t type )
{
    if( type >= LAST_TRIGGER_TYPE ) { return TRIGGER_TYPE_INVALID; }
    afeCore.trigger.type = type;
}

//////////////////////////////////////
//////////////////////////////////////

afeTrigType_t afeCore_getTriggerType(void)
{
    return afeCore.trigger.type;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Sets the trigger holdoff between subsequent triggers in milliseconds.
afeErr_t afeCore_setTriggerHoldoff( uint32_t holdoff_ms )
{

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Sets the amount of time over which samples are saved before and after trigger
afeErr_t afeCore_setTriggerLength( uint32_t postTriggerLength_ms,
                                   uint32_t preTriggerLength_ms )
{

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Sets the input range for the given channel. ( x0.1, x0.3 )
afeErr_t afeCore_setChannelRange( afeRange_t range, afeChannel_t channel )
{
    if( channel >= LAST_CHANNEL ){ return CHANNEL_INVALID; }
    if( range >= LAST_RANGE ){ return RANGE_INVALID; }

    if( channel == CHANNEL_1 )
    {
        afeCore.ch1_range = range;
        gpio_set_level( (gpio_num_t)CH1_RANGE_SEL, range == RANGE_15V ? 0 : 1 );
    }
    else if( channel == CHANNEL_2 )
    {
        afeCore.ch2_range = range;
        gpio_set_level( (gpio_num_t)CH2_RANGE_SEL, range == RANGE_15V ? 0 : 1 );
    }
}

//////////////////////////////////////
//////////////////////////////////////

afeRange_t afeCore_getChannelRange( afeChannel_t channel )
{
    return channel == CHANNEL_1 ? afeCore.ch1_range : afeCore.ch2_range;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Returns the current sample rate in samples per second.
uint32_t afeCore_getSampleRate(void)
{
    return afeCore.sampleRate;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Takes the buffers where samples are copied to and the number of wanted
// samples. Both buffers should be the same size and their length should
// not exceed the given n. 
uint32_t afeCore_getNewestSamples( uint16_t *ch1_buffer, uint16_t *ch2_buffer,
                                   uint32_t n )
{
    char tempStr[60];

    if( ch1_buffer == NULL && ch2_buffer == NULL ) { return 0; }

    static uint32_t startIndex = 0;

    uint32_t temp2 = afeCore.currentSampleIndex;
    int32_t temp = (temp2 + 1) - n;

    // We want to read more values than the currentSampleIndex 
    // is so we need to start from the end of the buffer
    if( temp < 0 ) 
    { 
        temp += SAMPLE_BUFFER_SIZE; 
    }
    // else
    // {  
    //     // We want to read more than have been 
    //     // sampled between current and last read
    //     if( startIndex + n > temp2 ) 
    //     { 
    //         // Number of samples that we want to read is 
    //         // the number of samples taken since the last read
    //         n = temp2 - startIndex;


    //         if( startIndex + n > SAMPLE_BUFFER_SIZE )
    //         {

    //         }
    //     }
    // }

    for( uint32_t i = 0; i < n; i++, temp++ )
    {
        ch1_buffer[i] = afeCore.ch1_sampleBuffer[temp];
        ch2_buffer[i] = afeCore.ch2_sampleBuffer[temp];

        snprintf( tempStr, sizeof(tempStr), "CH1: %d,\tCH2: %d,\tTemp: %ld", afeCore.ch1_sampleBuffer[temp], afeCore.ch2_sampleBuffer[temp], temp );

        printStr(tempStr);

        if( temp >= SAMPLE_BUFFER_SIZE ) { temp = 0; }
    }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Copies samples from sample buffer to the given trigger buffer from
// (triggerIndex - preTriggerLength) to (triggerIndex + postTriggerLength) or
// until the end of the given buffer is hit, whichever comes first.
// Takes the buffers where samples are copied to and the size of the buffers.
// Both buffers should be the same size and their length should not exceed
// the given n. Returns the number of samples actually copied.
uint32_t afeCore_getTriggerBuffer( int32_t *ch1_buffer, int32_t *ch2_buffer,
                                   uint32_t n )
{

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

static const double ADC_RESOLUTION = 3.3 / (0xFFF-1);
static const double RANGE_5V_MULT = 1 / 0.3;
static const double RANGE_15V_MULT = 1 / 0.1;

// Takes in a sample and converts it to voltage. Returns the voltage as double. 
// ( does not apply calibration nor inversion ). Returns 0.0 if range invalid.
double afeCore_convertSampleToVoltage( int32_t sample, afeRange_t range )
{
    if( range >= LAST_RANGE ){ return 0.0; }

    // Convert value to voltage
    double temp = sample * ADC_RESOLUTION;

    // Multiply by the inverse of frontend gain
    if( range == RANGE_5V )         { temp *= RANGE_5V_MULT; }
    else if( range == RANGE_15V )   { temp *= RANGE_15V_MULT; }

    return temp;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

float afeCore_sample2VoltageCal( int32_t sample, afeChannel_t channel )
{
    double voltage = 0.0;

    switch( channel )
    {
        case CHANNEL_1:
            sample += afeCore.ch1_cal.zeroOffset[ afeCore.ch1_range ];
            sample *= -1;
            sample *= sample < 0 ? afeCore.ch1_cal.nScaling[ afeCore.ch1_range ] : afeCore.ch1_cal.pScaling[ afeCore.ch1_range ];
            voltage = afeCore_convertSampleToVoltage( sample, afeCore.ch1_range );
            break;
        
        case CHANNEL_2:
            sample += afeCore.ch2_cal.zeroOffset[ afeCore.ch2_range ];
            sample *= -1;
            sample *= sample < 0 ? afeCore.ch2_cal.nScaling[ afeCore.ch2_range ] : afeCore.ch2_cal.pScaling[ afeCore.ch2_range ];
            voltage = afeCore_convertSampleToVoltage( sample, afeCore.ch2_range );
            break;
    }

    return voltage;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

