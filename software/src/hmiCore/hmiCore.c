/*
 * File:        hmiCore.h
 * Author:      Juho Rantsi
 * Created:     25.02.2026
 * Description: 
        hmiCore is a library / module for all custom human interface inputs that
        are needed in esp32-oscilloscope project. E.g buttons, encoders etc...
 *
 * Licensed under the GNU General Public License version 2.
 * See the accompanying LICENSE file for full details.
 *
 *******************************************************************************
 *******************************************************************************
 
 Version history:
 
    25.02.2026 JR   
        - Library created

*/

#include "hmiCore.h"

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// esp32 hardware timers
#include "driver/timer.h"

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#define LOCAL                   static inline  

// Number of user inputs present
#define NUMBER_OF_INPUTS        16

#define DEFAULT_PRESSED_SAMPLES         10
#define DEFAULT_HOLD_SAMPLES            200  
#define DEFAULT_HOLD_RELEASE_SAMPLES    10

#define setHigh(x)              digitalWrite(x, HIGH)
#define setLow(x)               digitalWrite(x, LOW)
#define read(x)                 digitalRead(x)
#define delay_us(x)             delayMicroseconds(x)
#define delay_ms(x)             delay(x)

#define clkPulse(x)             do{\
                                    setHigh(CLK);\
                                    delay_us(x);\
                                    setLow(CLK);\
                                    delay_us(x);\
                                } while(0)

#define parallelLoadPulse(x)    do{\
                                    setLow(PL);\
                                    delay_us(x);\
                                    setHigh(PL);\
                                } while(0)

#define switchRow(x)            do{\
                                    if( read(ROW_SEL) ) setLow(ROW_SEL);\
                                    else setHigh(ROW_SEL);\
                                    delay_us(x);\
                                } while(0)

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

typedef enum
{
    STATUS_CLEAR,
    STATUS_HOLD,
} inputStatus_t;

typedef struct 
{
    // Last sample taken from inputs
    uint32_t sample;

    // How many consecutive samples need to be 
    // ones so that input is considered pressed
    uint32_t pressedSamples;
    
    // How many consecutive samples need to be 
    // ones for hold to be triggered
    uint32_t holdSamples;

    // How many consecutive samples need to be 
    // zeroes for hold release to be triggered
    uint32_t holdReleaseSamples;

    // Freertos task handle for hmiHandler
    TaskHandle_t handle;

    // Callback used to dispatch hmi events
    void (*callback)(hmiEventData_t e);

} hmiCore_t;

typedef struct
{
    // current status of input
    inputStatus_t status;

    // Number of consecutive 0's and 1's respectively
    uint32_t c0;
    uint32_t c1;

} hmiInput_t;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

static hmiCore_t hmiCore = 
{
    .sample = 0,
    .pressedSamples = DEFAULT_PRESSED_SAMPLES,
    .holdSamples = DEFAULT_HOLD_SAMPLES,
    .holdReleaseSamples = DEFAULT_HOLD_RELEASE_SAMPLES,
    .handle = 0,
    .callback = NULL
}; 

static hmiInput_t inputs[NUMBER_OF_INPUTS] = {};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

