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
        - hmiCore_init() now returns QueueHandle_t instead of void

    18.04.2026 wawa2024
       - Added getinput function for queue handling
*/

#ifndef HMI_CORE_H
#define HMI_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Pin numbers for keyboard scanner
#define PL          22
#define DATA_OUT    36
#define CLK         2
#define ROW_SEL     32

// Pin numbers for encoder
#define ENC_A       38
#define ENC_B       39

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

typedef enum
{
    E_NONE = 0,
    E_PRESSED,
    E_HOLD,
    E_HOLD_RELEASE,
    E_TURNED_CW,
    E_TURNED_CCW,
    E_LAST_EVENT
} hmiEvent_t;

typedef struct
{
    hmiEvent_t event;
    uint32_t inputs;
} hmiEventData_t;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
    extern "C" {
#endif

// Initializes the hmiCore (hmiCore uses hardware timer1 internally,
// also creates a freertos task). The threshold parameters tell how many
// milliseconds the input needs to be present or absent before the given
// event is triggered.
//
// Returns a handle to a FreeRTOS queue which can be used to read event data
// instead of a callback
extern QueueHandle_t hmiCore_init( uint32_t pressThresholdMs,
                                   uint32_t holdThresholdMs,
                                   uint32_t holdReleaseThresholdMs );

// Deinitializes the hmiCore ( pauses the hardware timer used and
// removes the freertos task used to implement all of the functionality )
extern void hmiCore_deinit(void);

// Initializes the callback, which is used to get the event data from hmiCore
extern void hmiCore_attachEventCallback( void (*callback)(hmiEventData_t e) );

// Returns true if the given input and event are found within
// the given hmiEventData_t struct.
extern bool hmiCore_eventFound( hmiEventData_t e, hmiEvent_t event,
                                uint32_t input );

// Reads a hmiEventData_t queue to end, returns last element
extern hmiEventData_t getinputs(QueueHandle_t q);

// Reads a hmiEventData_t queue to end, returns last element
extern hmiEventData_t get_held_keys(QueueHandle_t q);

#ifdef __cplusplus
}
#endif

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#endif
