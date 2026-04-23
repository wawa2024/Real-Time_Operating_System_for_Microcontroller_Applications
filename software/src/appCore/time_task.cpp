////////////////////////////////////////////////////////////////////////
// @file time_task.cpp
// Time task
///////////////////////////// 1.Libraries //////////////////////////////

#include <stdint.h>
#include <esp32-oscilloscope.h>
#include "time_task.h"

/////////////////////////////// 2.Macros ///////////////////////////////

#define REFRESH_RATE_MS 20

/////////////////////////////// 3.Types ////////////////////////////////
//////////////////////////// 4.Declarations ////////////////////////////

Time current_time;

//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////
//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////

static QueueHandle_t time_rx_q = xQueueCreate(1,sizeof(timeRequest_t));
static QueueHandle_t time_tx_q = xQueueCreate(1,sizeof(timeResponse_t));
static SemaphoreHandle_t time_mutex = xSemaphoreCreateMutex();

static const char* TAG = "time_task";

//////////////////////////// 5.2.Functions /////////////////////////////

/**
 * @brief Converts a double digit string to byte value.
 *
 * @param p Constant string value.
 *
 * @return Returns uint8_t value, from 0-99.
 */
uint8_t conv2d(const char* p) {
  uint8_t v = 0;
  if ('0' <= *p && *p <= '9')
    v = *p - '0';
  return 10 * v + *++p - '0';
}

static void time_increment(TimerHandle_t xTimer){
  current_time.ss++; // increment second
  if ( current_time.ss > 59 ) {
    current_time.ss = 0;
    current_time.mm++; // increment minute
    if( current_time.mm > 59 ) {
      current_time.mm = 0;
      current_time.hh++; // increment hour
      if( current_time.hh > 23 ) {
        current_time.hh = 0;
      }
    }
  }
}

timeResponse_t time_request(timeRequest_t request){

  timeResponse_t response = { .time = {-1,-1,-1} };

  if(xSemaphoreTake(time_mutex,pdMS_TO_TICKS(20)) == pdTRUE)
    if(xQueueSend(time_rx_q,&request,pdMS_TO_TICKS(20)) == pdPASS)
      if(xQueueReceive(time_tx_q,&response,pdMS_TO_TICKS(20)) == pdPASS);

  xSemaphoreGive(time_mutex);
  return response;
}

void time_task(void* pvParameter) {

  #ifdef DEBUG
  ESP_LOGI(TAG,MSG_LAUNCHED);
  #endif

  TimerHandle_t timer = xTimerCreate("time_increment", pdMS_TO_TICKS(1000), pdTRUE, NULL, time_increment);
  xTimerStart(timer,0);

  while(true){

    timeRequest_t request;
    timeResponse_t response;

    xQueueReceive(time_rx_q,&request,pdMS_TO_TICKS(REFRESH_RATE_MS));

    timeMethod_t& method = request.method;

    switch(method){
    case TIME_STOP:
      xTimerStop(timer,0);
      break;
    case TIME_START:
      xTimerStart(timer,0);
      break;
    default:
      break;
    }

    switch(method){
    case TIME_READ:
    case TIME_STOP:
    case TIME_START:
      response.time = current_time;
      while(xQueueSend(time_tx_q,&response,0) == pdFALSE) DELAY(20);
      break;
    default:
      break;
    }

    DELAY(REFRESH_RATE_MS);
  }

  #ifdef DEBUG
  ESP_LOGI(TAG,MSG_DELETED);
  #endif

}
