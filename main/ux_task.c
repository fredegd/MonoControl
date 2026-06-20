#include "ux_task.h"
#include "param_store.h"
#include "ws_server.h"
#include "encoder.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "UX";

static ux_state_t s_state = {
    .mode        = UX_MODE_ANIM_SELECT,
    .preselected = 0,
    .active      = -1,
};
static SemaphoreHandle_t s_state_mutex = NULL;

typedef enum {
    UX_EVT_ROTATE,
    UX_EVT_SINGLE_CLICK,
    UX_EVT_DOUBLE_CLICK,
    UX_EVT_BACK,
} ux_event_type_t;

typedef struct {
    ux_event_type_t type;
    int8_t          delta;
} ux_event_t;

static QueueHandle_t s_ux_queue = NULL;
static QueueHandle_t s_ws_notify_queue = NULL;

static volatile bool s_timer_running = false;
static esp_timer_handle_t s_click_timer = NULL;

static void post_state_changed(void) {
    ws_notify_msg_t msg;
    msg.type = WS_NOTIFY_STATE_CHANGED;
    msg.state = ux_task_get_state();
    if (s_ws_notify_queue) {
        xQueueSend(s_ws_notify_queue, &msg, pdMS_TO_TICKS(10));
    }
}

static void post_param_changed(uint8_t index, float value) {
    ws_notify_msg_t msg;
    msg.type = WS_NOTIFY_PARAM_CHANGED;
    msg.param.index = index;
    msg.param.value = value;
    if (s_ws_notify_queue) {
        xQueueSend(s_ws_notify_queue, &msg, pdMS_TO_TICKS(10));
    }
}

static void post_anim_changed(uint8_t anim_index) {
    ws_notify_msg_t msg;
    msg.type = WS_NOTIFY_ANIM_CHANGED;
    msg.anim_index = anim_index;
    if (s_ws_notify_queue) {
        xQueueSend(s_ws_notify_queue, &msg, pdMS_TO_TICKS(10));
    }
}

static void click_timer_callback(void *arg) {
    s_timer_running = false;
    ux_event_t evt = {
        .type = UX_EVT_SINGLE_CLICK,
        .delta = 0
    };
    if (s_ux_queue) {
        xQueueSend(s_ux_queue, &evt, 0);
    }
}

