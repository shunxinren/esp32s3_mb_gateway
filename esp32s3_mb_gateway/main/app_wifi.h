/**
 * @file  app_wifi.h
 * @brief WiFi STA 连接管理（断线自动重连）
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief 启动 WiFi 并等待获取 IP
 * @return ESP_OK 已连接；ESP_ERR_TIMEOUT 超时（后台仍会继续重连）
 */
esp_err_t app_wifi_start(void);

bool app_wifi_connected(void);
