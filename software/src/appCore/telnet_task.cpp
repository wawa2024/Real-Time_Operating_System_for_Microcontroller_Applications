////////////////////////////////////////////////////////////////////////
// SPDX-FileCopyrightText: Copyright © 2026, wawa2024. All rights reserved.
// SPDX-License-Identifier: GPL-2.0
/// @file telnet_task.cpp
/// @date 2026-04-21
/// @author wawa2024
/// @copyright Copyright © 2026, wawa2024. All rights reserved.
/// @brief A telnet task for FreeRTOS.
///////////////////////////// 1.Libraries //////////////////////////////

#include <esp32-oscilloscope.h>
#include <telnetCore.h>
#include <hmiCore.h>

#include "esp_log.h"

/////////////////////////////// 2.Macros ///////////////////////////////
/////////////////////////////// 3.Types ////////////////////////////////
//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////
//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////

static const char *TAG = "telnet_task";

//////////////////////////// 5.2.Functions /////////////////////////////

static void draw() {

  tft.print("Telnet: ");

  if(telnet_available()){

    tft.print("ON");

  } else {

    tft.print("OFF");

  }
}

void telnet_task(void* pvParameter){

#ifdef DEBUG
  ESP_LOGI(TAG,MSG_LAUNCHED);
#endif

  QueueHandle_t q = *(QueueHandle_t*)pvParameter;
  bool flag = true;

  // attempt to take mutex
  while(not mutex_take()) DELAY(100);
  draw();
  while(flag){
    uint32_t inputs = getinputs(q).inputs;

    reset();

    if ( inputs & BTN_ESC ) {
      flag = false;
    }

    if ( inputs & BTN_ENTER ) {
      telnet_toggle();
      draw();
    }

    DELAY(20);

  }

  mutex_release();

#ifdef DEBUG
  ESP_LOGI(TAG,MSG_DELETED);
#endif

  vTaskDelete(NULL); // self-delete

}