static void encoder_reader_task(void *pvParameters) {
    QueueHandle_t encoder_queue = (QueueHandle_t)pvParameters;
    encoder_event_t enc_evt;

    int last_poll_sw = encoder_read_sw();
    int sw_stable_count = 0;
    int64_t last_rotate_time = 0;
    bool button_pressed_flag = false;

    while (1) {
        if (xQueueReceive(encoder_queue, &enc_evt, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (enc_evt.type == ENCODER_EVT_ROTATE) {
                last_rotate_time = esp_timer_get_time();
                if (s_timer_running) {
                    esp_timer_stop(s_click_timer);
                    s_timer_running = false;
                    ESP_LOGI(TAG, "Rotation during click timer — cancelled");
                }
                ESP_LOGI(TAG, "Encoder rotate: delta=%d", enc_evt.delta);
                ux_event_t ux_evt = {
                    .type = UX_EVT_ROTATE,
                    .delta = enc_evt.delta
                };
                xQueueSend(s_ux_queue, &ux_evt, pdMS_TO_TICKS(10));
            } else if (enc_evt.type == ENCODER_EVT_BUTTON_DOWN) {
                ESP_LOGI(TAG, "Button DOWN (timer_running=%d)", s_timer_running);
                if (s_timer_running) {
                    ESP_LOGI(TAG, "Double click detected");
                    esp_timer_stop(s_click_timer);
                    s_timer_running = false;
                    ux_event_t ux_evt = {
                        .type = UX_EVT_DOUBLE_CLICK,
                        .delta = 0
                    };
                    xQueueSend(s_ux_queue, &ux_evt, pdMS_TO_TICKS(10));
                } else {
                    s_timer_running = true;
                    esp_timer_start_once(s_click_timer, 250000);
                }
            } else if (enc_evt.type == ENCODER_EVT_BUTTON_UP) {
            }
        }

        int cur_sw = encoder_read_sw();

        if (cur_sw == 1) {
            button_pressed_flag = false;
        }

        bool rotation_active = (esp_timer_get_time() - last_rotate_time) < 50000;

        if (rotation_active) {
            sw_stable_count = 0;
        }

        if (cur_sw == last_poll_sw && !rotation_active && !button_pressed_flag) {
            if (cur_sw == 0 && ++sw_stable_count >= 2) {
                sw_stable_count = 0;
                button_pressed_flag = true;
                ESP_LOGI(TAG, "Button pressed");
                if (s_timer_running) {
                    ESP_LOGI(TAG, "Double click detected");
                    esp_timer_stop(s_click_timer);
                    s_timer_running = false;
                    ux_event_t ux_evt = {
                        .type = UX_EVT_DOUBLE_CLICK,
                        .delta = 0
                    };
                    xQueueSend(s_ux_queue, &ux_evt, pdMS_TO_TICKS(10));
                } else {
                    s_timer_running = true;
                    esp_timer_start_once(s_click_timer, 250000);
                }
            }
        } else {
            last_poll_sw = cur_sw;
            sw_stable_count = 0;
        }
    }
}

static void ux_processing_task(void *pvParameters) {
    ux_event_t evt;
    ESP_LOGI(TAG, "UX Processing Task started");

    while (1) {
        if (xQueueReceive(s_ux_queue, &evt, portMAX_DELAY) == pdTRUE) {
            uint8_t param_count = param_store_count();
            if (param_count == 0) continue;

            if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {

                /* ===================== ANIM_SELECT MODE ===================== */
                if (s_state.mode == UX_MODE_ANIM_SELECT) {

                    if (evt.type == UX_EVT_ROTATE) {
                        uint8_t anim_count = param_store_get_anim_count();
                        int pre = (int)s_state.preselected + evt.delta;
                        if (pre < 0) pre = 0;
                        if (pre >= anim_count) pre = anim_count - 1;
                        s_state.preselected = (uint8_t)pre;
                        ESP_LOGI(TAG, "Anim select rotate: anim=%d", pre);
                        xSemaphoreGive(s_state_mutex);
                        post_state_changed();

                    } else if (evt.type == UX_EVT_BACK) {
                        ESP_LOGI(TAG, "Anim select back (already here)");
                        xSemaphoreGive(s_state_mutex);

                    } else if (evt.type == UX_EVT_SINGLE_CLICK) {
                        uint8_t chosen_anim = s_state.preselected;
                        ESP_LOGI(TAG, "Anim select single_click -> selecting anim %d", chosen_anim);
                        xSemaphoreGive(s_state_mutex);
                        param_store_switch_anim(chosen_anim);
                        if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                            s_state.mode = UX_MODE_NAVIGATE;
                            s_state.preselected = 0;
                            s_state.active = -1;
                            xSemaphoreGive(s_state_mutex);
                        }
                        post_anim_changed(chosen_anim);
                        post_state_changed();

                    } else if (evt.type == UX_EVT_DOUBLE_CLICK) {
                        ESP_LOGI(TAG, "Anim select double_click (no-op)");
                        xSemaphoreGive(s_state_mutex);
                    } else {
                        xSemaphoreGive(s_state_mutex);
                    }

                /* ===================== NAVIGATE MODE ===================== */
                } else if (s_state.mode == UX_MODE_NAVIGATE) {

                    if (evt.type == UX_EVT_ROTATE) {
                        int pre = s_state.preselected + evt.delta;
                        if (pre < 0) pre = 0;
                        if (pre >= param_count) pre = param_count - 1;
                        s_state.preselected = pre;
                        ESP_LOGI(TAG, "Navigate rotate: preselected=%d", pre);
                        xSemaphoreGive(s_state_mutex);
                        post_state_changed();

                    } else if (evt.type == UX_EVT_SINGLE_CLICK) {
                        if (s_state.preselected == 0) {
                            uint8_t current_anim = param_store_get_current_anim();
                            ESP_LOGI(TAG, "Navigate single_click on back -> ANIM_SELECT");
                            s_state.mode = UX_MODE_ANIM_SELECT;
                            s_state.preselected = current_anim;
                            s_state.active = -1;
                            xSemaphoreGive(s_state_mutex);
                            post_state_changed();
                        } else {
                            s_state.mode = UX_MODE_EDIT;
                            s_state.active = s_state.preselected;
                            ESP_LOGI(TAG, "Navigate single_click -> EDIT mode: active=%d", s_state.active);
                            xSemaphoreGive(s_state_mutex);
                            post_state_changed();
                        }

                    } else if (evt.type == UX_EVT_DOUBLE_CLICK || evt.type == UX_EVT_BACK) {
                        uint8_t current_anim = param_store_get_current_anim();
                        ESP_LOGI(TAG, "Navigate %s -> ANIM_SELECT mode",
                            evt.type == UX_EVT_DOUBLE_CLICK ? "double_click" : "back");
                        s_state.mode = UX_MODE_ANIM_SELECT;
                        s_state.preselected = current_anim;
                        s_state.active = -1;
                        xSemaphoreGive(s_state_mutex);
                        post_state_changed();

                    } else {
                        xSemaphoreGive(s_state_mutex);
                    }

                /* ===================== EDIT MODE ===================== */
                } else if (s_state.mode == UX_MODE_EDIT) {

                    if (evt.type == UX_EVT_ROTATE) {
                        int8_t active_idx = s_state.active;
                        xSemaphoreGive(s_state_mutex);

                        param_t snapshot[PARAM_MAX_COUNT];
                        uint8_t count;
                        if (param_store_get_snapshot(snapshot, &count) == ESP_OK) {
                            if (active_idx >= 0 && active_idx < count) {
                                float val = snapshot[active_idx].value;
                                float step = snapshot[active_idx].step;
                                float new_val = val + (evt.delta * step);
                                param_store_set_value(active_idx, new_val);
                                float updated_val;
                                param_store_get_value(active_idx, &updated_val);
                                ESP_LOGI(TAG, "Edit rotate: param[%d] %s = %.2f", active_idx, snapshot[active_idx].name, updated_val);
                                post_param_changed(active_idx, updated_val);
                            }
                        }

                    } else if (evt.type == UX_EVT_SINGLE_CLICK || evt.type == UX_EVT_DOUBLE_CLICK) {
                        s_state.mode = UX_MODE_NAVIGATE;
                        s_state.active = -1;
                        ESP_LOGI(TAG, "Edit %s -> NAVIGATE mode",
                            evt.type == UX_EVT_SINGLE_CLICK ? "single_click" : "double_click");
                        xSemaphoreGive(s_state_mutex);
                        post_state_changed();

                    } else if (evt.type == UX_EVT_BACK) {
                        uint8_t current_anim = param_store_get_current_anim();
                        ESP_LOGI(TAG, "Edit back -> ANIM_SELECT mode");
                        s_state.mode = UX_MODE_ANIM_SELECT;
                        s_state.preselected = current_anim;
                        s_state.active = -1;
                        xSemaphoreGive(s_state_mutex);
                        post_state_changed();

                    } else {
                        xSemaphoreGive(s_state_mutex);
                    }
                }
            }
        }
    }
}

