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

    08.04.2026 JR
        - Hardware timer code removed and replaced with FreeRTOS APIs
        - hmiCore_init() now returns QueueHandle_t instead of void
        - Added the ability to receive event data through FreeRTOS queue

    18.04.2026 wawa2024
       - Added getinput function for queue handling

    21.04.2026 JR
        - Changed default event detection times
        - Removed unused code
        - Added a default argument to encoder state switch
*/

#include <hmiCore.h>

// only used because was unable to set
// ROW_SEL as output with esp idf api
#include <Arduino.h>

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#define LOCAL                   static inline

// Number of user inputs present
#define NUMBER_OF_INPUTS        16

// These values are in milli seconds if sampling frequency is 1kHz
#define DEFAULT_PRESSED_SAMPLES         50
#define DEFAULT_HOLD_SAMPLES            500
#define DEFAULT_HOLD_RELEASE_SAMPLES    30

#define setHigh(x)              gpio_set_level((gpio_num_t)x, 1)
#define setLow(x)               gpio_set_level((gpio_num_t)x, 0)
#define read(x)                 gpio_get_level((gpio_num_t)x)
#define delay_us(x)             //ets_delay_us(x)
#define delay_ms(x)             vTaskDelay(pdMS_TO_TICKS(x))

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
    ENC_S1 = 0,
    ENC_S2 = 1,
    ENC_S3 = 3,
    ENC_S4 = 2
} encoderState_t;

typedef enum
{
    STATUS_CLEAR,
    STATUS_HOLD,
} inputStatus_t;

typedef struct
{
    // Last sample taken from inputs
    uint32_t sample;

    struct
    {
        encoderState_t newState;
        encoderState_t oldState;
    } enc;

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

    // Freertos queue handle for events
    QueueHandle_t xEventQueue;

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
    .enc =
    {
        .newState = ENC_S1,
        .oldState = ENC_S2
    },
    .pressedSamples = DEFAULT_PRESSED_SAMPLES,
    .holdSamples = DEFAULT_HOLD_SAMPLES,
    .holdReleaseSamples = DEFAULT_HOLD_RELEASE_SAMPLES,
    .handle = 0,
    .xEventQueue = 0,
    .callback = NULL
};

static hmiInput_t inputs[NUMBER_OF_INPUTS] = {};

// Event queue
#define QUEUE_LENGTH        100
#define QUEUE_ITEM_SIZE     sizeof( hmiEventData_t )
static StaticQueue_t xStaticQueue;
uint8_t ucQueueStorageArea[ QUEUE_LENGTH * QUEUE_ITEM_SIZE ];

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

LOCAL void hmiHandler( void * pvParameters );

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

