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

    23.04.2026 JR
        - Extension created

    24.04.2026 JR
        - Calibration procedure finalized and couple helper functions added

*/

#include "afeCore.h"

#include <cstdio>

// Definitions
#include <esp32-oscilloscope.h>

// User input
#include "hmiCore.h"

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#ifndef LOCAL
    #define LOCAL   static inline
#endif

#define SAMPLE_MULTIPLIER   4

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

static const uint32_t xMidPoint = RESOLUTION_X / 2;
static const uint32_t yOffset = RESOLUTION_Y / 10;

static bool isCalibrationDone = false;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void printStr( const char *str )
{
    Serial.println(str);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

extern uint64_t counter; 

LOCAL void drawVoltage(void)
{
    static char ch1_string[20] = {"\0"};
    static char ch2_string[20] = {"\0"};

    double ch1_voltage = afeCore_getCalibratedVoltage( CHANNEL_1 );
    double ch2_voltage = afeCore_getCalibratedVoltage( CHANNEL_2 );

    tft.setTextSize( TFT_SMALL );

    // Remove old text
    tft.setTextColor( TFT_BLACK );
    tft.drawString( ch1_string, RESOLUTION_X / 6, 9*yOffset);
    tft.drawString( ch2_string, RESOLUTION_X * 5 / 6, 9*yOffset);
    tft.setTextColor( TFT_WHITE );

    std::snprintf( ch1_string, sizeof(ch1_string), "CH1: %.2lf", ch1_voltage );
    std::snprintf( ch2_string, sizeof(ch2_string), "CH2: %.2lf", ch2_voltage );

    tft.drawString( ch1_string, RESOLUTION_X / 6, 9*yOffset);
    tft.drawString( ch2_string, RESOLUTION_X * 5 / 6, 9*yOffset);
    tft.setTextSize( TFT_MEDIUM );
}

LOCAL void exitIf( uint32_t arg )
{
    if( arg )
    {

        mutex_release();
        vTaskDelete(NULL);  
    }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Returns when enter is pressed and 
// exists calibration task if escape is pressed
LOCAL void handleInputs( QueueHandle_t q )
{
    for(;;)
    {
        hmiEventData_t e = getinputs( q );
        exitIf( e.inputs & BTN_ESC );
        if( e.inputs & BTN_ENTER ) { return; }
        drawVoltage();
    }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

LOCAL void clearInputBuffer( QueueHandle_t q )
{
    hmiEventData_t data = { E_NONE, 0 };
    while( xQueueReceive( q, &data, 0 ) == pdTRUE );
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

LOCAL void calibrateZeroOffset( QueueHandle_t q )
{
    tft.fillScreen( TFT_BLACK );
    tft.setTextSize( TFT_LARGE );
    tft.drawString( "CALIBRATION", xMidPoint, 1*yOffset );
    tft.setTextSize( TFT_MEDIUM );
    tft.drawString( "Short each input,", xMidPoint, 2*yOffset );
    tft.drawString( "after which press enter", xMidPoint, 3*yOffset );

    clearInputBuffer( q );

    handleInputs(q);

    int32_t ch1_sum = 0, ch2_sum = 0;

    // Average samples over 2 seconds
    // TODO: Change this to wait for ~2 seconds 
    // and use afeCore_getNewestSamples( n = 2*afeCore.sampleRate );
    for( uint32_t i = 0; i < (SAMPLE_MULTIPLIER * afeCore_getSampleRate()); i++ )
    {
        int ch1_sample = 0, ch2_sample = 0;

        adc_oneshot_read( afeCore_getChannelAdcHandle(CHANNEL_1), CH1_VOLTAGE, &ch1_sample );
        adc_oneshot_read( afeCore_getChannelAdcHandle(CHANNEL_2), CH2_VOLTAGE, &ch2_sample );

        ch1_sum += ch1_sample;
        ch2_sum += ch2_sample;
    }

    ch1_sum /= (SAMPLE_MULTIPLIER * afeCore_getSampleRate());
    ch2_sum /= (SAMPLE_MULTIPLIER * afeCore_getSampleRate());

    // Zero offset is implemented as ( rawValue + zeroOffset )
    // so the offset needs to be inverse of the measured value
    ch1_sum *= -1;
    ch2_sum *= -1;

    // The range is that the offset is saved to is the currenty active one
    afeCore_setZeroOffset( ch1_sum, CHANNEL_1 );
    afeCore_setZeroOffset( ch2_sum, CHANNEL_2 );

    char ch1_sumString[20];
    char ch2_sumString[20];

    std::snprintf( ch1_sumString, sizeof(ch1_sumString), "CH1: %ld", ch1_sum );
    std::snprintf( ch2_sumString, sizeof(ch2_sumString), "CH2: %ld", ch2_sum );

    tft.drawString( "Applied offsets:", xMidPoint, 6*yOffset );
    tft.drawString( ch1_sumString, xMidPoint, 7*yOffset );
    tft.drawString( ch2_sumString, xMidPoint, 8*yOffset );
    tft.setTextSize( TFT_SMALL );  
    tft.drawString( "Press enter to continue ==>>", xMidPoint, 9*yOffset );  

    // Clear all events
    clearInputBuffer( q );

    handleInputs(q);
}

//////////////////////////////////////
//////////////////////////////////////

LOCAL void calibrateRange( QueueHandle_t q, afeRange_t range )
{
    double ch1_pScaling = 0.0, ch1_nScaling = 0.0;
    double ch2_pScaling = 0.0, ch2_nScaling = 0.0;
    char ch1_scalingStr[20], ch2_scalingStr[20];

    // Calibrate both +RANGE ( i == 0 ) and -RANGE ( i == 1 )
    for( uint32_t i = 0; i < 2; i++ )
    {
        tft.fillScreen( TFT_BLACK );
        tft.setTextSize( TFT_LARGE );
        tft.drawString( "CALIBRATION", xMidPoint, 1*yOffset );
        tft.setTextSize( TFT_MEDIUM );

        if( i == 0 && range == RANGE_15V )
        {
            tft.drawString( "Supply +12V to each input,", xMidPoint, 2*yOffset );
        }
        else if( i == 0 && range == RANGE_5V )
        {
            tft.drawString( "Supply +3V to each input,", xMidPoint, 2*yOffset );
        }
        else if( i == 1 && range == RANGE_15V )
        {
            tft.drawString( "Supply -12V to each input,", xMidPoint, 2*yOffset );
        }
        else if( i == 1 && range == RANGE_5V )
        {
            tft.drawString( "Supply -3V to each input,", xMidPoint, 2*yOffset );
        }
        else
        {
            tft.drawString( "ERROR: RANGE || Idx,", xMidPoint, 2*yOffset );
        }
        
        tft.drawString( "after which press enter", xMidPoint, 3*yOffset );

        // Clear all events
        clearInputBuffer( q );

        handleInputs(q);

        // Delay for 1 second so the inputs have time to stabilize
        DELAY(1000);

        int32_t ch1_sum = 0, ch2_sum = 0;

        // Average samples over 2 seconds
        // TODO: Change this to wait for ~2 seconds 
        // and use afeCore_getNewestSamples( n = 2*afeCore.sampleRate );
        for( uint32_t i = 0; i < (SAMPLE_MULTIPLIER * afeCore_getSampleRate()); i++ )
        {
            int ch1_sample = 0, ch2_sample = 0;

            adc_oneshot_read( afeCore_getChannelAdcHandle(CHANNEL_1), CH1_VOLTAGE, &ch1_sample );
            adc_oneshot_read( afeCore_getChannelAdcHandle(CHANNEL_2), CH2_VOLTAGE, &ch2_sample );

            ch1_sum += ch1_sample;
            ch2_sum += ch2_sample;
        }

        char buff[120];
        std::snprintf( buff, sizeof(buff), "Range: %d, Pol: %ld, ch1_sum: %ld, ch2_sum: %ld\r\n", range, i, ch1_sum, ch2_sum);
        Serial.print(buff);

        ch1_sum /= (SAMPLE_MULTIPLIER * afeCore_getSampleRate());
        ch2_sum /= (SAMPLE_MULTIPLIER * afeCore_getSampleRate());

        std::snprintf( buff, sizeof(buff), "ch1_sum: %ld, ch2_sum: %ld\r\n", ch1_sum, ch2_sum);
        Serial.print(buff);

        // Should not be needed when afeCore is used to read the samples 
        // since it will automaticly calculate the offset and invert the sign
        int32_t ch1_zeroOffset = 0, ch2_zeroOffset = 0;
        afeCore_getZeroOffset( &ch1_zeroOffset, CHANNEL_1 );
        afeCore_getZeroOffset( &ch2_zeroOffset, CHANNEL_2 );
        ch1_sum += ch1_zeroOffset;
        ch2_sum += ch2_zeroOffset;
        
        std::snprintf( buff, sizeof(buff), "ch1_sum: %ld, ch1_zeroOffset: %ld, ch2_sum: %ld, ch2_zeroOffset: %ld\r\n", ch1_sum, ch1_zeroOffset, ch2_sum, ch2_zeroOffset );
        Serial.print(buff);
        
        ch1_sum *= -1;
        ch2_sum *= -1;
        
        std::snprintf( buff, sizeof(buff), "ch1_sum: %ld, ch2_sum: %ld\r\n", ch1_sum, ch2_sum);
        Serial.print(buff);

        // Range is expected to be what was set earlier in the calibration process
        double ch1_voltage = afeCore_convertSampleToVoltage( ch1_sum, range );
        double ch2_voltage = afeCore_convertSampleToVoltage( ch2_sum, range );

        std::snprintf( buff, sizeof(buff), "ch1_voltage: %.2lf, ch2_voltage: %.2lf\r\n", ch1_voltage, ch2_voltage);
        Serial.print(buff);

        if( i == 0 )    
        { 
            double expected = ( range == RANGE_15V ? 12.0 : 3.0 );
            ch1_pScaling = expected / ch1_voltage;
            ch2_pScaling = expected / ch2_voltage;

            std::snprintf( buff, sizeof(buff), "ch1_pScaling: %.2lf, ch2_pScaling: %.2lf\r\n", ch1_pScaling, ch2_pScaling );
            Serial.print(buff);

            std::snprintf( ch1_scalingStr, sizeof(ch1_scalingStr), "CH1: %.6lf", ch1_pScaling );
            std::snprintf( ch2_scalingStr, sizeof(ch2_scalingStr), "CH2: %.6lf", ch2_pScaling );

            tft.drawString( "Calculated pScaling:", xMidPoint, 6*yOffset );
        }
        else            
        { 
            double expected = ( range == RANGE_15V ? -12.0 : -3.0 );
            ch1_nScaling = expected / ch1_voltage;
            ch2_nScaling = expected / ch2_voltage;

            std::snprintf( buff, sizeof(buff), "ch1_pScaling: %.2lf, ch2_pScaling: %.2lf\r\n", ch1_pScaling, ch2_pScaling );
            Serial.print(buff);

            std::snprintf( ch1_scalingStr, sizeof(ch1_scalingStr), "CH1: %.6lf", ch1_nScaling );
            std::snprintf( ch2_scalingStr, sizeof(ch2_scalingStr), "CH2: %.6lf", ch2_nScaling );

            tft.drawString( "Calculated nScaling:", xMidPoint, 6*yOffset );
        }
        
        tft.drawString( ch1_scalingStr, xMidPoint, 7*yOffset );
        tft.drawString( ch2_scalingStr, xMidPoint, 8*yOffset );
        tft.setTextSize( TFT_SMALL );  
        tft.drawString( "Press enter to continue ==>>", xMidPoint, 9*yOffset );  

        // Clear all events
        clearInputBuffer( q );

        handleInputs(q);  

        // Both negative and positive side have been calibrated
        if( i == 1 ) 
        { 
            // The values are automatically set for the active range,
            // so no need to specify
            afeCore_setScaling( ch1_pScaling, ch1_nScaling, CHANNEL_1 );
            afeCore_setScaling( ch2_pScaling, ch2_nScaling, CHANNEL_2 );
            
            return; 
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Used to calibrate both analog frontends.
void afeCore_calibrationTask( void* pvParameter )
{
    while( !mutex_take() ) { DELAY(100); }

    if( pvParameter == NULL ) { vTaskDelete(NULL); }

    QueueHandle_t q = *(QueueHandle_t*)pvParameter;

    while( !afeCore_isInitialized() )
    {
        exitIf( getinputs( q ).inputs & BTN_ESC );
    }

    tft.fillScreen( TFT_BLACK );
    tft.setTextSize( TFT_LARGE );
    tft.setTextColor( TFT_WHITE );
    tft.setTextDatum( MC_DATUM );
    tft.drawString( "CALIBRATION", xMidPoint, 1*yOffset );
    tft.setTextSize( TFT_MEDIUM );
    tft.drawString( "By pressing enter you", xMidPoint, 2*yOffset );
    tft.drawString( "start the calibration,", xMidPoint, 3*yOffset );
    tft.drawString( "and the previous", xMidPoint, 4*yOffset );
    tft.drawString( "calibration will be lost.", xMidPoint, 5*yOffset );
    tft.setTextSize( TFT_SMALL ); 
    tft.drawString( "Press enter to continue ==>>", xMidPoint, 6*yOffset );

    clearInputBuffer( q );

    for(;;)
    {
        hmiEventData_t e = getinputs( q );
        exitIf( e.inputs & BTN_ESC );
        if( e.inputs & BTN_ENTER ) { break; }
        if( e.inputs & BTN_RIGHT ) 
        { 
            tft.setTextSize( TFT_SMALL );
            tft.setTextDatum( ML_DATUM );
            tft.setTextColor( TFT_BLACK );
            tft.drawString( "5V", 5, 10 );
            tft.setTextColor( TFT_WHITE );
            tft.drawString( "15V", 5, 10 );
            tft.setTextSize( TFT_MEDIUM );
            tft.setTextDatum( MC_DATUM );
            afeCore_setChannelRange( RANGE_15V, CHANNEL_1 ); 
            afeCore_setChannelRange( RANGE_15V, CHANNEL_2 ); 
        }
        if( e.inputs & BTN_LEFT ) 
        { 
            tft.setTextSize( TFT_SMALL );
            tft.setTextDatum( ML_DATUM );
            tft.setTextColor( TFT_BLACK );
            tft.drawString( "15V", 5, 10 );
            tft.setTextColor( TFT_WHITE );
            tft.drawString( "5V", 5, 10 );
            tft.setTextSize( TFT_MEDIUM );
            tft.setTextDatum( MC_DATUM );
            afeCore_setChannelRange( RANGE_5V, CHANNEL_1 ); 
            afeCore_setChannelRange( RANGE_5V, CHANNEL_2 ); 
        }
        drawVoltage();
    }
    isCalibrationDone = false;

    afeCore_resetCalibration();

    // First the high range is calibrated
    afeCore_setChannelRange( RANGE_15V, CHANNEL_1 );
    afeCore_setChannelRange( RANGE_15V, CHANNEL_2 );

    // zero offset is automatically set for the active range
    calibrateZeroOffset(q);
    calibrateRange( q, RANGE_15V );

    // Then the low range is calibrated
    afeCore_setChannelRange( RANGE_5V, CHANNEL_1 );
    afeCore_setChannelRange( RANGE_5V, CHANNEL_2 );
    
    // zero offset is automatically set for the active range
    calibrateZeroOffset(q);
    calibrateRange( q, RANGE_5V );

    isCalibrationDone = true;

    writeCalibrationData();

    exitIf( 1 );
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////