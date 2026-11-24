# ESP32-S3 Modbus-RTU 网关

通过 RS485 采集 Modbus-RTU 从站数据（FC03 读保持寄存器 / FC04 读输入寄存器），
经 WiFi + MQTT 上报到云端（云端再由桥接服务写入 MongoDB）。

```
从站设备 ──RS485(A/B)──> MAX485/自动方向收发器 ──UART──> ESP32-S3 ──WiFi──> MQTT Broker ──> 桥接服务 ──> MongoDB
```

## 功能范围

| 模块 | 状态 |
|---|---|
| Modbus-RTU 主站（FC03/FC04，单从站，1~125 寄存器） | 已实现，含 CRC、超时、重试、异常码解析、统计计数 |
| RS485 方向控制（硬件 RTS / 自动方向收发器） | 已实现，menuconfig 可切 |
| 30s 周期轮询（`vTaskDelayUntil`，不漂移） | 已实现 |
| WiFi STA 连接 + 断线无限重连 | 已实现（可开关） |
| SNTP 校时 | 已实现（可开关） |
| MQTT 上报 + LWT 在线状态 + 自动重连 | 已实现（可开关） |
| 物理量解析（缩放、32 位浮点字序） | **留空**，后续在 `handle_data()` 里加 |
| 断网缓存补传 / OTA / 以太网 / 4G | 未实现，见文末路线 |

## 目录

```
esp32s3_mb_gateway/
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
└── main/
    ├── CMakeLists.txt
    ├── Kconfig.projbuild   # 所有参数在 menuconfig 里调
    ├── config.h            # Kconfig -> 宏映射
    ├── modbus.c/.h         # RS485 + 帧收发 + CRC + 重试
    ├── app_wifi.c/.h       # WiFi STA
    ├── app_mqtt.c/.h       # MQTT 客户端 + topic + LWT
    ├── app_time.c/.h       # SNTP
    ├── app_event.c/.h      # 连接状态事件组
    ├── app_util.c/.h       # 设备 ID（MAC）
    └── main.c              # 采集任务 + JSON 组包 + 上报
```

## 硬件接线

| RS485 模块 | ESP32-S3 | 说明 |
|---|---|---|
| VCC | 3V3 | 模块必须支持 3.3V 电平 |
| GND | GND | 共地，否则通信不稳 |
| DI | GPIO17 | UART TX → 模块 DI |
| RO | GPIO18 | 模块 RO → UART RX |
| DE + /RE（短接） | GPIO16 | 普通 MAX485 必需；自动方向收发器不用接，menuconfig 里把 RTS 填 -1 |
| A / B | 从站 A / B | 总线两端各加 120Ω 终端电阻 |

> GPIO 只是默认值，可在 `menuconfig -> Modbus-RTU Gateway` 里改。
> 别用 GPIO19/20（USB）、GPIO0/45/46（启动脚）。

## 快速开始

```bash
cd esp32s3_mb_gateway

# 1. 生成 sdkconfig 并设目标
idf.py set-target esp32s3

# 2. 调参数（从站地址 / 波特率 / 寄存器 / 引脚 / WiFi / MQTT）
idf.py menuconfig
#   -> Modbus-RTU Gateway

# 3. 编译烧录
idf.py build
idf.py -p COM3 flash monitor
```

### 第一阶段：只验证 Modbus（默认配置）

`APP_WIFI_ENABLE` 默认 **n**，固件只读寄存器并在串口打印，30 秒一次：

```
I (12345) app: reg[  0]:    12    34     0     0     0     0     0     0
I (12345) app: reg[  8]:     0     0     0     0     0     0     0     0
I (12345) app:   ... (125 regs read, only first 16 printed)
```

同时用电脑上的 **Modbus Poll / QModMaster**（9600 8N1、同一从站地址）读同一段寄存器，
**逐项对比前几个值**，确认两件事：

1. **地址基准**：设备手册写 40001~40125 → `MB_REG_START = 0`；若手册本身 0 基则同样是 0。
   如果读出来全 0 或报异常码 02，先把 `MB_REG_START` ±1 试一次。
2. **帧质量**：`stats: ok=.. timeout=.. crc=..` 里 crc/frame 长期为 0 才算总线健康。

### 第二阶段：接云端

```bash
idf.py menuconfig
#   -> Network & Upload -> Enable WiFi + MQTT upload
#   填 SSID / password / broker URI / topic prefix
```

上报报文（topic 固定为 `<prefix>/<deviceId>/data`，QoS1）：

```json
{
  "deviceId": "ESP32S3-A1B2C3",
  "ts": 1764000000,
  "slave": 1,
  "fc": 3,
  "regStart": 0,
  "count": 125,
  "regs": [12, 34, 0, 0, "..."]
}
```

`ts` 为 0 表示 SNTP 还没校时成功（网关启动后几秒内完成）。
在线状态走 `<prefix>/<deviceId>/status`，retain=1 + LWT，断线自动变 `{"online":false}`。

## 关键实现说明

1. **方向控制**：`uart_set_mode(UART_MODE_RS485_HALF_DUPLEX)` 用硬件 RTS 自动翻转 DE/RE，
   固件不用管时序。用自动方向收发器时 `MB_PIN_RTS = -1`，此时会额外丢一次回显。
2. **发送后 `uart_wait_tx_done`**：不等最后一位发完就切方向会截断帧，这是最常见的"偶发 CRC 错"原因。
3. **响应读满预期长度**：`uart_read_bytes` 会提前返回，代码里用 `read_exact()` 循环读，
   否则会出现间歇性的"半帧"。
4. **超时动态计算**：`5 + count*2` 字节 × 11 bit / 波特率 × 1.5 + 100ms，下限 500ms。
5. **周期不漂移**：`vTaskDelayUntil` 而非 `vTaskDelay`。
6. **重试策略**：单次事务失败后间隔 50ms 重试，最多 `MB_RETRY_MAX`（默认 2）次，
   连续失败只记日志不重启，链路恢复后自动继续。

## 常见问题

| 现象 | 排查方向 |
|---|---|
| 一直 `no/short response` | A/B 接反；DE/RE 脚没接或接错；从站地址/波特率不符；未共地 |
| `crc error` 偶发 | 120Ω 终端电阻缺失；线太长/未用屏蔽双绞；波特率偏差；`uart_wait_tx_done` 被删 |
| `exception 0x02` | 寄存器地址越界（起始地址或数量超了从站实际范围） |
| 读出来全是 0 / 值明显不对 | 地址基准差 1；数据类型是 32 位浮点但按 16 位读了 |
| `mqtt offline, data dropped` | WiFi 断、broker URI/端口错、broker 需要鉴权但没填账号 |
| MQTT 连上但没数据 | topic 前缀不一致；订阅端 `dev/+/data` 没匹配到 deviceId |

## 后续路线

1. **物理量解析**：在 `main.c` 的 `handle_data()` 里加寄存器表（类型、缩放、字节序 ABCD/CDAB），
   把 `regs[]` 映射成 `{"temp":23.5,"pressure":101.3}`。
2. **断网缓存**：环形缓冲写 NVS/SD，恢复后按时间戳补传（MongoDB 侧靠 `ts` 去重）。
3. **云端桥接**：EMQX/Mosquitto → Node.js 或 Python 订阅 → MongoDB（时序数据建议 Time Series Collection）。
4. **OTA + 远程配置**：现场设备不便拆机，OTA 与远程改采集点表是刚需。
5. **多从站轮询调度** + 以太网（W5500）/4G（Cat.1 模组）主备链路切换。