LOCAL void hmiHandler( void * pvParameters );
static void IRAM_ATTR timer_isr( void *arg );

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void hmiCore_init( uint32_t pressThresholdMs, uint32_t holdThresholdMs, 
                   uint32_t holdReleaseThresholdMs )
{
    if( pressThresholdMs != 0 ) { hmiCore.pressedSamples = pressThresholdMs; }
    if( holdThresholdMs != 0 )  { hmiCore.holdSamples = holdThresholdMs; }
    if( holdReleaseThresholdMs != 0 ) 
    { 
        hmiCore.holdReleaseSamples = holdReleaseThresholdMs; 
    }

    pinMode(PL, OUTPUT);
    pinMode(CLK, OUTPUT);
    pinMode(ROW_SEL, OUTPUT);
    pinMode(DATA_OUT, INPUT);

    digitalWrite(PL, HIGH);
    digitalWrite(CLK, LOW);
    digitalWrite(ROW_SEL, LOW);

    xTaskCreate( hmiHandler, "HMI_HANDLER", 4096, NULL, 
                 tskIDLE_PRIORITY, &hmiCore.handle );

    // (80 MHz / 80) = 1 MHz timer clock + 1000us interval = 1kHz timer
    static const uint32_t timerDivider = 80;  
    static const uint32_t timerInterval_us = 1000;

    // Initialize esp32 hardware timer for 1kHz 
    timer_config_t config = 
    {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_EN,
        .divider = timerDivider
    };

    timer_init( TIMER_GROUP_1, TIMER_1, &config );

    timer_set_counter_value(TIMER_GROUP_1, TIMER_1, 0 );

    timer_set_alarm_value( TIMER_GROUP_1, TIMER_1, timerInterval_us );

    timer_isr_register( TIMER_GROUP_1, TIMER_1, timer_isr, NULL, 
                        ESP_INTR_FLAG_IRAM, NULL );

    timer_start( TIMER_GROUP_1, TIMER_1 );

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void hmiCore_deinit(void)
{
    timer_pause( TIMER_GROUP_1, TIMER_1 );
    vTaskDelete( hmiCore.handle );
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void hmiCore_attachEventCallback( void (*callback)(hmiEventData_t e) )
{
    if( callback != NULL ) { hmiCore.callback = callback; }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

inline bool hmiCore_eventFound( hmiEventData_t e, hmiEvent_t event, 
                                uint32_t input )
{
    if( e.event == event )
    {
        return e.inputs & 1 << input ? true : false;
    }

    return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// delay used between output level changes in the scanKeyboard function (µs)
static const uint32_t scanningDelay = 0;

LOCAL uint32_t scanKeyboard()
{
    uint32_t temp = 0;

    // toggle row selection
    switchRow(scanningDelay);

    parallelLoadPulse(scanningDelay);

    for( uint32_t i = 0; i < 8; i++ )
    {
        temp |= (read(DATA_OUT) << i);
        clkPulse(scanningDelay);
    }

    // toggle row selection
    switchRow(scanningDelay);

    // parallel load (transition from high to low)
    parallelLoadPulse(scanningDelay);

    for( uint32_t i = 8; i < 16; i++ )
    {
        temp |= (read(DATA_OUT) << i);
        clkPulse(scanningDelay);
    }

    return temp;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

LOCAL hmiEvent_t detectEvent( uint32_t inputIndex )
{
    hmiInput_t *input = &inputs[inputIndex];

    if( hmiCore.sample & (1 << inputIndex) )
    {
        input->c0 = 0;
        input->c1++;
    }
    else
    { 
        input->c0++; 

        // Handle press
        if( input->c1 >= hmiCore.pressedSamples 
        &&  input->status == STATUS_CLEAR )
        {
            input->c1 = 0;
            return E_PRESSED;
        }

        input->c1 = 0;
    }

    // Handle hold release
    if( input->status == STATUS_HOLD )
    {
        if( input->c0 >= hmiCore.holdReleaseSamples )
        {
            input->status = STATUS_CLEAR;
            return E_HOLD_RELEASE;
        }
        else { return E_NONE; }
    }

    // Handle hold
    if( input->c1 >= hmiCore.holdSamples )
    {
        // No need to reset c1, because status changes to hold
        input->status = STATUS_HOLD;
        return E_HOLD;
    }

    return E_NONE;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

static void IRAM_ATTR timer_isr( void *arg )
{
    // Clear interrupt
    TIMERG1.int_clr_timers.t1 = 1;

    // Re-enable alarm
    TIMERG1.hw_timer[TIMER_1].config.alarm_en = TIMER_ALARM_EN;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Notify hmiHandler
    vTaskNotifyGiveFromISR( hmiCore.handle, &xHigherPriorityTaskWoken );

    if( xHigherPriorityTaskWoken ) { portYIELD_FROM_ISR(); }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

LOCAL void hmiHandler( void * pvParameters )
{
    (void) pvParameters;

    for(;;)
    {
        // Blocks execution until a notification is 
        // received from hardware timer based isr
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

        // Store inputs that triggered a given event
        uint32_t inputEvents[E_LAST_EVENT] = {0};

        // Read new sample
        hmiCore.sample = scanKeyboard();

        // Detect events for each input
        for( uint32_t i = 0; i < NUMBER_OF_INPUTS; i++ )
        {
            hmiEvent_t e = detectEvent(i);
            inputEvents[e] |= (1 << i);
        }

        if( hmiCore.callback != NULL )
        {
            // Call the callback with all new events
            for( uint32_t e = E_LAST_EVENT - 1; e > E_NONE; e-- )
            {
                if( inputEvents[e] != 0 ) 
                {
                    hmiEventData_t event = 
                    { 
                        .event = (hmiEvent_t)e, 
                        .inputs = inputEvents[e],
                    }; 

                    hmiCore.callback(event); 
                }
            }
        }
    }
}
