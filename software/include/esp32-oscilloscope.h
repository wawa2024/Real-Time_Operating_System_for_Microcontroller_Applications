////////////////////////////////////////////////////////////////////////
// @file esp32-oscilloscope.h
// Device related macros
///////////////////////////// 1.Libraries //////////////////////////////

#ifdef __cplusplus
#include <Arduino.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#else
#include <stdbool.h>
#endif

#ifndef ESP32_OSCILLOSCOPE_H
#define ESP32_OSCILLOSCOPE_H

/////////////////////////////// 2.Macros ///////////////////////////////

#define LED_BUILTIN 22
#define DELAY(X) vTaskDelay(X / portTICK_PERIOD_MS)

#ifdef TFT_DISPLAY
#define RESOLUTION_X 320
#define RESOLUTION_Y 240
#else
#define RESOLUTION_X 256
#define RESOLUTION_Y 128
#endif

/**
  TFT Font Sizes
  1: small size (character height: 8 pixels)
  2: medium size (character height: 16 pixels)
  3: large size (character height: 24 pixels)
  4: extra large size (character height: 32 pixels)
  5: extra extra large size (character height: 40 pixels)
  6: extra extra extra large size (character height: 48 pixels)
  7: extra extra extra extra large size (character height: 56 pixels)

  See: https://doc-tft-espi.readthedocs.io/tft_espi/methods/settextsize/
*/
#define TFT_SMALL 1
#define TFT_MEDIUM 2
#define TFT_LARGE 3

#define HEIGHT_SMALL 8
#define WIDTH_MEDIUM 12
#define HEIGHT_MEDIUM 16
#define HEIGHT_LARGE 24

/**
   esp32-oscilloscope buttons
 */
#define BTN_RIGHT 1
#define BTN_UP 2
#define BTN_DOWN 4
#define BTN_CH2 8
#define BTN_SCALE 16
#define BTN_MATH 32
#define BTN_TRIGGER 64
#define BTN_MEASURE 128
#define BTN_ENTER 256
#define BTN_ESC 512
#define BTN_LEFT 1024
#define BTN_CH1 2048
#define BTN_CURSORS 4096
#define BTN_TRIGSET 8192
#define BTN_STOP 16384
#define BTN_AUTOSET 32768

/////////////////////////////// 3.Types ////////////////////////////////
//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////

#ifdef DEBUG
#define MSG_LAUNCHED "launched"
#define MSG_DELETED "deleted"
#endif

#ifdef __cplusplus
extern TFT_eSPI tft;
extern TFT_eSprite spr;
extern SemaphoreHandle_t screen_mutex;
extern SemaphoreHandle_t inputs_mutex;
extern SemaphoreHandle_t serial_mutex;
#endif

//////////////////////////// 4.2.Functions /////////////////////////////

extern void reset();
extern bool mutex_take();
extern bool mutex_release();
extern bool mutex_delete();
extern bool mutex_create();

#ifdef __cplusplus
extern bool mutex_take(SemaphoreHandle_t);
extern bool mutex_release(SemaphoreHandle_t);
#endif

//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////
//////////////////////////// 5.2.Functions /////////////////////////////

#endif
