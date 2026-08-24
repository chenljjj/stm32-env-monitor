# stm32-env-monitor

基于 **STM32F103C8T6** 与 **ESP32-WROOM-32E** 的环境监测联网终端。STM32 通过 I2C 采集温湿度和光照、驱动 OLED 本地显示；ESP32 作为网络网关接收环境数据。两块 MCU 使用带 CRC 与 ACK 的自定义二进制 UART 协议通信。

当前版本已完成传感器采集、OLED 显示、双向 UART 与 ACK 实物联调；EMQX Serverless 的 TLS 账号认证和 MQTTX QoS 1 发布/订阅已经验证。ESP32 侧已实现 Wi-Fi 重连、MQTT/TLS、遥测队列、在线保留消息和离线遗嘱，并通过离线构建；设备端连接真实网络持续上报仍待硬件验证。

## 已完成的工作

- 基于 STM32CubeMX、HAL 和 STM32CubeIDE 完成 STM32F103C8T6 工程配置：HSE 8 MHz、系统时钟 72 MHz、SWD 下载调试。
- 在 I2C1（PB6/PB7，400 kHz）上接入并实测 AHT20、BH1750 与 SSD1306 0.96 英寸 OLED（128×64）。
- 实现基于 PC13 的运行状态指示，以及 USART2（PA2/PA3）文本日志。
- 将温度、湿度和照度以定点整数保存，避免传感器驱动层依赖浮点运算。
- 设计 STM32—ESP32 二进制 UART 协议：帧头、版本、长度、序号、CRC-16/CCITT-FALSE 与 ACK。
- STM32 使用 USART1 接收中断解析 ACK 与网络状态；ESP32 使用 UART2（GPIO25/GPIO26）解析环境帧，并回传 ACK、Wi-Fi/MQTT 状态及断线计数。
- 针对实际联调中 ACK 延迟一帧的问题，增加序号诊断字段并修正 STM32 ACK 状态机和 ESP32 UART 读取超时。
- STM32 对未确认环境帧执行 200 ms 超时、最多 3 次同序号重传；ESP32 对重复帧只回 ACK、不重复写入 MQTT 队列。
- 建立 ESP-IDF 网关工程，完成 ESP32-WROOM-32E 的编译、烧录、串口监视和双板联调。
- 实现 ESP32 Wi-Fi STA 事件驱动重连，以及获取 IPv4 后启动 MQTT 客户端。
- 使用 TLS 8883、CA 校验和用户名/密码连接 EMQX；遥测采用 QoS 1，设备状态使用 retained 消息与 LWT。
- 通过容量为 32 条的 FreeRTOS 队列解耦 UART 收帧与 MQTT 发布，并统计队列溢出、发布失败和 PUBACK。
- 实现 `NET_STATUS` 状态回传：网络状态改变时立即发送，同时每 10 个样本发送一次快照，便于 STM32 复位后重新同步。
- 将 ESP32 应用分区由默认 1 MiB 扩展为 4 MiB；当前固件约 895 KiB，分区剩余约 78%。

## 实测效果

已在真实硬件上验证以下完整链路：

```text
AHT20 / BH1750 → I2C → STM32 → OLED
                         │
                         └─ ENV_REPORT → ESP32 → ACK / NET_STATUS → STM32
```

STM32 串口日志中的稳定状态示例：

```text
sample=12 aht=OK valid=1 t=23.777C h=43.892% bh=OK valid=1 l=48.333lx
uart1=OK txerr=0 ack=11 rxerr=0 ackerr=0 exp=11 got=10 af=0x00 retry=0 timeout=0 drop=0 pending=0
```

其中 `ack` 持续递增、`rxerr=0`、`ackerr=0`、`af=0x00` 表示 ESP32 已正确接收并确认每帧环境数据。`retry=0 timeout=0 drop=0 pending=0` 表示链路无需恢复。`exp` 与 `got` 在同一行相差 1 属于异步日志时机的正常现象：当前帧已发出，上一帧 ACK 刚完成确认。

## 硬件清单

| 模块 | 型号 / 要求 | 作用 |
| --- | --- | --- |
| 主控 | STM32F103C8T6 Blue Pill | 传感器采集、本地显示和链路管理。 |
| 网关 | ESP32 DevKitC，ESP32-WROOM-32E | UART 数据网关、Wi-Fi 接入与 MQTT/TLS 上报。 |
| 温湿度 | AHT20 | I2C 温湿度采集。 |
| 光照 | BH1750 | I2C 光照采集，ADDR 接地时地址为 `0x23`。 |
| 显示 | SSD1306 0.96 英寸 128×64 I2C OLED | 本地显示温湿度和照度。 |
| 下载与日志 | ST-LINK V2、3.3 V USB-TTL、ESP32 板载 CP2102 | STM32 下载、STM32 日志、ESP32 烧录与日志。 |

