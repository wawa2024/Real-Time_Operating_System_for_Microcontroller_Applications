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

#ifndef HMI_CORE_H
#define HMI_CORE_H

#include <stdint.h>
#include <stdbool.h>
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Pin numbers for keyboard scanner
#define PL          22     
#define DATA_OUT    36
#define CLK         2
#define ROW_SEL     32

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

extern void hmiCore_init( uint32_t pressThresholdMs, uint32_t holdThresholdMs, 
                          uint32_t holdReleaseThresholdMs );

extern void hmiCore_deinit(void);

// you should not take a semaphore in the callback or update the screen etc...
// only change a value of a variable or a status etc...
extern void hmiCore_attachEventCallback( void (*callback)(hmiEventData_t e) );

extern bool hmiCore_eventFound( hmiEventData_t e, hmiEvent_t event, 
                                uint32_t input );

#ifdef __cplusplus
}
#endif

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#endif