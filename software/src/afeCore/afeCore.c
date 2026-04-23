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

*/

#include <afeCore.h>



// non volatile storage controller for calibration data
#include "nvs_flash.h"
#include "nvs.h"

// moved to afeCalib.cpp
//#include <esp32-oscilloscope.h>
//#include "hmiCore.h"

// #include "esp_adc/adc_cali.h"
// #include "esp_adc/adc_cali_scheme.h"
// #include "esp_adc/adc_oneshot.h"

#include <driver/adc.h>

#include "driver/gpio.h"

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
    .sampleRate = 1000,
    .ch1_range = RANGE_5V,
    .ch2_range = RANGE_5V,
    .currentSampleIndex = 0,
    .ch1_sampleBuffer = {0},
    .ch2_sampleBuffer = {0},
    .ch1_cal = {0},
    .ch2_cal = {0},
    // .ch1_handle = 0,
    // .ch2_handle = 0,
    // .ch1_calHandle = 0,
    // .ch2_calHandle = 0,
    .isCh1Disabled = false,
    .isInitialized = false,
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Interrupt service routine used to sample the adcs
LOCAL void hardwareTimerISR(void);

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
    nvs_open( nvs_calNamespace, NVS_READONLY, &handle );

    for( uint32_t i = 0; i < 2; i++ )
    {
        // Data to be stored
        data = i == 0 ? &afeCore.ch1_cal : &afeCore.ch2_cal;
        size_t size = sizeof(data);

        // Key for nvs key value pair system
        const char * key = i == 0 ? ch1_storageKey : ch2_storageKey;

        // Read data
        err = nvs_get_blob( handle, key, &data, &size );

        // No value stored yet
        if( err == ESP_ERR_NVS_NOT_FOUND )
        {
            // Set default values
            for( uint32_t i = 0; i < LAST_RANGE; i++ )
            {
                data->zeroOffset[i] = 0;
                data->pScaling[i] = 0.0f;
                data->nScaling[i] = 0.0f;
            }
        }
    }

    nvs_close( handle );

}

//////////////////////////////////////
//////////////////////////////////////

