#include "app_time.h"

#include "esp_log.h"
#include "esp_sntp.h"

#include <sys/time.h>
#include <time.h>

static const char *TAG = "time";

/* 2023-01-01 00:00:00 UTC，早于该值认为未校时 */
#define TIME_VALID_MIN  1672531200UL

static void on_time_sync(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "SNTP synced");
}

void app_time_start(void)
{
    if (esp_sntp_enabled()) {
        return;
    }

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_set_time_sync_notification_cb(on_time_sync);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started (ntp.aliyun.com)");
}

uint32_t app_time_now(void)
{
    const time_t now = time(NULL);
    if (now < (time_t)TIME_VALID_MIN) {
        return 0;
    }
    return (uint32_t)now;
}

bool app_time_synced(void)
{
    return app_time_now() != 0;
}
