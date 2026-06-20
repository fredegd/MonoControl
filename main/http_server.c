#include "http_server.h"
#include "esp_log.h"
#include "web_content.h"

static const char *TAG = "HTTP_SERVER";

static int s_serve_count = 0;

static esp_err_t root_get_handler(httpd_req_t *req) {
    s_serve_count++;
    ESP_LOGI(TAG, "[%d] Serving %s (uri=%s)", s_serve_count, req->uri, req->uri);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    return httpd_resp_send(req, (const char *)web_content_gz,
                           web_content_gz_len);
}

static esp_err_t redirect_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Redirecting %s to /", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t captive_redirect_handler(httpd_req_t *req) {
    return redirect_handler(req);
}

esp_err_t http_server_start(httpd_handle_t *out_handle) {
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 16;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start httpd: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t root_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = root_get_handler,
        .user_ctx  = NULL
    };
    err = httpd_register_uri_handler(server, &root_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register GET / handler: %s", esp_err_to_name(err));
        httpd_stop(server);
        return err;
    }

    const char *captive_uris[] = {
        "/generate_204",
        "/gen_204",
        "/hotspot-detect.html",
        "/library/test/success.html",
        "/ncsi.txt",
        "/connecttest.txt",
        "/fwlink",
        "/success.html",
        "/success.txt",
        "/detect.html",
        "/blank.html",
        "/captive",
    };

    for (size_t i = 0; i < sizeof(captive_uris) / sizeof(captive_uris[0]); i++) {
        httpd_uri_t captive_uri = {
            .uri = captive_uris[i],
            .method = HTTP_GET,
            .handler = captive_redirect_handler,
            .user_ctx = NULL,
        };
        err = httpd_register_uri_handler(server, &captive_uri);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register captive redirect %s: %s",
                     captive_uris[i], esp_err_to_name(err));
        }
    }

    *out_handle = server;
    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}
