/**
 * @file  app_event.h
 * @brief 全局事件组：WiFi / MQTT 连接状态共享
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define APP_BIT_WIFI_CONNECTED  BIT0
#define APP_BIT_MQTT_CONNECTED  BIT1

void app_event_init(void);
EventGroupHandle_t app_event_group(void);