esp_err_t ux_task_start(QueueHandle_t encoder_queue,
                        QueueHandle_t ws_notify_queue) {
    if (encoder_queue == NULL || ws_notify_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_ws_notify_queue = ws_notify_queue;

    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
        if (s_state_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_ux_queue == NULL) {
        s_ux_queue = xQueueCreate(32, sizeof(ux_event_t));
        if (s_ux_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_click_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = &click_timer_callback,
            .name = "click_timer"
        };
        esp_err_t err = esp_timer_create(&timer_args, &s_click_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create click timer: %s", esp_err_to_name(err));
            return err;
        }
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        encoder_reader_task,
        "enc_reader_task",
        4096,
        (void*)encoder_queue,
        configMAX_PRIORITIES - 2,
        NULL,
        1
    );
    if (ret != pdPASS) {
        return ESP_FAIL;
    }

    ret = xTaskCreatePinnedToCore(
        ux_processing_task,
        "ux_proc_task",
        4096,
        NULL,
        configMAX_PRIORITIES - 2,
        NULL,
        1
    );
    if (ret != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "UX tasks started successfully on Core 1");
    return ESP_OK;
}

esp_err_t ux_task_post_back(void) {
    if (s_ux_queue == NULL) return ESP_ERR_INVALID_STATE;
    ux_event_t evt = { .type = UX_EVT_BACK, .delta = 0 };
    if (xQueueSend(s_ux_queue, &evt, pdMS_TO_TICKS(10)) == pdTRUE) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

ux_state_t ux_task_get_state(void) {
    ux_state_t state = {0};
    if (s_state_mutex && xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state = s_state;
        xSemaphoreGive(s_state_mutex);
    }
    return state;
}
