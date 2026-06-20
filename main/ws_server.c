#include "ws_server.h"
#include "param_store.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdbool.h>

#ifndef CONFIG_HTTPD_MAX_OPEN_SOCKETS
#define CONFIG_HTTPD_MAX_OPEN_SOCKETS 7
#endif

static const char *TAG = "WS_SERVER";

static int s_clients[CONFIG_HTTPD_MAX_OPEN_SOCKETS];
static bool s_pending_snapshot[CONFIG_HTTPD_MAX_OPEN_SOCKETS];
static bool s_pending_anim_reload[CONFIG_HTTPD_MAX_OPEN_SOCKETS];
static SemaphoreHandle_t s_clients_mutex = NULL;
static QueueHandle_t s_notify_queue = NULL;
static httpd_handle_t s_server_handle = NULL;

static void add_client(int fd) {
    if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < CONFIG_HTTPD_MAX_OPEN_SOCKETS; i++) {
            if (s_clients[i] == -1) {
                s_clients[i] = fd;
                s_pending_snapshot[i] = true;
                s_pending_anim_reload[i] = false;
                ESP_LOGI(TAG, "Added client fd %d at slot %d", fd, i);
                break;
            }
        }
        xSemaphoreGive(s_clients_mutex);
    }
}

static void remove_client(int fd) {
    if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < CONFIG_HTTPD_MAX_OPEN_SOCKETS; i++) {
            if (s_clients[i] == fd) {
                s_clients[i] = -1;
                s_pending_snapshot[i] = false;
                s_pending_anim_reload[i] = false;
                ESP_LOGI(TAG, "Removed client fd %d from slot %d", fd, i);
                break;
            }
        }
        xSemaphoreGive(s_clients_mutex);
    }
}

static void send_snapshot(httpd_handle_t hd, int fd) {
    param_t snapshot[PARAM_MAX_COUNT];
    uint8_t count = 0;
    if (param_store_get_snapshot(snapshot, &count) != ESP_OK) {
        return;
    }

    ux_state_t state = ux_task_get_state();
    const char *mode_str;
    switch (state.mode) {
        case UX_MODE_ANIM_SELECT: mode_str = "ANIM_SELECT"; break;
        case UX_MODE_EDIT:        mode_str = "EDIT";        break;
        default:                  mode_str = "NAVIGATE";    break;
    }

    uint8_t anim_index = param_store_get_current_anim();
    uint8_t anim_count = param_store_get_anim_count();
    const anim_profile_t *profiles = param_store_get_anim_profiles();

    char buf[2048];
    int len = snprintf(buf, sizeof(buf),
        "{\"type\":\"snapshot\",\"mode\":\"%s\","
        "\"anim_index\":%u,\"anim_count\":%u,\"anim_names\":[",
        mode_str, anim_index, anim_count);

    for (uint8_t a = 0; a < anim_count; a++) {
        int remaining = sizeof(buf) - len - 5;
        if (remaining <= 0) break;
        int a_len = snprintf(buf + len, remaining,
            "\"%s\"%s", profiles[a].name, (a == anim_count - 1) ? "" : ",");
        len += a_len;
    }

    len += snprintf(buf + len, sizeof(buf) - len,
        "],\"preselected\":%d,\"active\":%d,\"params\":[",
        state.preselected, state.active);

    for (uint8_t i = 0; i < count; i++) {
        int remaining = sizeof(buf) - len - 5;
        if (remaining <= 0) break;
        int p_len = snprintf(buf + len, remaining,
            "{\"name\":\"%s\",\"min\":%.2f,\"max\":%.2f,\"step\":%.2f,\"value\":%.2f,\"decimals\":%d}%s",
            snapshot[i].name, snapshot[i].min, snapshot[i].max, snapshot[i].step,
            snapshot[i].value, snapshot[i].decimals, (i == count - 1) ? "" : ",");
        len += p_len;
    }

    if (len < sizeof(buf) - 3) {
        buf[len++] = ']';
        buf[len++] = '}';
        buf[len] = '\0';
    }

    httpd_ws_frame_t ws_pkt = {
        .payload = (uint8_t *)buf,
        .len = len,
        .type = HTTPD_WS_TYPE_TEXT,
    };

    esp_err_t err = httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send snapshot to fd %d: %s", fd, esp_err_to_name(err));
        remove_client(fd);
    } else {
        ESP_LOGI(TAG, "Sent snapshot to fd %d (%d bytes, %d params)", fd, len, count);
    }
}

static esp_err_t ws_post_handshake_cb(httpd_req_t *req) {
    int fd = httpd_req_to_sockfd(req);
    add_client(fd);
    ESP_LOGI(TAG, "WS client connected fd=%d", fd);
    return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req) {
    int fd = httpd_req_to_sockfd(req);
    ESP_LOGI(TAG, "ws_handler called fd=%d uri=%s", fd, req->uri);

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    if (ws_pkt.len > 0 && ws_pkt.len < 128) {
        uint8_t tmp[128];
        ws_pkt.payload = tmp;
        size_t to_read = ws_pkt.len;
        while (to_read > 0) {
            ws_pkt.len = (to_read > sizeof(tmp)) ? sizeof(tmp) : to_read;
            ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            if (ret != ESP_OK) {
                return ret;
            }
            to_read -= ws_pkt.len;
        }

        // Simple JSON action parser — check for "action":"back"
        tmp[ws_pkt.len] = '\0';
        if (strstr((const char *)tmp, "\"back\"") != NULL) {
            ESP_LOGI(TAG, "WS back action received");
            ux_task_post_back();
        }
    }

    return ESP_OK;
}

