#include "app_util.h"

#include "esp_log.h"
#include "esp_mac.h"

#include <stdio.h>

static const char *TAG = "util";
static char s_id[24];

const char *app_device_id(void)
{
    if (s_id[0] != '\0') {
        return s_id;
    }

    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        ESP_LOGW(TAG, "read mac failed, fallback id");
        snprintf(s_id, sizeof(s_id), "ESP32S3-000000");
        return s_id;
    }

    snprintf(s_id, sizeof(s_id), "ESP32S3-%02X%02X%02X",
             (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
    return s_id;
}
