#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t http_server_start(httpd_handle_t *out_handle);
