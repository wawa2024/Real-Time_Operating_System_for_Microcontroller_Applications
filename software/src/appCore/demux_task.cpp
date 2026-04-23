////////////////////////////////////////////////////////////////////////
// SPDX-FileCopyrightText: Copyright © 2026, wawa2024. All rights reserved.
// SPDX-License-Identifier: GPL-2.0
/// @file demux_task.cpp
/// @date 2026-04-21
/// @author wawa2024
/// @brief Demux task that duplicates/deletes FreeRTOS Queues.
///////////////////////////// 1.Libraries //////////////////////////////

#include <esp32-oscilloscope.h>
#include "demux_task.h"

/////////////////////////////// 2.Macros ///////////////////////////////

#define DEMUX_COUNT 8

/////////////////////////////// 3.Types ////////////////////////////////

typedef struct {
  bool status;
  size_t i;
} demuxFind_t;

typedef struct {
  QueueHandle_t q0;
  QueueHandle_t q1;
  TaskHandle_t pid;
  size_t t_size;
} demuxData_t;

//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////
//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////

static QueueHandle_t demux_rx_q = xQueueCreate(1,sizeof(demuxRequest_t));
static QueueHandle_t demux_tx_q = xQueueCreate(1,sizeof(demuxResponse_t));
static SemaphoreHandle_t demux_mutex = xSemaphoreCreateMutex();

static demuxData_t buf[DEMUX_COUNT];

static const char* TAG = "demux_task";

//////////////////////////// 5.2.Functions /////////////////////////////

static bool pushBuf(demuxData_t data, size_t i){
  if(buf[i].pid == NULL){
    buf[i] = data;
    return true;
  } else {
    return false;
  }
}

static bool popBuf(size_t i){
  if ( i < sizeof(buf)/sizeof(demuxData_t) ){
    buf[i] = { NULL, NULL, NULL , 0};
    return true;
  }
  return false;
}

static demuxFind_t freeBuf(){
  for(size_t i = 0 ; i < sizeof(buf)/sizeof(demuxData_t) ; i++){
    if( buf[i].pid == NULL ){
      return { true, i };
    }
  }
  return { false, 0 };
}

static demuxFind_t findBuf(TaskHandle_t pid){
  for(size_t i = 0 ; i < sizeof(buf)/sizeof(demuxData_t) ; i++){
    if( buf[i].pid == pid ){
      return { true, i };
    }
  }
  return { false, 0 };
}

static demuxResponse_t demux_del_q(const demuxRequest_t& request){

  demuxResponse_t response = { NULL, false };

  demuxFind_t m = findBuf(request.pid);

  if(not m.status) return response;

  demuxData_t data = buf[m.i];

  return { data.q0, popBuf(m.i) };
}

static demuxResponse_t demux_dup_q(const demuxRequest_t& request){

  demuxResponse_t response = { NULL, false };

  demuxFind_t m = freeBuf();

  if(not m.status) return response;

  response.status = true;

  QueueHandle_t q = xQueueCreate(1,request.t_size);

  demuxData_t data = { .q0 = request.q
                     , .q1 = q
                     , .pid = request.pid
                     , .t_size = request.t_size
  };

  if(not pushBuf(data,m.i)) return response;

  response = { q, true };

  return response;
}

static demuxResponse_t demux(const demuxRequest_t& request){

  switch(request.method){

  case DEMUX_DUP: return demux_dup_q(request); break;

  case DEMUX_DEL: return demux_del_q(request); break;

  default: return {NULL, false}; break;

  }

}

demuxResponse_t demux_request(const demuxRequest_t request){

  demuxResponse_t response = { NULL, false };

  while(xSemaphoreTake(demux_mutex,0) == pdFALSE) DELAY(20);

  if(xQueueSend(demux_rx_q,&request,0) == errQUEUE_FULL){
    xSemaphoreGive(demux_mutex);
    return response;
  }

  while(xQueueReceive(demux_tx_q,&response,0) == pdFALSE) DELAY(20);

  xSemaphoreGive(demux_mutex);

  return response;
}

static void demux_init(){
#ifdef DEBUG
  ESP_LOGI(TAG,MSG_LAUNCHED);
#endif
}

static void demux_deinit(){
#ifdef DEBUG
  ESP_LOGI(TAG,MSG_DELETED);
#endif
}

void demux_task(void* pvParameter){

  demux_init();

  while(true){

    demuxRequest_t request;

    if( xQueueReceive(demux_rx_q,&request,0) == pdPASS ){


#ifdef DEBUG
      ESP_LOGI(TAG,"message recieved");
#endif

      demuxResponse_t response = demux(request);

#ifdef DEBUG
      ESP_LOGI(TAG,"sending message");
#endif

      xQueueSend(demux_tx_q,&response,0);

    }

    for(size_t i = 0 ; i < sizeof(buf)/sizeof(demuxData_t) ; i++){

      QueueHandle_t& q_in = buf[i].q0;

      if(q_in == NULL) continue;

      size_t& t_size = buf[i].t_size;
      void* bz = (void*)pvPortMalloc(t_size);

      if( xQueueReceive(q_in,bz,0) == pdPASS ){;
        for(size_t j = 0 ; j < sizeof(buf)/sizeof(demuxData_t) ; j++){

          QueueHandle_t& q0 = buf[j].q0;
          QueueHandle_t& q1 = buf[j].q1;

          if( q0 == q_in ) xQueueSend(q1,bz,0);

        }
      }

      vPortFree(bz);

    }
    DELAY(10);
  }

  demux_deinit();

}
