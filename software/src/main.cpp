////////////////////////////////////////////////////////////////////////
// @file main.cpp
// Main program
///////////////////////////// 1.Libraries //////////////////////////////

#include <esp32-oscilloscope.h>
#include <hmiCore.h>

#include "appCore/menu_task.h"

/////////////////////////////// 2.Macros ///////////////////////////////
/////////////////////////////// 3.Types ////////////////////////////////
//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////
//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////

TFT_eSPI tft = TFT_eSPI();
SemaphoreHandle_t screen_mutex = xSemaphoreCreateMutex();
SemaphoreHandle_t inputs_mutex = xSemaphoreCreateMutex();
SemaphoreHandle_t serial_mutex = xSemaphoreCreateMutex();

//////////////////////////// 5.2.Functions /////////////////////////////

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

  tft.init();
  tft.setRotation(3);

  reset();

  int columns = (RESOLUTION_X/WIDTH_MEDIUM);
  int lines = (RESOLUTION_Y/HEIGHT_MEDIUM);

  for(int i = 0 ; i < lines ; i++){
    int ypos = i * HEIGHT_MEDIUM;
    tft.setCursor(0,ypos);
    if ( i == 0 || i == (lines-1) ) {
      tft.print("+");
      for( int j = 0 ; j < (columns-2) ; j++){
        tft.print("-");
      }
      tft.print("+");
    } else {
      tft.print("|");
      for( int j = 0 ; j < (columns-2) ; j++){
        tft.print(" ");
      }
      tft.print("|");
    }
  }
  tft.drawCentreString("SETUP BOOTED",RESOLUTION_X/2,RESOLUTION_Y/2,1);
  // ^drawCentreString(string,x,y,font_px_size)

  DELAY(2000);
}

extern void loop(); // menu_task
