////////////////////////////////////////////////////////////////////////
// @file main.cpp
// Main program
///////////////////////////// 1.Libraries //////////////////////////////

#include <esp32-oscilloscope.h>
#include <hmiCore.h>

#include "appCore/serial_task.h"
#include "appCore/menu_task.h"

#include <afeCore.h>
#include <telnetCore.h>

/////////////////////////////// 2.Macros ///////////////////////////////
/////////////////////////////// 3.Types ////////////////////////////////
//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////
//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);
SemaphoreHandle_t screen_mutex = xSemaphoreCreateMutex();
SemaphoreHandle_t inputs_mutex = xSemaphoreCreateMutex();
SemaphoreHandle_t serial_mutex = xSemaphoreCreateMutex();

//////////////////////////// 5.2.Functions /////////////////////////////

bool mutex_delete(){
  vSemaphoreDelete(screen_mutex);
  vSemaphoreDelete(inputs_mutex);
  return true;
}

bool mutex_create(){
  screen_mutex = xSemaphoreCreateMutex();
  inputs_mutex = xSemaphoreCreateMutex();
  return true;
}

bool mutex_take(){
  return
      ( xSemaphoreTake(screen_mutex,0) == pdTRUE ) &&
      ( xSemaphoreTake(inputs_mutex,0) == pdTRUE )
    ;
}

bool mutex_take(SemaphoreHandle_t m){
  return xSemaphoreTake(m,0) == pdTRUE;
}

bool mutex_release(){
  return
    ( xSemaphoreGive(screen_mutex) == pdTRUE ) &&
    ( xSemaphoreGive(inputs_mutex) == pdTRUE )
    ;
}

bool mutex_release(SemaphoreHandle_t m){
  return xSemaphoreGive(m) == pdTRUE;
}

void reset() {
  tft.setTextFont(1); // GLCD, original Adafruit 8x5 font
  tft.fillScreen(TFT_BLACK); // fill screen black background
  tft.setTextSize(TFT_MEDIUM);
  tft.setCursor(0,0); // setCursor(x,y)
  tft.setTextColor(TFT_WHITE, TFT_BLACK); // setTextColor(fg,bg)
}

void setup() {

  Serial.begin(115200);

  xTaskCreate(serial_task,"serial_task",4096,NULL,1,NULL);

  tft.init();
  tft.setRotation(3);

  reset();
  tft.drawCentreString("SETUP BOOTED",RESOLUTION_X/2,RESOLUTION_Y/2,1);
  // ^drawCentreString(string,x,y,font_px_size)

  wifi_init();
  telnet_init();

  //afeCore_init();
  DELAY(2000);
}

void loop(){
  //menu_task();
  DELAY(10);
}
