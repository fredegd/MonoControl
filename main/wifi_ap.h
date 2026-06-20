#pragma once
#include "esp_err.h"

#define WIFI_AP_SSID      "GenArt"
#define WIFI_AP_PASSWORD  "genart00"
#define WIFI_AP_CHANNEL   6
#define WIFI_AP_MAX_CONN  4

esp_err_t wifi_ap_init(void);