static void ws_broadcast(const char *json_str, size_t len) {
    if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < CONFIG_HTTPD_MAX_OPEN_SOCKETS; i++) {
            int fd = s_clients[i];
            if (fd != -1) {
                httpd_ws_frame_t ws_pkt = {
                    .payload = (uint8_t *)json_str,
                    .len = len,
                    .type = HTTPD_WS_TYPE_TEXT,
                };
                esp_err_t err = httpd_ws_send_frame_async(s_server_handle, fd, &ws_pkt);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to send to fd %d, removing client", fd);
                    s_clients[i] = -1;
                    s_pending_snapshot[i] = false;
                    s_pending_anim_reload[i] = false;
                }
            }
        }
        xSemaphoreGive(s_clients_mutex);
    }
}

static void ws_task(void *pvParameters) {
    ws_notify_msg_t msg;
    char buf[256];

    while (1) {
        if (xQueueReceive(s_notify_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            int len = 0;

            if (msg.type == WS_NOTIFY_STATE_CHANGED) {
                const char *mode_str;
                switch (msg.state.mode) {
                    case UX_MODE_ANIM_SELECT: mode_str = "ANIM_SELECT"; break;
                    case UX_MODE_EDIT:        mode_str = "EDIT";        break;
                    default:                  mode_str = "NAVIGATE";    break;
                }
                len = snprintf(buf, sizeof(buf),
                    "{\"type\":\"state_changed\",\"mode\":\"%s\","
                    "\"preselected\":%d,\"active\":%d}",
                    mode_str, msg.state.preselected, msg.state.active);

            } else if (msg.type == WS_NOTIFY_PARAM_CHANGED) {
                len = snprintf(buf, sizeof(buf),
                    "{\"type\":\"param_changed\",\"index\":%d,\"value\":%.4f}",
                    msg.param.index, msg.param.value);

            } else if (msg.type == WS_NOTIFY_ANIM_CHANGED) {
                uint8_t anim_idx = msg.anim_index;
                const anim_profile_t *prof = param_store_get_anim_profile(anim_idx);
                const char *anim_name = prof ? prof->name : "Unknown";
                len = snprintf(buf, sizeof(buf),
                    "{\"type\":\"animation_changed\",\"anim_index\":%u,\"anim_name\":\"%s\"}",
                    anim_idx, anim_name);

                // Send snapshot to all clients immediately
                if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    for (int i = 0; i < CONFIG_HTTPD_MAX_OPEN_SOCKETS; i++) {
                        if (s_clients[i] != -1) {
                            int fd = s_clients[i];
                            s_pending_snapshot[i] = false;
                            xSemaphoreGive(s_clients_mutex);
                            send_snapshot(s_server_handle, fd);
                            if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(500)) != pdTRUE) break;
                        }
                    }
                    xSemaphoreGive(s_clients_mutex);
                }
            }

            if (len > 0 && len < sizeof(buf)) {
                ws_broadcast(buf, len);
            }
        }

        // Check for clients needing snapshot (periodic poll)
        if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (int i = 0; i < CONFIG_HTTPD_MAX_OPEN_SOCKETS; i++) {
                if (s_clients[i] != -1 && s_pending_snapshot[i]) {
                    int fd = s_clients[i];
                    s_pending_snapshot[i] = false;
                    xSemaphoreGive(s_clients_mutex);
                    send_snapshot(s_server_handle, fd);
                    if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(500)) != pdTRUE) break;
                }
            }
            xSemaphoreGive(s_clients_mutex);
        }
    }
}

esp_err_t ws_server_start(httpd_handle_t server,
                          QueueHandle_t  notify_queue) {
    if (server == NULL || notify_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_server_handle = server;
    s_notify_queue = notify_queue;

    for (int i = 0; i < CONFIG_HTTPD_MAX_OPEN_SOCKETS; i++) {
        s_clients[i] = -1;
        s_pending_snapshot[i] = false;
        s_pending_anim_reload[i] = false;
    }

    if (s_clients_mutex == NULL) {
        s_clients_mutex = xSemaphoreCreateMutex();
        if (s_clients_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    httpd_uri_t ws_uri = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
        .ws_post_handshake_cb = ws_post_handshake_cb
    };
    esp_err_t err = httpd_register_uri_handler(server, &ws_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WS URI handler: %s", esp_err_to_name(err));
        return err;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        ws_task,
        "ws_task",
        6144,
        NULL,
        configMAX_PRIORITIES - 3,
        NULL,
        0
    );
    if (ret != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WebSocket server started successfully");
    return ESP_OK;
}
