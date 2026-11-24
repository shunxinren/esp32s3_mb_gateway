/**
 * @file  app_mqtt.h
 * @brief MQTT 客户端封装（esp-mqtt，自动重连 + LWT 在线状态）
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/** 启动客户端（内部自动重连） */
esp_err_t app_mqtt_start(void);

bool app_mqtt_connected(void);

/** 数据上报 topic：<prefix>/<deviceId>/data */
const char *app_mqtt_topic_data(void);

/**
 * @brief 发布消息
 * @return 成功返回 msg_id，失败返回 -1
 */
int app_mqtt_publish(const char *topic, const char *payload);