QueueHandle_t hmiCore_init( uint32_t pressThresholdMs, uint32_t holdThresholdMs,
                            uint32_t holdReleaseThresholdMs )
{
    if( pressThresholdMs != 0 ) { hmiCore.pressedSamples = pressThresholdMs; }
    if( holdThresholdMs != 0 )  { hmiCore.holdSamples = holdThresholdMs; }
    if( holdReleaseThresholdMs != 0 )
    {
        hmiCore.holdReleaseSamples = holdReleaseThresholdMs;
    }

    // Configure output pins
    gpio_config_t outputConfig = {
        .pin_bit_mask = (1ULL << PL) | (1ULL << CLK) | (1ULL << ROW_SEL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config( &outputConfig );

    // Configure input pins
    gpio_config_t inputConfig = {
        .pin_bit_mask = (1ULL << DATA_OUT) | (1ULL << ENC_A) | (1ULL << ENC_B),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config( &inputConfig );

    // Was not able to use esp idf to set ROW_SEL as output even after
    // trying to reset the pin config, disabling rtc and adc on the pin etc
    pinMode( ROW_SEL, OUTPUT );

    setHigh(PL);
    setLow(CLK);
    setLow(ROW_SEL);

    // Initialize encoder state without creating event
    hmiCore.enc.oldState = (encoderState_t)(read(ENC_B) << 1 | read(ENC_A));

    hmiCore.xEventQueue = xQueueCreateStatic( QUEUE_LENGTH,
                                              QUEUE_ITEM_SIZE,
                                              ucQueueStorageArea,
                                              &xStaticQueue );

    xTaskCreate( hmiHandler, "HMI_HANDLER", 4096, NULL,
                 tskIDLE_PRIORITY, &hmiCore.handle );

    return hmiCore.xEventQueue;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void hmiCore_deinit(void)
{
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

LOCAL void hmiHandler( void * pvParameters )
{
    (void)pvParameters;

    TickType_t xLastWakeTime = xTaskGetTickCount();
    static const TickType_t xTaskTimeout = 1;

    for(;;)
    {
        // Used to set a constant execution interval for the task
        (void)xTaskDelayUntil( &xLastWakeTime, pdMS_TO_TICKS( xTaskTimeout ) );

        // Store inputs that triggered a given event
        uint32_t inputEvents[E_LAST_EVENT] = {0};

        // Read new sample
        hmiCore.sample = scanKeyboard();

        // Detect events for each input ( encoder excluded )
        for( uint32_t i = 0; i < NUMBER_OF_INPUTS; i++ )
        {
            hmiEvent_t e = detectEvent(i);
            inputEvents[e] |= (1 << i);
        }

        // encoder
        hmiCore.enc.newState = (encoderState_t)(read(ENC_B) << 1 | read(ENC_A));

        // Update encoder state
        if( hmiCore.enc.newState != hmiCore.enc.oldState )
        {
            encoderState_t next, last;

            switch( hmiCore.enc.newState )
            {
                case ENC_S1:
                    next = ENC_S2;
                    last = ENC_S4;
                    break;

                case ENC_S2:
                    next = ENC_S3;
                    last = ENC_S1;
                    break;

                case ENC_S3:
                    next = ENC_S4;
                    last = ENC_S2;
                    break;

                case ENC_S4:
                    next = ENC_S1;
                    last = ENC_S3;
                    break;

                default:
                    // In case there is an error we do not want
                    // to think that encoder has been turned
                    next = hmiCore.enc.newState;
                    last = hmiCore.enc.newState;
                    break;
            }

            if( hmiCore.enc.oldState == last )
            {
              inputEvents[E_TURNED_CW] = (1 << 16);
            }
            else if( hmiCore.enc.oldState == next )
            {
                inputEvents[E_TURNED_CCW] = (1 << 16);
            }

            hmiCore.enc.oldState = hmiCore.enc.newState;
        }

        // Call callback with all new events and add events to the queue
        for( uint32_t e = E_LAST_EVENT - 1; e > E_NONE; e-- )
        {
            if( inputEvents[e] != 0 )
            {
                hmiEventData_t event =
                {
                    .event = (hmiEvent_t)e,
                    .inputs = inputEvents[e],
                };

                if( hmiCore.callback != NULL ) { hmiCore.callback(event); }

                BaseType_t ret = xQueueSendToBack( hmiCore.xEventQueue,
                                                   (void*)&event, 0 );

                // If queue full, break out of the loop
                if( ret != pdPASS ) { break; }

            }
        }

    }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Reads a hmiEventData_t queue to end and returns last pressed button.
 * @param q QueueHandle_t
 * @return Returns a hmiEventData_t struct.
 */
hmiEventData_t getinputs(QueueHandle_t q){
  hmiEventData_t data = {E_NONE,0};
  hmiEventData_t tmp_data = {E_NONE,0};
  delay_ms(17); // 60 Hz
  while(xQueueReceive(q,&data,0) == pdTRUE)
  {
    if(tmp_data.event == E_PRESSED) data = tmp_data;  
  } 
  return data;
}
