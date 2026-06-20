#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_http_server.h"
#include "ux_task.h"

typedef enum {
    WS_NOTIFY_STATE_CHANGED,
    WS_NOTIFY_PARAM_CHANGED,
    WS_NOTIFY_ANIM_CHANGED,
} ws_notify_type_t;

typedef struct {
    ws_notify_type_t type;
    union {
        ux_state_t state;
        struct {
            uint8_t index;
            float   value;
        } param;
        uint8_t anim_index;
    };
} ws_notify_msg_t;

esp_err_t ws_server_start(httpd_handle_t server,
                          QueueHandle_t  notify_queue);
