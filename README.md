# stm32-env-monitor

基于 STM32F103C8T6 与 ESP32-WROOM-32E 的双 MCU 环境监测联网终端。STM32 负责传感器采集、OLED 本地显示和可靠串口链路；ESP32 负责 Wi-Fi、MQTT over TLS 与云端遥测发布。

项目已完成真实硬件端到端联调：AHT20、BH1750、SSD1306 OLED、STM32—ESP32 UART ACK、EMQX MQTT/TLS、MQTTX 订阅，以及 Wi-Fi 断网后的自动重连与离线队列补发。

## 功能概览

```text
 AHT20 ─┐
BH1750 ─┼─ I2C1 ──> STM32F103C8T6 ── UART 二进制协议 ──> ESP32-WROOM-32E
SSD1306─┘                  │                                      │
                           ├─ OLED：温湿度 / 照度 / 网络状态       ├─ Wi-Fi STA
                           ├─ USART2：调试日志                     └─ MQTT over TLS ──> EMQX
                           └─ FreeRTOS：采集、显示、链路、健康任务
```

| 模块 | 实现内容 |
| --- | --- |
| 环境采集 | AHT20 温湿度、BH1750 照度，I2C1 400 kHz，定点数保存与范围校验。 |
| 本地显示 | SSD1306 128×64 帧缓冲显示温湿度、照度，以及 `WIFI:ON MQTT:ON` 网络状态。 |
| STM32 运行时 | CMSIS-RTOS v2 / FreeRTOS，使用 `SensorTask`、`DisplayTask`、`LinkTask`、`HealthTask`；I2C1 由互斥锁保护，快照经消息队列传递。 |
| 板间通信 | USART1 二进制协议，含帧头、版本、长度、序号、CRC-16/CCITT-FALSE、ACK、超时重传与重复帧抑制。 |
| 联网网关 | ESP-IDF、Wi-Fi STA 事件重连、MQTT 3.1.1 over TLS、QoS 1、在线 retained 状态、LWT 与有界离线队列。 |
| 可观测性 | STM32 USART2 与 ESP-IDF Monitor 输出分层日志；统计 ACK、重试、超时、队列、堆与任务栈高水位。 |

## 配套上位机

