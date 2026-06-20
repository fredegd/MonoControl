#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define ENCODER_PIN_CLK   18
#define ENCODER_PIN_DT    19
#define ENCODER_PIN_SW    21

typedef enum {
    ENCODER_EVT_ROTATE,
    ENCODER_EVT_BUTTON_DOWN,
    ENCODER_EVT_BUTTON_UP,
} encoder_evt_type_t;

typedef struct {
    encoder_evt_type_t type;
    int8_t             delta;
} encoder_event_t;

esp_err_t encoder_init(QueueHandle_t evt_queue);

int encoder_read_sw(void);
