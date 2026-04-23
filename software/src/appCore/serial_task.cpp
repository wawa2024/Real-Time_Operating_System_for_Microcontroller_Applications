////////////////////////////////////////////////////////////////////////
// SPDX-FileCopyrightText: Copyright © 2026, wawa2024. All rights reserved.
// SPDX-License-Identifier: GPL-2.0
/// @file serial_task.cpp
/// @date 2026-04-19
/// @author wawa2024
/// @brief Serial task for command line interaction with FreeRTOS
///////////////////////////// 1.Libraries //////////////////////////////

#include <esp32-oscilloscope.h>
#include <shellCore.h>

#include "esp_log.h"

/////////////////////////////// 2.Macros ///////////////////////////////

#define REFRESH_RATE_MS 100
#define WORD_SIZE 16

/////////////////////////////// 3.Types ////////////////////////////////
//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////
//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////

static const char* TAG = "serial_task";

//////////////////////////// 5.2.Functions /////////////////////////////

static String getline(){

#ifdef DEBUG
  ESP_LOGI(TAG,"call getline");
#endif

  bool flag = true;
  String s = "";
  while(flag){
    if(Serial.available()){
      char c = Serial.read();
      switch(c){
      case '\r':
        Serial.write(c);
        flag = false;
        break;
      case '\n':
        Serial.write(c);
        break;
      case '\b':
        if (!s.isEmpty()) s.remove(s.length() - 1);
        Serial.write(c);
        Serial.write(' ');
        Serial.write(c);
        break;
      default:
        s += c;
        Serial.write(c);
        break;
      }
    }
    DELAY(20);
  }

#ifdef DEBUG
  ESP_LOGI(TAG,"return getline");
#endif

  return s;
}

static void serial_init(){
#ifdef DEBUG
  ESP_LOGI(TAG,MSG_LAUNCHED);
#endif
}

static void serial_deinit(){
#ifdef DEBUG
  ESP_LOGI(TAG,MSG_DELETED);
#endif
  vTaskDelete(NULL); // self-delete
}

void serial_task(void* pvParameter) {

  serial_init();

  if( not mutex_take(serial_mutex) ) {
    serial_deinit();
    return;
  }

  while (true) {
    String line = getline();
    Serial.print(shell(line.c_str()).c_str());
    DELAY(REFRESH_RATE_MS);
  }

  serial_deinit();

}
