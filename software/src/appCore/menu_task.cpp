////////////////////////////////////////////////////////////////////////
// @file menu_task.cpp
// Menu task
///////////////////////////// 1.Libraries //////////////////////////////

#include <esp32-oscilloscope.h>
#include <hmiCore.h>

#include "snake_task.h"
#include "graph_task.h"
#include "serial_task.h"
#include "menu_task.h"
#include "afeCore.h"

#include "telnet_task.h"

/////////////////////////////// 2.Macros ///////////////////////////////

#define REFRESH_RATE_MS 170
#define ITEM_SIZE sizeof(hmiEventData_t)
#define DEFAULT_INDEX 1

/////////////////////////////// 3.Types ////////////////////////////////
//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////

static void info_task(void*);

//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////

menu_t items[MENU_TASKS] = {
  {info_task,(char*)"About version", 4096}
  ,{oscilloscope_task,(char*)"Oscilloscope", 16384}
  ,{snake_task,(char*)"Snake", 16384}
  ,{telnet_task,(char*)"Telnet",16384}
  ,{afeCore_calibrationTask, (char*)"AFE Cal", 16384}
};
static const size_t items_num = sizeof(items)/sizeof(menu_t);

//////////////////////////// 5.2.Functions /////////////////////////////

/**
 * Example menu task structure
 */
static void info_task(void* pvParameter){

  static const char* TAG = "info_task";

#ifdef DEBUG
  ESP_LOGI(TAG,MSG_LAUNCHED);
#endif

  QueueHandle_t q = *(QueueHandle_t*)pvParameter;

  // attempt to take mutex
  while(not mutex_take()) DELAY(100);

  reset();

  tft.print("Version: ");
  tft.println(__DATE__);
  tft.print("Number of menu items: ");
  tft.println(items_num);

  // Wait for ESC button press
  while(true){
    uint32_t inputs = getinputs(q).inputs;
    if( inputs & BTN_ESC ) break;
    DELAY(20);
  }

  mutex_release();

#ifdef DEBUG
  ESP_LOGI(TAG,MSG_DELETED);
#endif

  vTaskDelete(NULL); // self-delete

}

static void draw(uint16_t index) {

  // attempt to take mutex
  if (xSemaphoreTake(screen_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {

    reset();

    for( uint16_t i = 0 ; i < items_num ; i++ ){

      if( index == i )
        tft.print("> ");
      else
        tft.print("  ");

      tft.println(items[i].title);

    }

    xSemaphoreGive(screen_mutex); // release mutex

  }

}

static bool isChildDead(TaskHandle_t t){
  eTaskState s = eTaskGetState(t);
  return t == NULL ? false : s == eDeleted;
}

void menu_task(void){

  QueueHandle_t q = hmiCore_init(0,0,0);
  uint16_t index = 0;
  TaskHandle_t xHandle = NULL;

  menu_t& default_item = items[DEFAULT_INDEX];
  xTaskCreate(default_item.task
              ,default_item.title
              ,default_item.stack_size
              ,&q
              ,1
              ,&xHandle
              );

  while(not isChildDead(xHandle)) DELAY(REFRESH_RATE_MS);

  xHandle = NULL;
  draw(index);

  while (true) {

    hmiEventData_t data = {};
    uint32_t& inputs = data.inputs;

    if (xSemaphoreTake(inputs_mutex,pdMS_TO_TICKS(100)) == pdTRUE) {
      data = getinputs(q);
      xSemaphoreGive(inputs_mutex);
    }

    if ( inputs & BTN_UP ) {

      index = index <= 0 ? (items_num-1) : (index-1) ;

    } else if ( inputs & BTN_DOWN ) {

      index = index >= (items_num-1) ? 0 : (index+1) ;

    } else if ( inputs & BTN_ENTER ) {

      menu_t& item = items[index];
      if( item.task != nullptr )
      {
        xTaskCreate(item.task
                    ,item.title
                    ,item.stack_size
                    ,&q
                    ,1
                    ,&xHandle
                    );

        draw(index);
      }

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
