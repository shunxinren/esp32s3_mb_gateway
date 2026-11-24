/**
 * @file  app_util.h
 * @brief 设备标识等小工具
 */
#pragma once

/** 由 MAC 生成的设备 ID，例如 "ESP32S3-A1B2C3"（内部静态缓冲，勿 free） */
const char *app_device_id(void);
