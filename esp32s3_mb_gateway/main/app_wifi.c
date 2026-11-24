#include "app_wifi.h"
#include "config.h"
#include "app_event.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include <string.h>

static const char *TAG = "wifi";

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "sta start, connecting to \"%s\"", APP_WIFI_SSID);
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            const wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
            xEventGroupClearBits(app_event_group(), APP_BIT_WIFI_CONNECTED);
            ESP_LOGW(TAG, "disconnected (reason %d), retrying...", d->reason);
            esp_wifi_connect();   /* 无限重连，网关不能因为断网停摆 */
            break;
        }

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "associated, waiting for IP...");
            break;

        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(app_event_group(), APP_BIT_WIFI_CONNECTED);
    }
}

esp_err_t app_wifi_start(void)
{
    static bool s_started = false;

    if (!s_started) {
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(err));
            return err;
        }

        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "create event loop: %s", esp_err_to_name(err));
            return err;
        }

        if (esp_netif_create_default_wifi_sta() == NULL) {
            ESP_LOGE(TAG, "create default wifi sta failed");
            return ESP_FAIL;
        }

        wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "esp_wifi_init failed");

        ESP_RETURN_ON_ERROR(
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                wifi_event_handler, NULL, NULL),
            TAG, "register wifi event failed");
        ESP_RETURN_ON_ERROR(
            esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                wifi_event_handler, NULL, NULL),
            TAG, "register ip event failed");

        wifi_config_t wifi_cfg = {0};
        strlcpy((char *)wifi_cfg.sta.ssid, APP_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
        strlcpy((char *)wifi_cfg.sta.password, APP_WIFI_PASSWORD, sizeof(wifi_cfg.sta.password));
        wifi_cfg.sta.threshold.authmode =
            (strlen(APP_WIFI_PASSWORD) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        wifi_cfg.sta.pmf_cfg.capable = true;

        /* 不把密码写进 NVS，减少 flash 写入 */
        ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set storage failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "set config failed");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

        s_started = true;
    }

    EventBits_t bits = xEventGroupWaitBits(app_event_group(), APP_BIT_WIFI_CONNECTED,
                                           pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(APP_WIFI_CONNECT_TIMEOUT_MS));
    if (bits & APP_BIT_WIFI_CONNECTED) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "wait IP timeout, will keep reconnecting in background");
    return ESP_ERR_TIMEOUT;
}

bool app_wifi_connected(void)
{
    return (xEventGroupGetBits(app_event_group()) & APP_BIT_WIFI_CONNECTED) != 0;
}