日志可视化与 MQTT JSON 消息解析上位机位于独立仓库：[env-monitor-studio](https://github.com/chenljjj/env-monitor-studio)。该工具用于连接设备调试日志、查看 MQTT 遥测和辅助端到端联调。

## 已验证结果

### 本地采集与双板通信

- AHT20、BH1750、SSD1306 已同挂 I2C1 完成实测；传感器数据持续有效。
- STM32 与 ESP32 已完成双向 UART 实测。稳定状态下 `ack` 持续递增，`rxerr=0`、`ackerr=0`。
- 曾定位并修复 ACK 落后一帧问题：通过 `exp/got/af` 诊断字段将问题收敛到 ESP32 UART 批量读取等待过长，随后缩短读取超时并优先回 ACK。

```text
sample=12 ... uart1=OK txerr=0 ack=11 rxerr=0 ackerr=0
exp=11 got=10 af=0x00 retry=0 timeout=0 drop=0
```

`exp` 和 `got` 在同一日志行相差 1 是异步日志时机导致的正常现象；关键是 ACK 持续增长且错误统计保持为 0。

### MQTT/TLS 与断网恢复

- MQTTX 已完成 TLS、CA、账号认证和 QoS 1 发布/订阅基线验证。
- ESP32 已将真实环境 JSON 发布到 MQTT Broker，MQTTX 可订阅遥测主题。
- 关闭热点后，本地采集、OLED 和 UART ACK 持续工作；状态会经历 `WM → -- → W- → WM`。
- 实测断网期间离线遥测队列峰值为 14，网络恢复后清空，`overflow=0`。

### FreeRTOS 短时资源基线

在传感器、OLED、双板通信和 MQTT 同时运行约 65 秒后，记录到：

```text
rtos heap=3344 min=2720 stack=1140/896/864/644 q=1/0 peak=1/1 qdrop=0/0
```

其中任务栈顺序为采集/显示/链路/健康，单位为字节。这是早期短时资源基线；后续 4 小时 27 分 47 秒的三端长稳结果见下一节。

### 三端长稳记录（2026-08-27）

对导出的 STM32、ESP32 与 MQTT 日志按采样号关联分析后，真实设备从 `sample=8` 连续运行至 `16075`。ESP32 `uptime_ms` 从 `7084` 单调增加到 `16074084`，相邻 16067 次增量均为 1000 ms，对应 **4 小时 27 分 47 秒**的 1 Hz 端到端在线运行。

| 检查项 | 结果 |
| --- | --- |
| 三端样本对应 | STM32、ESP32、MQTT 各 16068 条，采样序号无缺口。 |
| 传感器与显示 | 全部样本均为 AHT20/BH1750 有效；MQTT JSON 解析无失败，三项有效标志均为 `true`，四类传感器错误均为 0。 |
| UART 链路 | `rxerr=0`、`txerr=0`、`ackerr=0`、`retry=0`、`timeout=0`、`drop=0`，STM32 队列丢弃为 `qdrop=0/0`。 |
| 网络与云端 | STM32 全程为 `net=WM`，Wi-Fi/MQTT 断线计数未增长；末次网关统计为 `queue=0 submitted=16072 accepted=16072 puback=16072 puberr=0 overflow=0 abandoned=0`。 |
| FreeRTOS 资源 | 共记录 267 条资源日志，`heap=3344`、`min=2720` 始终不变；四任务最小剩余栈保持正数，最低观测值为 `1140/848/864/616` 字节。 |

CSV 的 `time` 是日志被上位机接收/记录的时间，少量相邻行间隔会受串口缓冲和界面调度影响；稳定性判断以连续采样序号、ESP32 单调 `uptime_ms` 和三端一一对应的内容为准。本记录已通过 30 分钟基线，证明当前在线链路可连续运行约 4.5 小时；它仍不覆盖 24 小时运行、断网队列打满、ACK 丢失和异常断电等故障边界。

## 硬件与接线

| 模块 | 接口 / 参数 |
| --- | --- |
| STM32 主控 | STM32F103C8T6 Blue Pill，HSE 8 MHz，SYSCLK 72 MHz。 |
| ESP32 网关 | ESP32 DevKitC，ESP32-WROOM-32E。 |
| AHT20 | I2C，7 位地址 `0x38`。 |
| BH1750 | I2C，ADDR 接地时 7 位地址 `0x23`。 |
| SSD1306 | I2C，128×64，常用 7 位地址 `0x3C`。 |

### I2C1

| STM32 | AHT20 / BH1750 / SSD1306 |
| --- | --- |
| 3.3V | VCC |
| GND | GND |
| PB6 | SCL |
| PB7 | SDA |

### STM32—ESP32 UART

| STM32 | ESP32 | 作用 |
| --- | --- | --- |
| PA9 / USART1_TX | GPIO25 / UART2_RX | `ENV_REPORT` 上报。 |
| PA10 / USART1_RX | GPIO26 / UART2_TX | ACK 与 `NET_STATUS` 回传。 |
| GND | GND | 两板必须共地。 |

串口参数为 `115200-8-N-1`、无校验、无硬件流控。两块开发板分别使用 USB 供电时，板间只连接 TX、RX、GND；不要把两块板的 `3.3V`、`5V` 或 `VIN` 直接相连。

## 协议与状态设计

UART 帧格式：

```text
+--------+--------+---------+------+-------------+-------------+---------+-------+
| SOF0   | SOF1   | Version | Type | Sequence    | Payload Len | Payload | CRC16 |
+--------+--------+---------+------+-------------+-------------+---------+-------+
| 0xA5   | 0x5A   | 1 byte  | 1 B  | uint16, LE  | uint16, LE  | 0~64 B  | 2 B   |
+--------+--------+---------+------+-------------+-------------+---------+-------+
```

- `ENV_REPORT`：STM32 每秒发送一次环境快照。
- `ACK`：ESP32 仅确认 UART 帧已通过协议解析；不等同于 MQTT 已成功发布。
- `NET_STATUS`：ESP32 回传 Wi-Fi/MQTT 状态与累计断线次数，供 STM32 日志和 OLED 显示。
- STM32 在发送前登记期待序号；200 ms 未收到匹配 ACK 时最多同序号重传 3 次。
- ESP32 对重复帧仍回 ACK，但不重复写入 MQTT 发布队列。

完整字段、状态机和故障定位记录见 [STM32与ESP32串口协议与调试](docs/03-STM32与ESP32串口协议与调试.md)。

## 快速复现

### 1. STM32 工程

1. 使用 STM32CubeIDE 打开 [firmware/stm32-env-monitor](firmware/stm32-env-monitor)。
2. 选择 `Debug` 配置构建。
3. 通过 ST-LINK 连接 `3.3V`、`GND`、`PA13/SWDIO`、`PA14/SWCLK` 并下载。
4. 使用 3.3 V USB-TTL 查看 USART2：`RXD → PA2`、`GND → GND`，终端设为 115200-8-N-1。

### 2. ESP32 工程

1. 使用 VS Code + ESP-IDF 打开 [firmware/esp32-env-gateway](firmware/esp32-env-gateway)。
2. 复制 `main/app_secrets.h.example` 为本机 `main/app_secrets.h`，填写自己的 2.4 GHz Wi-Fi 和 MQTT 部署参数。
3. 选择 ESP32 对应 `COMx`，执行：

   ```powershell
   idf.py -p COMx build flash monitor
   ```

4. 看到 `UART2 已启动: RX=GPIO25 TX=GPIO26 115200-8-N-1` 后，再接入 STM32 UART。

### 3. 联调与云端观察

1. 连接三个 I2C 设备与两板 UART，先启动 ESP32，再复位 STM32。
2. 确认 STM32 日志中 `ack` 持续递增且 `ackerr=0`。
3. 使用 MQTTX 订阅：
   - `stm32-env-monitor/telemetry`
   - `stm32-env-monitor/status`
4. 关闭并恢复热点，确认 STM32 本地采样持续、ESP32 自动重连、离线队列最终清空。

## 私密配置约定

真实 Wi-Fi 凭据、MQTT 账号密码、私钥及部署证书不应提交。仓库提供 `app_secrets.h.example` 作为模板；本机实际配置应保存为被 Git 忽略的 `app_secrets.h`。公开日志和文档只记录匿名化状态、错误码与统计值。

## 当前边界与后续验证

- 已完成 4 小时 27 分 47 秒的三端在线长稳记录；尚未完成 24 小时运行及故障注入场景下的长稳测试。
- 尚待人为制造 ACK 丢失、队列满、ESP32 非正常断电，验证重传、去重、LWT 和受控丢弃策略。
- 当前 ESP32 使用 4 MiB 单应用分区；尚未实现 OTA 双槽、持久化离线缓存和远程配置。

## 目录

```text
stm32-env-monitor/
├── firmware/
│   ├── stm32-env-monitor/       # STM32CubeIDE / CubeMX 工程
│   └── esp32-env-gateway/       # ESP-IDF 网关工程
├── docs/                        # 公开技术文档
└── README.md
```

## 文档索引

- [CubeIDE工程配置与基线验证](docs/01-CubeIDE工程配置与基线验证.md)
- [项目实现路线图](docs/02-项目实现路线图.md)
- [STM32与ESP32串口协议与调试](docs/03-STM32与ESP32串口协议与调试.md)
- [ESP32 WiFi、MQTT TLS与遥测队列](docs/04-ESP32-WiFi-MQTT-TLS与遥测队列.md)
- [STM32 FreeRTOS运行时设计](docs/05-STM32-FreeRTOS运行时设计.md)
- [运行时资源监控与长稳测试](docs/06-运行时资源监控与长稳测试.md)
