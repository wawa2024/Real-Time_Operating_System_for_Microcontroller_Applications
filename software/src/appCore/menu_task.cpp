////////////////////////////////////////////////////////////////////////
// @file menu_task.cpp
// Menu task
///////////////////////////// 1.Libraries //////////////////////////////

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>

#define TFT_DISPLAY
#include <esp32-oscilloscope.h>

#include <hmiCore.h>
#include "snake_task.h"
#include "graph_task.h"

/////////////////////////////// 2.Macros ///////////////////////////////

#define REFRESH_RATE_MS 170
#define ITEM_SIZE sizeof(hmiEventData_t)
#define DELAY(X) vTaskDelay(X / portTICK_PERIOD_MS)

/////////////////////////////// 3.Types ////////////////////////////////

typedef struct {
  void (*task)(void*);
  char* title;
  uint32_t stack_size;
} menu_t;

//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////

static void info(void*);

//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////

static menu_t items[] = {
  {info,"About version", 1024}
  ,{ui_task,"Oscilloscope", 16384}
  ,{snake_task,"Snake", 16384}
};
static const size_t items_num = sizeof(items)/sizeof(menu_t);

//////////////////////////// 5.2.Functions /////////////////////////////

/**
 * Example menu task structure
 */
static void info(void* pvParameter){

  #ifdef DEBUG
  Serial.println("[info_task]: launched");
  #endif

  QueueHandle_t q = *(QueueHandle_t*)pvParameter;
  // attempt to take mutex
  while(true){
    if (mutex_take()){

      reset();

      tft.print("Version: ");
      tft.println(__DATE__);
      tft.print("Number of menu items: ");
      tft.println(items_num);

      // Wait for ESC button press
      for(hmiEventData_t data = getinputs(q); !(data.inputs & BTN_ESC); data = getinputs(q));

      mutex_release();
      #ifdef DEBUG
      Serial.println("[info_task]: self-deleting");
      #endif
      vTaskDelete(NULL); // self-delete
      break;
    } else {
      DELAY(160);
    }
  }
}

static void draw(uint16_t index) {

  // attempt to take mutex
  if (xSemaphoreTake(screen_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {

    reset();

    for( uint16_t i = 0 ; i < items_num ; i++ ){

      tft.setCursor(0,i*HEIGHT_MEDIUM);

      if( index == i )
        tft.print("> ");
      else
        tft.print("  ");

      tft.setCursor(2*WIDTH_MEDIUM,i*HEIGHT_MEDIUM);
      tft.print(items[i].title);

    }

    xSemaphoreGive(screen_mutex); // release mutex

  }

}

void loop(void) {

  QueueHandle_t q = hmiCore_init(100,250,100);
  uint16_t index = 0;
  TaskHandle_t xHandle = NULL;

  draw(index);

  while (true) {

    hmiEventData_t data = {};
    uint32_t& inputs = data.inputs;

    if (xSemaphoreTake(inputs_mutex,0) == pdTRUE) {
      data = getinputs(q);
      xSemaphoreGive(inputs_mutex);
    }

    if ( inputs & BTN_UP ) {

      index = index <= 0 ? (items_num-1) : (index-1) ;

    } else if ( inputs & BTN_DOWN ) {

      index = index >= (items_num-1) ? 0 : (index+1) ;

    } else if ( inputs & BTN_ENTER ) {

      menu_t& item = items[index];
      xTaskCreate(item.task
                  ,item.title
                  ,item.stack_size
                  ,&q
                  ,1
                  ,&xHandle
                  );

      draw(index);

    }

    if ( inputs & BTN_UP || inputs & BTN_DOWN ) {
      draw(index);
    }

    if(xHandle){
      eTaskState s = eTaskGetState(xHandle);
      if (s == eDeleted) {
        xHandle = NULL;
        draw(index);
      }
    }

    DELAY(REFRESH_RATE_MS);

  }

  hmiCore_deinit();

}
