/**
 * @file  modbus.c
 * @brief Modbus-RTU 主站实现（手写帧，仅依赖 UART 驱动，便于逐字节排查问题）
 *
 * 设计要点：
 *  1. 用 UART 硬件 RTS 控制 RS485 方向（UART_MODE_RS485_HALF_DUPLEX），
 *     方向切换时序由外设负责，避免软件 GPIO 翻转导致末字节被截断。
 *  2. 发送后必须 uart_wait_tx_done()，等最后一个字节从移位寄存器发完。
 *  3. 读响应按"预期长度循环读"，不用单次 uart_read_bytes，避免半帧。
 *  4. 响应超时按帧长度和波特率动态计算，慢速/多寄存器场景不会误判超时。
 */
#include "modbus.h"
#include "config.h"

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "modbus";
static const uart_port_t s_port = (uart_port_t)MB_UART_PORT_NUM;

static uint8_t s_slave = MB_SLAVE_DEF;
static bool    s_ready = false;
static mb_stats_t s_stats;

/* ---------------- CRC16 (Modbus, poly 0xA001, 初值 0xFFFF) ---------------- */
static uint16_t crc16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x0001) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc; /* 发送时低字节在前 */
}

/* ---------------- 初始化 ---------------- */
esp_err_t mb_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    const uart_config_t cfg = {
        .baud_rate  = MB_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(s_port, MB_RX_BUF_SIZE, MB_TX_BUF_SIZE,
                                            0, NULL, 0),
                        TAG, "uart_driver_install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(s_port, &cfg), TAG, "uart_param_config failed");

    const int rts_pin = (MB_PIN_RTS >= 0) ? MB_PIN_RTS : UART_PIN_NO_CHANGE;
    ESP_RETURN_ON_ERROR(uart_set_pin(s_port, MB_PIN_TX, MB_PIN_RX, rts_pin, UART_PIN_NO_CHANGE),
                        TAG, "uart_set_pin failed");

    if (MB_PIN_RTS >= 0) {
        /* 半双工：RTS 由硬件自动翻转，发送期间接收器关闭（不会收到自己发的回显） */
        ESP_RETURN_ON_ERROR(uart_set_mode(s_port, UART_MODE_RS485_HALF_DUPLEX),
                            TAG, "uart_set_mode RS485 failed");
        ESP_LOGI(TAG, "RS485 half-duplex, DE/RE = GPIO%d", MB_PIN_RTS);
    } else {
        ESP_LOGI(TAG, "auto-direction transceiver (no DE/RE pin)");
    }

    uart_flush_input(s_port);
    s_ready = true;
    ESP_LOGI(TAG, "init ok: uart%d %d 8N1 tx=%d rx=%d",
             (int)MB_UART_PORT_NUM, MB_BAUD, MB_PIN_TX, MB_PIN_RX);
    return ESP_OK;
}

/* ---------------- 从站地址 ---------------- */
esp_err_t mb_set_slave(uint8_t addr)
{
    if (addr < MB_SLAVE_MIN || addr > MB_SLAVE_MAX) {
        ESP_LOGE(TAG, "invalid slave addr %u (1..247)", (unsigned)addr);
        return ESP_ERR_INVALID_ARG;
    }
    s_slave = addr;
    ESP_LOGI(TAG, "slave addr = %u", (unsigned)s_slave);
    return ESP_OK;
}

uint8_t mb_get_slave(void)
{
    return s_slave;
}

/* ---------------- 内部工具 ---------------- */
static uint32_t resp_timeout_ms(uint16_t count)
{
    /* 11 bit/字节（1 起始 + 8 数据 + 1 校验/无校验 2 位停止，按最坏估算），再留 50% 余量 */
    const uint32_t frame_bytes = 5u + (uint32_t)count * 2u;
    uint32_t ms = (frame_bytes * 11u * 1000u) / (uint32_t)MB_BAUD;
    ms = ms * 3u / 2u + 100u;
    return (ms < MB_TIMEOUT_MS) ? MB_TIMEOUT_MS : ms;
}

/* 读 1 字节，超时返回 false */
static bool read_byte(uint8_t *b, TickType_t deadline)
{
    const TickType_t now = xTaskGetTickCount();
    if ((int32_t)(deadline - now) <= 0) {
        return false;
    }
    return uart_read_bytes(s_port, b, 1, deadline - now) == 1;
}

/**
 * @brief 扫描式读取响应帧
 *
 * 逐字节匹配帧头 [addr][fc][byte_count]，可容忍：
 *  - 自动方向收发器把自身发送内容回显到 RO（回显帧头字节数对不上，会被丢掉）
 *  - 总线上的零星噪声字节、上一帧残留
 * 异常响应 [addr][fc|0x80][code][crc] 也在这里识别，总长 5 字节。
 *
 * @return 实际收到的完整帧长度；0 表示超时或帧不完整
 */