## 接线

### I2C 设备

三块 I2C 设备共用 STM32 I2C1：

| STM32F103C8T6 | AHT20 / BH1750 / SSD1306 |
| --- | --- |
| 3.3V | VCC |
| GND | GND |
| PB6 | SCL |
| PB7 | SDA |

I2C 地址：AHT20 为 `0x38`，BH1750 为 `0x23`，SSD1306 为 `0x3C`。

### STM32—ESP32 UART

| STM32F103C8T6 | ESP32 DevKitC | 说明 |
| --- | --- | --- |
| PA9 / USART1_TX | GPIO25 / UART2_RX | STM32 上报环境帧。 |
| PA10 / USART1_RX | GPIO26 / UART2_TX | ESP32 回传 ACK 与 `NET_STATUS`。 |
| GND | GND | 必须共地。 |

两端均配置为 `115200-8-N-1`、无校验、无硬件流控。两块开发板使用各自 USB 供电时，**仅连接两根 UART 线和一根 GND**；不要在两块板之间连接 `3.3V`、`5V` 或 `VIN`。

## 软件设计

### STM32 侧

| 模块 | 职责 |
| --- | --- |
| `app_i2c` | 封装设备探测、I2C 收发和 7 位地址左移。 |
| `aht20` / `bh1750` | 传感器初始化、采样、数据换算和异常处理。 |
| `ssd1306` / `app_display` | 128×64 显存缓冲和页面显示。 |
| `app_monitor` | 1 秒周期采样、有效性判断及错误统计。 |
| `app_protocol` | 帧编解码、CRC 与环境载荷打包。 |
| `app_link` | USART1 单字节中断接收、ACK 校验、网络状态保存和链路统计。 |
| `app_log` | USART2 文本日志。 |
| `app_status` | PC13 LED 非阻塞状态指示。 |

### ESP32 侧

ESP32 的 UART0 经板载 CP2102 连接电脑，用于烧录与 ESP-IDF Monitor；UART2 重映射至 GPIO25/GPIO26，专用于板间协议。网关成功解析 `ENV_REPORT` 后立即返回 ACK，再把采样快照加入 FreeRTOS 队列，独立发布任务等待 MQTT 连接并转换为 JSON。Wi-Fi/MQTT 事件经过统一状态聚合器生成 `NET_STATUS`；ACK 与状态帧共用发送互斥锁，避免来自不同任务的字节交叉。这样网络连接、TLS 握手和 Broker 重连不会阻塞 UART 收帧。

```text
STM32 ENV_REPORT
      │
      ▼
ESP32 UART 状态机 ── CRC/长度/语义校验 ── ACK
      │
      ▼
FreeRTOS 遥测队列（32 条）
      │
      ▼
Wi-Fi STA ── TLS 8883 ── EMQX Serverless
      │
      └─ 状态变化/周期快照 ── NET_STATUS ──→ STM32
```

MQTT 主题：

| 主题 | QoS | Retain | 内容 |
| --- | ---: | --- | --- |
| `stm32-env-monitor/telemetry` | 1 | 否 | 温度、湿度、照度、有效标志和错误计数。 |
| `stm32-env-monitor/status` | 1 | 是 | 连接后发布 `online=true`；异常断线由 LWT 发布 `online=false`。 |

目前 MQTTX 已完成 Broker 侧 TLS 认证与发布/订阅回环验证；ESP32 固件已构建通过，但设备端实际 Wi-Fi/MQTT 日志尚未采集，因此不把联网代码等同于硬件验证结果。

## UART 协议摘要

```text
+--------+--------+---------+------+-------------+-------------+---------+-------+
| SOF0   | SOF1   | Version | Type | Sequence    | Payload Len | Payload | CRC16 |
+--------+--------+---------+------+-------------+-------------+---------+-------+
| 0xA5   | 0x5A   | 1 byte  | 1 B  | uint16, LE  | uint16, LE  | 0~64 B  | 2 B   |
+--------+--------+---------+------+-------------+-------------+---------+-------+
```

- 协议版本：`0x01`；多字节字段显式使用小端序。
- CRC：CRC-16/CCITT-FALSE，`poly=0x1021`、`init=0xFFFF`、无反射、`xorout=0x0000`。
- CRC 覆盖范围：`Version` 至 `Payload`，不包含帧头和 CRC 字段。
- `ENV_REPORT`（`0x01`）载荷固定 40 字节，包含运行时间、采样序号、毫单位温湿度/照度、有效标志和传感器错误计数。
- `ACK`（`0x80`）载荷固定 4 字节：被确认序号、被确认消息类型和处理结果。结果 `0x00` 表示载荷已成功解包，`0x01` 表示类型不支持，`0x02` 表示载荷无效；ACK 不代表 MQTT 已发布成功。
- `NET_STATUS`（`0x81`）载荷固定 12 字节：Wi-Fi/MQTT 连接标志、变化原因、Wi-Fi 断线次数和 MQTT 断线次数。状态变化时立即发送，并每 10 个环境样本补发一次快照。

