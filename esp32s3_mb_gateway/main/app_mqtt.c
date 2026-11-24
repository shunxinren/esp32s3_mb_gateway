#include "app_mqtt.h"
#include "config.h"
#include "app_event.h"
#include "app_util.h"

#include "esp_check.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static char s_topic_data[96];
static char s_topic_status[96];
static const char *LWT_ONLINE  = "{\"online\":true}";
static const char *LWT_OFFLINE = "{\"online\":false}";

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;
    (void)base;

    const esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to %s", APP_MQTT_URI);
        xEventGroupSetBits(app_event_group(), APP_BIT_MQTT_CONNECTED);
        esp_mqtt_client_publish(s_client, s_topic_status, LWT_ONLINE, 0, 1, 1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected, auto reconnect in background");
        xEventGroupClearBits(app_event_group(), APP_BIT_MQTT_CONNECTED);
        break;

    case MQTT_EVENT_ERROR:
        if (event && event->error_handle) {
            ESP_LOGW(TAG, "error type %d, tls %d, sock errno %d",
                     event->error_handle->error_type,
                     event->error_handle->esp_tls_last_esp_err,
                     event->error_handle->esp_transport_sock_errno);
        }
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "published msg_id=%d", event->msg_id);
        break;

    default:
        break;
    }
}

const char *app_mqtt_topic_data(void)
{
    return s_topic_data;
}

esp_err_t app_mqtt_start(void)
{
    if (s_client != NULL) {
        return ESP_OK;
    }

    snprintf(s_topic_data, sizeof(s_topic_data), "%s/%s/data",
             APP_MQTT_TOPIC_PREFIX, app_device_id());
    snprintf(s_topic_status, sizeof(s_topic_status), "%s/%s/status",
             APP_MQTT_TOPIC_PREFIX, app_device_id());

    /* 空字符串按"匿名"处理：传 NULL 而不是 "" */
    const char *user = (sizeof(APP_MQTT_USERNAME) > 1) ? APP_MQTT_USERNAME : NULL;
    const char *pass = (sizeof(APP_MQTT_PASSWORD) > 1) ? APP_MQTT_PASSWORD : NULL;

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = APP_MQTT_URI,
        .credentials.username = user,
        .credentials.authentication.password = pass,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        .session.keepalive = 30,
        .session.last_will = {
            .topic = s_topic_status,
            .msg = LWT_OFFLINE,
            .msg_len = 0,
            .qos = 1,
            .retain = 1,
        },
        .network.reconnect_timeout_ms = 5000,
        .network.disable_auto_reconnect = false,
        /* 125 个寄存器的 JSON 约 1KB，默认 1KB 缓冲区会截断 */
        .buffer.size = 2048,
        .buffer.out_size = 2048,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "client init failed");
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(
        esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL),
        TAG, "register event failed");

    ESP_LOGI(TAG, "starting, data topic: %s", s_topic_data);
    return esp_mqtt_client_start(s_client);
}

bool app_mqtt_connected(void)
{
    return (xEventGroupGetBits(app_event_group()) & APP_BIT_MQTT_CONNECTED) != 0;
}

int app_mqtt_publish(const char *topic, const char *payload)
{
    if (s_client == NULL || topic == NULL || payload == NULL) {
        return -1;
    }
    if (!app_mqtt_connected()) {
        return -1;
    }
    return esp_mqtt_client_publish(s_client, topic, payload, 0,
                                   APP_MQTT_QOS, 0 /* 数据不 retain */);
}
