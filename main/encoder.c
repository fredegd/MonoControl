#include "encoder.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_attr.h"

static const char *TAG = "ENCODER";

// Queue handle to send events
static QueueHandle_t s_evt_queue = NULL;

// Fire only on return to idle (state 11 = both high).
// This gives exactly 1 event per complete quadrature cycle (1 per detent),
// rejecting all intermediate transitions and noise.
DRAM_ATTR static uint8_t s_prev_state = 3; // both high = idle
DRAM_ATTR static volatile int64_t s_last_rotate_time = 0;

static void IRAM_ATTR encoder_isr_handler(void *arg) {
    int cur_clk = gpio_get_level(ENCODER_PIN_CLK);
    int cur_dt = gpio_get_level(ENCODER_PIN_DT);
    uint8_t cur_state = (cur_clk << 1) | cur_dt;

    if (cur_state == s_prev_state) {
        return; // spurious interrupt, same state
    }

    uint8_t prev_state = s_prev_state;
    s_prev_state = cur_state;

    // Only fire on transition back to idle (state 11)
    if (cur_state != 3) {
        return;
    }

    int8_t delta = 0;
    if (prev_state == 2) delta = 1;      // 10 -> 11: CW
    else if (prev_state == 1) delta = -1; // 01 -> 11: CCW
    else return; // invalid transition to idle (reject noise)

    int64_t now = esp_timer_get_time();
    if ((now - s_last_rotate_time) >= 2000) {
        s_last_rotate_time = now;
        encoder_event_t evt = {
            .type = ENCODER_EVT_ROTATE,
            .delta = delta
        };
        BaseType_t higher_priority_task_woken = pdFALSE;
        xQueueSendFromISR(s_evt_queue, &evt, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/* Read the raw SW pin level (used by polling fallback) */
int encoder_read_sw(void) {
    return gpio_get_level(ENCODER_PIN_SW);
}

esp_err_t encoder_init(QueueHandle_t evt_queue) {
    if (evt_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_evt_queue = evt_queue;

    // GPIO configuration for CLK and DT
    gpio_config_t io_conf = {
        .pin_bit_mask = ((1ULL << ENCODER_PIN_CLK) | (1ULL << ENCODER_PIN_DT)),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure CLK/DT pins: %s", esp_err_to_name(err));
        return err;
    }

    // GPIO configuration for SW (no interrupt — handled by polling)
    gpio_config_t sw_conf = {
        .pin_bit_mask = (1ULL << ENCODER_PIN_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&sw_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure SW pin: %s", esp_err_to_name(err));
        return err;
    }

    // Read initial levels
    int init_clk = gpio_get_level(ENCODER_PIN_CLK);
    int init_dt = gpio_get_level(ENCODER_PIN_DT);
    s_prev_state = (init_clk << 1) | init_dt;

    // Install ISR service (can fail if already installed, check return code)
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(err));
        return err;
    }

    // Attach ISR handlers
    err = gpio_isr_handler_add(ENCODER_PIN_CLK, encoder_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add CLK ISR handler: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_isr_handler_add(ENCODER_PIN_DT, encoder_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add DT ISR handler: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Encoder initialized. CLK:%d, DT:%d, SW:%d (SW polling)",
             ENCODER_PIN_CLK, ENCODER_PIN_DT, ENCODER_PIN_SW);

    return ESP_OK;
}