完整字段定义、ACK 状态机、诊断字段和实际故障定位过程见 [UART 协议与联调记录](docs/03-STM32与ESP32串口协议与调试.md)。

## 快速复现

### 1. 准备开发环境

- 安装 STM32CubeIDE、STM32CubeProgrammer 和 VS Code。
- 在 VS Code 安装 Espressif ESP-IDF 扩展，并安装 ESP-IDF（本项目已使用 ESP-IDF v6.0.2 验证）。
- 为 ESP32 板载 CP2102 安装 Windows VCP 驱动，确认设备管理器出现新的 `COMx`。

### 2. 构建并烧录 STM32

1. 在 STM32CubeIDE 打开 `firmware/stm32-env-monitor`。
2. 选择 `Debug` 配置并构建。
3. ST-LINK 连接 `3.3V`、`GND`、`SWDIO(PA13)`、`SWCLK(PA14)`。
4. 执行 `Run As > STM32 C/C++ Application` 下载程序。
5. 使用 USB-TTL 打开 USART2：USB-TTL `GND → GND`、`RXD → PA2`，终端设为 115200、8-N-1、无流控。

### 3. 构建并烧录 ESP32

1. 在 VS Code 打开 `firmware/esp32-env-gateway`。
2. 复制 `main/app_secrets.h.example` 为 `main/app_secrets.h`，填写 2.4 GHz Wi-Fi 与 MQTT 连接参数；不要提交真实密码。
3. 项目的 `sdkconfig.defaults` 已配置 16 MiB Flash 和 4 MiB 单应用分区。选择 CP2102 对应的 `COMx`，在 ESP-IDF 终端运行：

   ```powershell
   idf.py -p COMx build flash monitor
   ```

4. 看到下列日志后，ESP32 网关已准备接收：

   ```text
   env_gateway: UART2 已启动: RX=GPIO25 TX=GPIO26 115200-8-N-1
   ```

### 4. 联调验证

1. 按上文连接传感器、OLED、STM32 和 ESP32。
2. 先启动 ESP32，再复位 STM32。
3. ESP-IDF Monitor 应每秒出现一条 `sample=` 环境日志。
4. STM32 USART2 日志中应看到 `ack` 持续递增，且 `rxerr=0 ackerr=0 af=0x00 retry=0 timeout=0 drop=0 pending=0`；收到网络快照后 `ns` 应递增，`net=WM` 表示 Wi-Fi 与 MQTT 均已连接。
5. MQTTX 订阅 `stm32-env-monitor/telemetry` 和 `stm32-env-monitor/status`，确认收到 QoS 1 遥测与在线状态。
6. 断开 Wi-Fi 或 Broker，检查自动重连、队列溢出统计和 LWT；恢复网络后检查队列中的待发数据继续发布。

## 目录结构

```text
stm32-env-monitor/
├── firmware/
│   ├── stm32-env-monitor/       # STM32CubeIDE / CubeMX 工程
│   └── esp32-env-gateway/       # ESP-IDF 网关工程
├── docs/
│   ├── 01-CubeIDE工程配置与基线验证.md
│   ├── 02-项目实现路线图.md
│   ├── 03-STM32与ESP32串口协议与调试.md
│   ├── 04-ESP32-WiFi-MQTT-TLS与遥测队列.md
│   └── 05-STM32-FreeRTOS运行时设计.md
└── README.md
```

## 后续计划

1. 完成 ESP32 设备端 Wi-Fi/MQTT/TLS 实测，并进行断网、Broker 不可达和错误凭据故障注入。
2. 在硬件上验证 UART 超时重传、重复帧去重和 `NET_STATUS` 的状态变化与周期快照。
3. 将 Wi-Fi/MQTT 状态接入 OLED 健康页面，区分 UART 处理成功、消息进入 ESP-MQTT outbox 与 Broker PUBACK 三层状态。
4. 在 STM32 轮询和中断版本稳定后引入 FreeRTOS，通过队列传递不可变采样快照。

## 相关文档

- [CubeIDE 工程配置与基线验证](docs/01-CubeIDE工程配置与基线验证.md)
- [实现路线图](docs/02-项目实现路线图.md)
- [STM32—ESP32 UART 协议与联调记录](docs/03-STM32与ESP32串口协议与调试.md)
- [ESP32 Wi-Fi、MQTT/TLS 与遥测队列](docs/04-ESP32-WiFi-MQTT-TLS与遥测队列.md)
- [STM32 FreeRTOS 运行时设计](docs/05-STM32-FreeRTOS运行时设计.md)
