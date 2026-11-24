/**
 * @file  modbus.h
 * @brief 极简 Modbus-RTU 主站：FC03 读保持寄存器 / FC04 读输入寄存器
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    uint32_t ok;        /* 成功次数 */
    uint32_t timeout;   /* 无响应 / 帧不完整 */
    uint32_t crc_err;   /* CRC 校验失败 */
    uint32_t frame_err; /* 从站地址、功能码、字节数不匹配 */
    uint32_t exception; /* 从站返回异常码 */
} mb_stats_t;

/** 初始化 UART + RS485 方向控制（重复调用安全） */
esp_err_t mb_init(void);

/** 设置从站地址，范围 1..247 */
esp_err_t mb_set_slave(uint8_t addr);

/** 读取当前从站地址 */
uint8_t mb_get_slave(void);

/**
 * @brief 读寄存器（单次事务，不含重试）
 * @param slave 从站地址 1..247
 * @param fc    0x03 保持寄存器 / 0x04 输入寄存器
 * @param start 起始协议地址（0 基）
 * @param count 寄存器个数 1..125
 * @param out   输出缓冲区，至少 count 个 uint16_t，按 Modbus 大端解析为宿主值
 */
esp_err_t mb_read_regs(uint8_t slave, uint8_t fc,
                       uint16_t start, uint16_t count, uint16_t *out);

/** 使用当前从站地址读寄存器，失败自动重试 MB_RETRY_MAX 次 */
esp_err_t mb_read_regs_retry(uint8_t fc, uint16_t start, uint16_t count,
                             uint16_t *out);

/** 统计信息（只读） */
const mb_stats_t *mb_get_stats(void);
void mb_reset_stats(void);
