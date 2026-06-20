#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    UX_MODE_ANIM_SELECT,
    UX_MODE_NAVIGATE,
    UX_MODE_EDIT,
} ux_mode_t;

typedef struct {
    ux_mode_t mode;
    uint8_t   preselected;
    int8_t    active;
} ux_state_t;

esp_err_t ux_task_start(QueueHandle_t encoder_queue,
                        QueueHandle_t ws_notify_queue);
ux_state_t ux_task_get_state(void);
esp_err_t ux_task_post_back(void);
