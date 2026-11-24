/**
 * @file  main.c
 * @brief ESP32-S3 Modbus-RTU 网关：采集 -> 打印 -> MQTT 上报
 *
 * 阶段一（默认配置 APP_WIFI_ENABLE=n）：只做 Modbus 采集 + 串口打印，用于验证接线。
 * 阶段二：menuconfig 打开 Network & Upload，填 WiFi / MQTT 参数即可上云。
 */
#include "config.h"
#include "modbus.h"
#include "app_event.h"
#include "app_util.h"
#include "app_time.h"

#if APP_WIFI_ENABLE
#include "app_wifi.h"
#include "app_mqtt.h"
#endif

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

static const char *TAG = "app";
static uint16_t s_regs[MB_REG_COUNT];

/* ---------------- 调试打印 ---------------- */
static void dump_regs(const uint16_t *regs, int count)
{
    char line[176];
    int limit = count;
#if !APP_PRINT_ALL_REGS
    limit = (count > 16) ? 16 : count;
#endif

    for (int i = 0; i < limit; i += 8) {
        int p = snprintf(line, sizeof(line), "reg[%3d]:", i);
        for (int j = i; j < i + 8 && j < limit; j++) {
            p += snprintf(line + p, sizeof(line) - (size_t)p, " %5u", regs[j]);
        }
        ESP_LOGI(TAG, "%s", line);
    }

    if (limit < count) {
        ESP_LOGI(TAG, "  ... (%d regs read, only first %d printed)", count, limit);
    }
}

/* ---------------- 组 JSON ---------------- */
static char *build_payload(const uint16_t *regs, int count)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "deviceId", app_device_id());
    cJSON_AddNumberToObject(root, "ts", (double)app_time_now());
    cJSON_AddNumberToObject(root, "slave", (double)mb_get_slave());
    cJSON_AddNumberToObject(root, "fc", (double)MB_FC);
    cJSON_AddNumberToObject(root, "regStart", (double)MB_REG_START);
    cJSON_AddNumberToObject(root, "count", (double)count);

    cJSON *arr = cJSON_AddArrayToObject(root, "regs");
    if (arr != NULL) {
        for (int i = 0; i < count; i++) {
            cJSON_AddItemToArray(arr, cJSON_CreateNumber((double)regs[i]));
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

/* ---------------- 数据就绪：打印 + 上报 ---------------- */
static void handle_data(const uint16_t *regs, int count)
{
    dump_regs(regs, count);

#if APP_WIFI_ENABLE
    static uint32_t dropped = 0;
    if (app_mqtt_connected()) {
        char *json = build_payload(regs, count);
        if (json != NULL) {
            if (app_mqtt_publish(app_mqtt_topic_data(), json) < 0) {
                ESP_LOGW(TAG, "publish failed");
            }
            cJSON_free(json);
        }
    } else {
        dropped++;
        ESP_LOGW(TAG, "mqtt offline, data dropped (total %u)", (unsigned)dropped);
    }
#endif
}

/* ---------------- 采集任务 ---------------- */
static void poll_task(void *arg)
{
    (void)arg;
    const uint8_t fc = (uint8_t)MB_FC;

    ESP_LOGI(TAG, "poll task: slave=%u fc=%02X start=%u count=%u period=%dms",
             (unsigned)mb_get_slave(), (unsigned)fc, (unsigned)MB_REG_START,
             (unsigned)MB_REG_COUNT, (int)MB_POLL_PERIOD_MS);

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t round = 0;

    while (true) {
        const esp_err_t err = mb_read_regs_retry(fc, MB_REG_START, MB_REG_COUNT, s_regs);

        if (err == ESP_OK) {
            handle_data(s_regs, MB_REG_COUNT);
        } else {
            ESP_LOGW(TAG, "poll failed: %s", esp_err_to_name(err));
        }

        /* 每 20 轮打一次统计，便于长时间运行时观察总线质量 */
        if (++round % 20 == 0) {
            const mb_stats_t *st = mb_get_stats();
            ESP_LOGI(TAG, "stats: ok=%u timeout=%u crc=%u frame=%u exc=%u",
                     (unsigned)st->ok, (unsigned)st->timeout, (unsigned)st->crc_err,
                     (unsigned)st->frame_err, (unsigned)st->exception);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MB_POLL_PERIOD_MS));
    }
}

/* ---------------- 启动 ---------------- */
static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 Modbus-RTU gateway ===");
    ESP_LOGI(TAG, "device id : %s", app_device_id());
    ESP_LOGI(TAG, "free heap : %u bytes", (unsigned)esp_get_free_heap_size());

    nvs_init();
    app_event_init();

    ESP_ERROR_CHECK(mb_init());
    ESP_ERROR_CHECK(mb_set_slave(MB_SLAVE_DEF));

#if APP_WIFI_ENABLE
    if (app_wifi_start() == ESP_OK) {
        app_time_start();
        app_mqtt_start();
    }
#else
    ESP_LOGI(TAG, "network disabled: enable it in menuconfig -> Modbus-RTU Gateway -> Network & Upload");
#endif

    const BaseType_t ok = xTaskCreate(poll_task, "modbus_poll", 5120, NULL, 5, NULL);
    configASSERT(ok == pdPASS);
    (void)ok;
}
