////////////////////////////////////////////////////////////////////////
// SPDX-FileCopyrightText: Copyright © 2026, wawa2024. All rights reserved.
// SPDX-License-Identifier: GPL-2.0
/// @file telnetCore.cpp
/// @date 2026-04-21
/// @author wawa2024
/// @copyright Copyright © 2026, wawa2024. All rights reserved.
/// @brief A telnet FreeRTOS task for ESP32.
///////////////////////////// 1.Libraries //////////////////////////////

#include <Arduino.h>
#include <shellCore.h>

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "AsyncTCP.h"

/////////////////////////////// 2.Macros ///////////////////////////////
/////////////////////////////// 3.Types ////////////////////////////////
//////////////////////////// 4.Declarations ////////////////////////////
//////////////////////////// 4.1.Variables /////////////////////////////
//////////////////////////// 4.2.Functions /////////////////////////////
//////////////////////////// 5.Definitions /////////////////////////////
//////////////////////////// 5.1.Variables /////////////////////////////

static const char *TAG = "mini_ap";

//////////////////////////// 5.2.Functions /////////////////////////////

class TelnetHandler {
public:
  TelnetHandler() {
    server = new AsyncServer{23};
    server->onClient(
                     [this](void* s, AsyncClient* client){
                       ESP_LOGI(TAG, "Telnet client connected");
                       client->onData(
                                      [](void* arg, AsyncClient* c, void* data, size_t len){

                                        size_t i = 0;
                                        char buf[256];

                                        for(i = 0 ; i < sizeof(buf)/sizeof(char) ; i++){
                                          buf[i] = '\0';
                                        }

                                        const char* stream = (const char*)data;

                                        for(i = 0 ; i < len ; i++){
                                          char c = stream[i];
                                          switch(c){
                                          case '\n': case '\r':
                                            break;
                                          default:
                                            buf[i] = c;
                                            break;
                                          }
                                        }

                                        String s = shell(buf);

                                        c->write(s.c_str(),s.length());
                                      }
                                      , nullptr
                                      );
                       client->onDisconnect(
                                            [](void* arg, AsyncClient* c){
                                              ESP_LOGI(TAG, "Telnet client disconnected");
                                            }
                                            , nullptr
                                            );
                     }
                     , nullptr
                     );
    server->begin();
  }
  ~TelnetHandler() {
    server->end();
    delete server;
  }
private:
  AsyncServer* server = nullptr;

};

static TelnetHandler* telnet = nullptr;

void telnet_init() {
  telnet = telnet == nullptr ? new TelnetHandler() : telnet;
}

void telnet_deinit() {
  delete telnet;
  telnet = nullptr;
}

void wifi_init() {

#ifdef DEBUG
  ESP_LOGI(TAG,"wifi_init");
#endif

  const char* AP_SSID = TAG;
  const char* AP_PASS = "itsasecret";
  const uint8_t AP_CHANNEL = 1;
  const uint8_t AP_MAX_CONN = 4;

  // NVS (required by Wi‑Fi)
  if (nvs_flash_init() == ESP_ERR_NVS_NO_FREE_PAGES ||
      nvs_flash_init() == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
      nvs_flash_erase();
      nvs_flash_init();
    }

  // Initialize
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
  if (ap_netif == NULL) {
#ifdef DEBUG
    ESP_LOGE(TAG, "failed AP netif");
#endif
    return;
  }
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Configure Access Point
  wifi_config_t ap_config = { 0 };
  strncpy((char *)ap_config.ap.ssid, AP_SSID, sizeof(ap_config.ap.ssid)-1);
  strncpy((char *)ap_config.ap.password, AP_PASS, sizeof(ap_config.ap.password)-1);
  ap_config.ap.channel = AP_CHANNEL;
  ap_config.ap.max_connection = AP_MAX_CONN;
  ap_config.ap.ssid_hidden = 0;
  ap_config.ap.ssid_len = 0;
  ap_config.ap.authmode = (strlen(AP_PASS) >= 8) ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;

  // Launch Access Point
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  // Log info
  ESP_LOGI(TAG,"SSID=%s PASS=%s",AP_SSID,AP_PASS);
  esp_netif_ip_info_t ip_info;
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"); // or "WIFI_AP_DEF"
  if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
    ESP_LOGI(TAG, "IP=" IPSTR, IP2STR(&ip_info.ip));
  }

}

void wifi_deinit() {
#ifdef DEBUG
  ESP_LOGI(TAG,"wifi_deinit");
#endif
  ESP_ERROR_CHECK(esp_wifi_stop());
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
  ESP_ERROR_CHECK(esp_wifi_deinit());
  ESP_ERROR_CHECK(esp_event_loop_delete_default());
  ESP_ERROR_CHECK(esp_netif_deinit());
}

bool telnet_available() {
  return telnet == nullptr ? false : true;
}

bool telnet_toggle() {
  if( telnet_available() ){
    telnet_deinit();
    wifi_deinit();
    return false;
  } else {
    wifi_init();
    telnet_init();
    return true;
  }
}
