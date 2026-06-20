#include "dns_server.h"
#include "esp_log.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "DNS_SERVER";
static const char *DNS_IP = "192.168.4.1";

static void dns_task(void *pvParameters) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(53),
    };

    int err = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to bind port 53: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Captive portal DNS listening on port 53");

    uint8_t buf[512];
    uint8_t resp[512];
    struct sockaddr_in from;
    socklen_t fromlen;

    while (1) {
        fromlen = sizeof(from);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
        if (len < 12) continue;

        if ((buf[2] & 0x78) != 0) continue;

        memcpy(resp, buf, 12);
        resp[2] = 0x80 | 0x08 | (buf[2] & 0x02);
        resp[3] = 0x80;
        resp[6] = 0x00;
        resp[7] = 0x01;
        resp[8] = 0x00;
        resp[9] = 0x00;
        resp[10] = 0x00;
        resp[11] = 0x00;

        int qlen = 12;
        while (qlen < len && buf[qlen] != 0) {
            qlen += buf[qlen] + 1;
        }
        qlen += 5;

        memcpy(resp + 12, buf + 12, qlen - 12);
        int rlen = qlen;

        rlen += 2;
        resp[rlen - 2] = 0xC0;
        resp[rlen - 1] = 0x0C;

        resp[rlen++] = 0x00;
        resp[rlen++] = 0x01;
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x01;
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x3C;
        resp[rlen++] = 0x00;
        resp[rlen++] = 0x04;

        uint32_t ip = inet_addr(DNS_IP);
        memcpy(resp + rlen, &ip, 4);
        rlen += 4;

        sendto(sock, resp, rlen, 0, (struct sockaddr *)&from, fromlen);
    }

    close(sock);
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(void) {
    BaseType_t ret = xTaskCreatePinnedToCore(
        dns_task,
        "dns_server",
        3072,
        NULL,
        configMAX_PRIORITIES - 3,
        NULL,
        0
    );
    if (ret != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
