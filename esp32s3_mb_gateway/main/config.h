/**
 * @file  config.h
 * @brief 全部可调参数集中在此，来源于 menuconfig (Kconfig.projbuild)
 *
 * 现场调试改参数时，优先改 menuconfig；这里只做宏映射，避免代码里散落魔法数字。
 */
#pragma once

#include "sdkconfig.h"

/* ===================== Modbus / RS485 ===================== */
#define MB_UART_PORT_NUM   CONFIG_MB_UART_PORT
#define MB_PIN_TX          CONFIG_MB_PIN_TX
#define MB_PIN_RX          CONFIG_MB_PIN_RX
#define MB_PIN_RTS         CONFIG_MB_PIN_RTS      /* <0 表示自动方向收发器 */

#define MB_BAUD            CONFIG_MB_BAUD         /* 8 数据位 / 无校验 / 1 停止位 */

#define MB_SLAVE_DEF       CONFIG_MB_SLAVE_ADDR
#define MB_SLAVE_MIN       1
#define MB_SLAVE_MAX       247

#define MB_REG_START       CONFIG_MB_REG_START    /* 协议地址（0 基） */
#define MB_REG_COUNT       CONFIG_MB_REG_COUNT    /* 1..125 */
#define MB_FC              CONFIG_MB_FUNCTION_CODE

#define MB_POLL_PERIOD_MS  (CONFIG_MB_POLL_PERIOD_S * 1000)
#define MB_TIMEOUT_MS      500                    /* 响应超时下限 */
#define MB_RETRY_MAX       2                      /* 失败重试次数 */
#define MB_RETRY_GAP_MS    50

#define MB_RX_BUF_SIZE     512
#define MB_TX_BUF_SIZE     256

/* 调试期打印全部寄存器；部署时改 0，只打印前 16 个 */
#define APP_PRINT_ALL_REGS 0

/* ===================== WiFi / MQTT ===================== */
#ifdef CONFIG_APP_WIFI_ENABLE
#define APP_WIFI_ENABLE           1
#else
#define APP_WIFI_ENABLE           0
#endif

#define APP_WIFI_SSID             CONFIG_APP_WIFI_SSID
#define APP_WIFI_PASSWORD         CONFIG_APP_WIFI_PASSWORD
#define APP_WIFI_CONNECT_TIMEOUT_MS 20000

#define APP_MQTT_URI              CONFIG_APP_MQTT_URI
#define APP_MQTT_USERNAME         CONFIG_APP_MQTT_USERNAME
#define APP_MQTT_PASSWORD         CONFIG_APP_MQTT_PASSWORD
#define APP_MQTT_TOPIC_PREFIX     CONFIG_APP_MQTT_TOPIC_PREFIX
#define APP_MQTT_QOS              1
