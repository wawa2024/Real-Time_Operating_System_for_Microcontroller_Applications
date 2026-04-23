/*
 * File:        afeCalib.c
 * Author:      Juho Rantsi
 * Created:     23.04.2026
 * Description:
        afeCore extension for calibration task
 *
 * Licensed under the GNU General Public License version 2.
 * See the accompanying LICENSE file for full details.
 *
 *******************************************************************************
 *******************************************************************************

 Version history:

    023.04.2026 JR
        - Extension created

*/

#include "afeCore.h"

#include <cstdio>

// Definitions
#include <esp32-oscilloscope.h>

// User input
#include "hmiCore.h"

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

// Used to calibrate both analog frontends.
void afeCore_calibrationTask( void* pvParameter )
{
    while( !mutex_take() ) { DELAY(100); }

    if( pvParameter == NULL ) { vTaskDelete(NULL); }

    QueueHandle_t q = *(QueueHandle_t*)pvParameter;

    // Remove this when direct access to the struct is no longer needed
    afeCore_t *afeCore = afeCore_getCore();

    while( !afeCore_isInitialized() )
    {
        hmiEventData_t e = getinputs( q );
        if( e.inputs & BTN_ESC )
        {
            mutex_release();
            vTaskDelete(NULL); 
        }
    }

    // Clear all events
    getinputs( q );

    static const uint32_t Y_OFFSET = RESOLUTION_Y / 10;

    tft.fillScreen( TFT_BLACK );
    tft.setTextSize( TFT_LARGE );
    tft.setTextColor( TFT_WHITE );
    tft.setTextDatum( MC_DATUM );
    tft.drawString( "CALIBRATION", RESOLUTION_X / 2, 1*Y_OFFSET );
    tft.setTextSize( TFT_MEDIUM );
    tft.drawString( "Short each input,", RESOLUTION_X / 2, 2*Y_OFFSET);
    tft.drawString( "after which press enter", RESOLUTION_X / 2, 3*Y_OFFSET );

    for(;;)
    {
        hmiEventData_t e = getinputs( q );
        if( e.inputs & BTN_ESC )
        {
            mutex_release();
            vTaskDelete(NULL); 
        }
        if( e.inputs & BTN_ENTER ) { break; }
    }

    uint32_t ch1_sum = 0, ch2_sum = 0;

    // Average samples over 2 seconds
    // TODO: Change this to wait for ~2 seconds 
    // and use afeCore_getNewestSamples( n = 2*afeCore.sampleRate );
    for( uint32_t i = 0; i < (2 * afeCore_getSampleRate()); i++ )
    {
        int ch1_sample = 0, ch2_sample = 0;
        int ch1_mv = 0, ch2_mv = 0;

        ch1_sample = adc1_get_raw( CH2_VOLTAGE ); 
        adc2_get_raw( CH1_VOLTAGE, ADC_WIDTH_BIT_12, &ch1_sample ); 

        // adc_oneshot_read( afeCore->ch1_handle, CH1_VOLTAGE, &ch1_sample );
        // adc_oneshot_read( afeCore->ch2_handle, CH2_VOLTAGE, &ch2_sample );

        // adc_cali_raw_to_voltage( afeCore->ch1_calHandle, ch1_sample, &ch1_mv );
        // adc_cali_raw_to_voltage( afeCore->ch2_calHandle, ch2_sample, &ch2_mv );

        ch1_sum += ch1_sample;
        ch2_sum += ch2_sample;
    }

    ch1_sum /= (2 * afeCore_getSampleRate());
    ch2_sum /= (2 * afeCore_getSampleRate());

    char ch1_sumString[10];
    char ch2_sumString[10];

    std::snprintf( ch1_sumString, sizeof(ch1_sumString), "CH1: %ld", ch1_sum );
    std::snprintf( ch2_sumString, sizeof(ch2_sumString), "CH2: %ld", ch2_sum );

    tft.drawString( ch1_sumString, RESOLUTION_X / 2, 6*Y_OFFSET );
    tft.drawString( ch2_sumString, RESOLUTION_X / 2, 5*Y_OFFSET );    

    for(;;)
    {
        hmiEventData_t e = getinputs( q );
        if( e.inputs & BTN_ESC )
        {
            mutex_release();
            vTaskDelete(NULL); 
        }
        if( e.inputs & BTN_ENTER ) { break; }
    }

    mutex_release();
    // self-delete
    vTaskDelete(NULL); 
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////