LOCAL void writeCalibrationData(void)
{
    nvs_handle_t handle;
    afeChannelCal_t *data;

    // Get namespace handle
    nvs_open( nvs_calNamespace, NVS_READWRITE, &handle );

    for( uint32_t i = 0; i < 2; i++ )
    {
        // Data to be stored
        data = i == 0 ? &afeCore.ch1_cal : &afeCore.ch2_cal;
        size_t size = sizeof(data);

        // Key for nvs key value pair system
        const char * key = i == 0 ? ch1_storageKey : ch2_storageKey;

        // Prepare data
        nvs_set_blob( handle, key, data, size );

        // Write data
        nvs_commit( handle );
    }

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

///////////////////////////
// Moved to afeCalib.cpp //
///////////////////////////
//
// // Used to calibrate both analog frontends.
// void afeCore_calibrationTask( void* pvParameter )
// {
//     if( pvParameter == NULL || !mutex_take() ) { vTaskDelete(NULL); }

//     QueueHandle_t q = *(QueueHandle_t*)pvParameter;

//     while( !afeCore.isInitialized )
//     {
//         hmiEventData_t e = getinputs( q );
//         if( e.inputs & (1 << BTN_ESC) )
//         {
//             mutex_release();
//             vTaskDelete(NULL);
//         }
//     }

//     // Clear all events
//     getinputs( q );

//     tft.fillScreen( TFT_BLACK );
//     tft.setTextSize( TFT_LARGE );
//     tft.setTextColor( TFT_WHITE );
//     tft.drawString( "CALIBRATION", RESOLUTION_X / 2, RESOLUTION_Y / 8 );
//     tft.setTextSize( TFT_MEDIUM );
//     tft.drawString( "Short each input,", RESOLUTION_X / 2, RESOLUTION_Y / 6 );
//     tft.drawString( "after which press enter", RESOLUTION_X / 2, RESOLUTION_Y / 5 );

//     for(;;)
//     {
//         hmiEventData_t e = getinputs( q );
//         if( e.inputs & (1 << BTN_ESC) )
//         {
//             mutex_release();
//             vTaskDelete(NULL);
//         }
//         if( e.inputs & (1 << BTN_ENTER) ) { break; }
//     }

//     // Average samples over 2 seconds
//     // TODO: Change this to wait for ~2 seconds
//     // and use afeCore_getNewestSamples( n = 2*afeCore.sampleRate );
//     for( uint32_t i = 0; i < (2 * afeCore.sampleRate); i++ )
//     {
//         int32_t sample;
//         adc_oneshot_read( afeCore_t, ADC_CHANNEL_0, &sample )
//     }

//     for(;;)
//     {

//     }

//     mutex_release();
//     // self-delete
//     vTaskDelete(NULL);
// }

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Temporarily used for calibration ( until afeCore is finalized )
afeCore_t* afeCore_getCore(void)
{
    return &afeCore;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Initializes afeCore and configures hardware timerX for use in this library
void afeCore_init(void)
{
    calibrationDataInit();

    afeCore_setChannelRange( RANGE_15V, CHANNEL_1 );
    afeCore_setChannelRange( RANGE_15V, CHANNEL_2 );

    adc2_config_channel_atten(ADC2_CHANNEL_8, ADC_ATTEN_DB_12);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_12);

    // adc_oneshot_chan_cfg_t chan_cfg =
    // {
    //     // 12‑bit resolution
    //     .bitwidth = ADC_BITWIDTH_DEFAULT,
    //     // Full scale input (0-3.3V)
    //     .atten = ADC_ATTEN_DB_12,
    // };

    // // ADC1 INIT
    // adc_oneshot_unit_init_cfg_t adc1_cfg =
    // {
    //     .unit_id = ADC_UNIT_1,
    //     .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
    //     .ulp_mode = ADC_ULP_MODE_DISABLE,
    // };

    // adc_oneshot_new_unit( &adc1_cfg, &afeCore.ch2_handle );
    // adc_oneshot_config_channel( afeCore.ch2_handle, ADC_CHANNEL_0, &chan_cfg );

    // // ADC2 INIT
    // adc_oneshot_unit_init_cfg_t adc2_cfg =
    // {
    //     .unit_id = ADC_UNIT_2,
    //     .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
    //     .ulp_mode = ADC_ULP_MODE_DISABLE,
    // };
    // adc_oneshot_new_unit( &adc2_cfg, &afeCore.ch1_handle );
    // adc_oneshot_config_channel( afeCore.ch1_handle, ADC_CHANNEL_2, &chan_cfg );

    // // Calibration
    // adc_cali_line_fitting_config_t ch1_calCfg =
    // {
    //     .unit_id = ADC_UNIT_1,
    //     .atten = ADC_ATTEN_DB_12,
    //     .bitwidth = ADC_BITWIDTH_DEFAULT,
    // };
    // adc_cali_line_fitting_config_t ch2_calCfg =
    // {
    //     .unit_id = ADC_UNIT_2,
    //     .atten = ADC_ATTEN_DB_12,
    //     .bitwidth = ADC_BITWIDTH_DEFAULT,
    // };
    // adc_cali_create_scheme_line_fitting( &ch1_calCfg, &afeCore.ch1_calHandle );
    // adc_cali_create_scheme_line_fitting( &ch2_calCfg, &afeCore.ch2_calHandle );

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
    if( type >= LAST_TRIGGER_TYPE ) {  }
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
        gpio_set_level( (gpio_num_t)CH1_RANGE_SEL, range == RANGE_5V ? 1 : 0 );
    }
    else if( channel == CHANNEL_2 )
    {
        afeCore.ch2_range = range;
        gpio_set_level( (gpio_num_t)CH2_RANGE_SEL, range == RANGE_5V ? 1 : 0 );
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
// not exceed the given n. Returns the number of samples actually copied
uint32_t afeCore_getNewestSamples( uint16_t *ch1_buffer, uint16_t *ch2_buffer,
                                   uint32_t n )
{

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Copies samples from sample buffer to the given trigger buffer from
// (triggerIndex - preTriggerLength) to (triggerIndex + postTriggerLength) or
// until the end of the given buffer is hit, whichever comes first.
// Takes the buffers where samples are copied to and the size of the buffers.
// Both buffers should be the same size and their length should not exceed
// the given n. Returns the number of samples actually copied.
uint32_t afeCore_getTriggerBuffer( uint16_t *ch1_buffer, uint16_t *ch2_buffer,
                                   uint32_t n )
{

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Takes in a sample and converts it to voltage.
// Returns the voltage as double
double afeCore_convertSampleToVoltage( uint16_t sample )
{

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