static int read_response(uint8_t slave, uint8_t fc, uint8_t byte_count,
                         uint8_t *resp, int expect, uint32_t timeout_ms)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    const uint8_t exc_fc = (uint8_t)(fc | 0x80);
    int idx = 0;
    int need = expect;

    while (idx < 3) {
        uint8_t b;
        if (!read_byte(&b, deadline)) {
            return 0;
        }

        if (idx == 0 && b != slave) {
            continue;                       /* 丢字节直到遇到从站地址 */
        }
        if (idx == 1 && b != fc && b != exc_fc) {
            idx = (b == slave) ? 1 : 0;     /* 该字节可能是新帧的起点 */
            if (idx == 1) {
                resp[0] = b;
            }
            continue;
        }
        if (idx == 2) {
            if (resp[1] == exc_fc) {
                resp[2] = b;                /* 异常码 */
                idx = 3;
                need = 5;
                break;
            }
            if (b != byte_count) {
                idx = (b == slave) ? 1 : 0;
                if (idx == 1) {
                    resp[0] = b;
                }
                continue;
            }
        }
        resp[idx++] = b;
    }

    int got = idx;
    while (got < need) {
        const TickType_t now = xTaskGetTickCount();
        if ((int32_t)(deadline - now) <= 0) {
            break;
        }
        const int n = uart_read_bytes(s_port, resp + got, (uint32_t)(need - got), deadline - now);
        if (n <= 0) {
            break;
        }
        got += n;
    }
    return (got == need) ? got : 0;
}

static const char *exception_desc(uint8_t code)
{
    switch (code) {
    case 0x01: return "illegal function";
    case 0x02: return "illegal data address";
    case 0x03: return "illegal data value";
    case 0x04: return "slave device failure";
    case 0x05: return "acknowledge";
    case 0x06: return "slave device busy";
    default:   return "unknown";
    }
}

/* ---------------- 读寄存器 ---------------- */
esp_err_t mb_read_regs(uint8_t slave, uint8_t fc,
                       uint16_t start, uint16_t count, uint16_t *out)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (slave < MB_SLAVE_MIN || slave > MB_SLAVE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (fc != 0x03 && fc != 0x04) {
        return ESP_ERR_INVALID_ARG;
    }
    if (count == 0 || count > 125 || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 组请求帧 */
    uint8_t req[8];
    req[0] = slave;
    req[1] = fc;
    req[2] = (uint8_t)(start >> 8);
    req[3] = (uint8_t)(start & 0xFF);
    req[4] = (uint8_t)(count >> 8);
    req[5] = (uint8_t)(count & 0xFF);
    const uint16_t c = crc16(req, 6);
    req[6] = (uint8_t)(c & 0xFF);
    req[7] = (uint8_t)(c >> 8);

    /* 清掉上一轮残留字节，避免把脏数据当响应 */
    uart_flush_input(s_port);

    if (uart_write_bytes(s_port, req, sizeof(req)) != (int)sizeof(req)) {
        ESP_LOGE(TAG, "uart write failed");
        return ESP_FAIL;
    }
    /* 关键：等最后一位真正发到总线上，否则半双工切方向会截断帧 */
    uart_wait_tx_done(s_port, pdMS_TO_TICKS(200));

    /* 读响应：扫描式匹配帧头，自动方向收发器的自身回显会被自动跳过 */
    const int expect = 5 + (int)count * 2;
    uint8_t resp[5 + 250];
    const uint32_t to_ms = resp_timeout_ms(count);
    const int n = read_response(slave, fc, (uint8_t)(count * 2), resp, expect, to_ms);

    if (n == 0) {
        s_stats.timeout++;
        ESP_LOGW(TAG, "no/short response (got %d, expect %d, %ums)", n, expect, (unsigned)to_ms);
        return ESP_ERR_TIMEOUT;
    }
    if (resp[0] != slave) {
        s_stats.frame_err++;
        ESP_LOGW(TAG, "slave mismatch: got %02X want %02X",
                 (unsigned)resp[0], (unsigned)slave);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (resp[1] & 0x80) {
        s_stats.exception++;
        ESP_LOGW(TAG, "exception 0x%02X (%s)", (unsigned)resp[2], exception_desc(resp[2]));
        return ESP_FAIL;
    }
    if (resp[1] != fc || resp[2] != (uint8_t)(count * 2) || n != expect) {
        s_stats.frame_err++;
        ESP_LOGW(TAG, "frame error: fc=%02X bytes=%u (want %u), len=%d/%d",
                 (unsigned)resp[1], (unsigned)resp[2], (unsigned)(count * 2), n, expect);
        return ESP_ERR_INVALID_RESPONSE;
    }
    const uint16_t rc = crc16(resp, (size_t)(n - 2));
    if (resp[n - 2] != (uint8_t)(rc & 0xFF) || resp[n - 1] != (uint8_t)(rc >> 8)) {
        s_stats.crc_err++;
        ESP_LOGW(TAG, "crc error: got %02X%02X want %02X%02X",
                 (unsigned)resp[n - 2], (unsigned)resp[n - 1],
                 (unsigned)(rc & 0xFF), (unsigned)(rc >> 8));
        return ESP_ERR_INVALID_CRC;
    }

    /* Modbus 为大端：高字节在前 */
    for (int i = 0; i < count; i++) {
        out[i] = (uint16_t)((resp[3 + i * 2] << 8) | resp[4 + i * 2]);
    }

    s_stats.ok++;
    return ESP_OK;
}

esp_err_t mb_read_regs_retry(uint8_t fc, uint16_t start, uint16_t count,
                             uint16_t *out)
{
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt <= MB_RETRY_MAX; attempt++) {
        err = mb_read_regs(s_slave, fc, start, count, out);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        if (attempt < MB_RETRY_MAX) {
            vTaskDelay(pdMS_TO_TICKS(MB_RETRY_GAP_MS));
        }
    }
    return err;
}

const mb_stats_t *mb_get_stats(void)
{
    return &s_stats;
}

void mb_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}
