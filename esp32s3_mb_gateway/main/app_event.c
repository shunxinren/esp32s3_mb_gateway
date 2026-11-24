#include "app_event.h"
#include "esp_log.h"

static const char *TAG = "event";
static EventGroupHandle_t s_group;

void app_event_init(void)
{
    if (s_group) {
        return;
    }
    s_group = xEventGroupCreate();
    if (s_group == NULL) {
        ESP_LOGE(TAG, "create event group failed");
    }
}

EventGroupHandle_t app_event_group(void)
{
    if (s_group == NULL) {
        app_event_init();
    }
    return s_group;
}
