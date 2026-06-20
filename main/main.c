#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_ap.h"
#include "param_store.h"
#include "encoder.h"
#include "ws_server.h"
#include "http_server.h"
#include "ux_task.h"
#include "dns_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

void app_main(void) {
    /* 1. Initialise NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. Initialise parameter store with built-in animation profiles */
    ESP_ERROR_CHECK(param_store_init());

    /* 3. Initialise WiFi AP (also inits netif and event loop) */
    ESP_ERROR_CHECK(wifi_ap_init());

    /* 4. Start captive portal DNS server (answers all DNS with 192.168.4.1) */
    ESP_ERROR_CHECK(dns_server_start());

    /* 6. Create inter-task queues */
    QueueHandle_t encoder_queue    = xQueueCreate(16, sizeof(encoder_event_t));
    QueueHandle_t ws_notify_queue  = xQueueCreate(32, sizeof(ws_notify_msg_t));
    assert(encoder_queue && ws_notify_queue);

    /* 7. Initialise encoder GPIO and ISR */
    ESP_ERROR_CHECK(encoder_init(encoder_queue));

    /* 8. Start HTTP server, obtain handle */
    httpd_handle_t server;
    ESP_ERROR_CHECK(http_server_start(&server));

    /* 9. Start WebSocket server (registers /ws handler, starts ws_task) */
    ESP_ERROR_CHECK(ws_server_start(server, ws_notify_queue));

    /* 10. Start UX task (starts ux_task on core 1) */
    ESP_ERROR_CHECK(ux_task_start(encoder_queue, ws_notify_queue));

    ESP_LOGI("MAIN", "System ready. Connect to SSID '%s'", WIFI_AP_SSID);
}
