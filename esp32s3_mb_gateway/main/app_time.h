/**
 * @file  app_time.h
 * @brief SNTP 校时（时间戳可信，否则离线数据的时间会乱）
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** 启动 SNTP 客户端（需先有网络） */
void app_time_start(void);

/** 当前 Unix 时间戳；未校时（< 2023 年）返回 0 */
uint32_t app_time_now(void);

bool app_time_synced(void);
