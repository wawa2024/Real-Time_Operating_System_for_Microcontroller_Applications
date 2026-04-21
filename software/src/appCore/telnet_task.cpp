////////////////////////////////////////////////////////////////////////
// SPDX-FileCopyrightText: Copyright © 2026, wawa2024. All rights reserved.
// SPDX-License-Identifier: GPL-2.0
/// @file telnet_task.cpp
/// @date 2026-04-21
/// @author wawa2024
/// @copyright Copyright © 2026, wawa2024. All rights reserved.
/// @brief A telnet FreeRTOS task for ESP32.
///////////////////////////// 1.Libraries //////////////////////////////

#include "AsyncTCP.h"
#include "esp_log.h"

/////////////////////////////// 2.Macros ///////////////////////////////
/////////////////////////////// 3.Types ////////////////////////////////
//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////
//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////
//////////////////////////// 5.2.Functions /////////////////////////////

static const char *TAG = "telnet";

class TelnetHandler {
public:
  AsyncServer server{23};

  TelnetHandler() {
    server.onClient(
                    [this](void* s, AsyncClient* client){
                      ESP_LOGI(TAG, "Client connected");
                      client->onData(
                                     [](void* arg, AsyncClient* c, void* data, size_t len){
                                       c->write((const char*)data, len); // echo back
                                     }
                                     , nullptr
                                     );
                      client->onDisconnect(
                                           [](void* arg, AsyncClient* c){
                                             ESP_LOGI(TAG, "Client disconnected");
                                           }
                                           , nullptr
                                           );
                    }
                    , nullptr
                    );
    server.begin();
  }
};

static TelnetHandler telnet;
