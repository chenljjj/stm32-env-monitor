# STM32—ESP32 UART 协议与联调记录

本文记录 `stm32-env-monitor` 当前使用的二进制 UART 协议、实际接线、ACK 状态机和实测调试过程。STM32 负责采样和本地显示；ESP32 负责网关处理，并将在后续接入 Wi-Fi 与 MQTT。

## 1. 通信通道与接线

两个 UART 通道相互独立：

```text
STM32 USART2 (PA2/PA3) ←→ CH340 / PC 文本日志
STM32 USART1 (PA9/PA10) ←→ ESP32 UART2 (GPIO25/GPIO26) 二进制协议
ESP32 UART0 (GPIO1/GPIO3) ←→ CP2102 / PC 烧录和日志
```

| 信号 | STM32F103C8T6 | ESP32 DevKitC | 方向 |
| --- | --- | --- | --- |
| 数据发送 | PA9 / USART1_TX | GPIO25 / UART2_RX | STM32 → ESP32 |
| 数据返回 | PA10 / USART1_RX | GPIO26 / UART2_TX | ESP32 → STM32 |
| 参考地 | GND | GND | 双向共地 |

双方均采用 `115200-8-N-1`、无校验、无硬件流控。两块板通过各自 USB 供电时，只连接两根 UART 信号线和一根 GND；不连接两块板之间的 `3.3V`、`5V` 或 `VIN`。两端 GPIO 都是 3.3 V 逻辑电平。

## 2. 帧格式与校验

```text
+--------+--------+---------+------+-------------+-------------+---------+-------+
| SOF0   | SOF1   | Version | Type | Sequence    | Payload Len | Payload | CRC16 |
+--------+--------+---------+------+-------------+-------------+---------+-------+
| 0xA5   | 0x5A   | 1 byte  | 1 B  | uint16, LE  | uint16, LE  | 0~64 B  | 2 B   |
+--------+--------+---------+------+-------------+-------------+---------+-------+
```

- 协议版本：`0x01`。
- 多字节字段：显式小端序；不直接发送 C 结构体，避免填充、对齐和端序问题。
- 最大载荷：64 字节。
- CRC：CRC-16/CCITT-FALSE，`poly=0x1021`、`init=0xFFFF`、无反射、`xorout=0x0000`。
- CRC 覆盖 `Version` 至 `Payload`，不包含帧头和 CRC 自身。

两端均用逐字节状态机解析。版本、载荷长度或 CRC 任一项不合法即丢弃该帧并重新寻找帧头。

## 3. 消息与载荷

| 名称 | 值 | 方向 | 当前用途 |
| --- | ---: | --- | --- |
| `ENV_REPORT` | `0x01` | STM32 → ESP32 | 每秒上报一次环境采样快照。 |
| `PING` | `0x02` | 预留 | 后续链路探测。 |
| `ACK` | `0x80` | ESP32 → STM32 | 确认已处理 `ENV_REPORT`。 |
| `NET_STATUS` | `0x81` | 预留 | 后续回传 Wi-Fi/MQTT 状态。 |
| `PONG` | `0x82` | 预留 | 后续响应 `PING`。 |

`ENV_REPORT` 载荷固定 40 字节：

| 偏移 | 字段 | 类型 | 说明 |
| ---: | --- | --- | --- |
| 0 | `uptime_ms` | `uint32` | STM32 启动后的毫秒数。 |
| 4 | `sample_sequence` | `uint32` | 采样序号。 |
| 8 | `temperature_milli_c` | `int32` | 温度，单位 `milli °C`。 |
| 12 | `humidity_milli_rh` | `uint32` | 湿度，单位 `milli %RH`。 |
| 16 | `illuminance_milli_lux` | `uint32` | 照度，单位 `milli lx`。 |
| 20 | `valid_flags` | `uint8` | bit0 温湿度有效，bit1 照度有效，bit2 OLED 已就绪。 |
| 21、22 | 状态 | `uint8` | AHT20 与 BH1750 最近 HAL 状态。 |
| 23 | `reserved` | `uint8` | 当前写 0。 |
| 24、28、32、36 | 错误计数 | `uint32` | 两个传感器的通信错误和数据异常次数。 |

`ACK` 载荷固定 4 字节：偏移 0~1 是被确认的 STM32 帧序号（`uint16` 小端），偏移 2 是被确认类型（当前必须为 `ENV_REPORT`），偏移 3 是处理结果（`0` 表示成功）。

## 4. ACK 状态机

1. STM32 为新采样生成序号为 `N` 的 `ENV_REPORT`。
2. STM32 **在发送前**写入 `expected_ack_sequence=N` 并置 `awaiting_ack=1`。
3. STM32 通过 USART1 发送；若发送失败，撤销等待状态。
4. ESP32 完整解析帧后，先回传携带 `N` 的 ACK，再输出调试日志。
5. STM32 在 USART1 单字节接收中断中解析 ACK，校验长度、确认类型、序号和结果码。
6. 校验通过则 `ack_count` 加一；失败则 `ack_error_count` 加一并保留诊断信息。

ESP32 的 UART2 读取一次最多等待 20 ms。这样单帧不足接收缓冲区长度时，也不会因长时间等待收满而延迟 ACK。

当前没有重传策略；后续应加入 ACK 超时、有限次数重传和 `NET_STATUS` 网络状态回传。

## 5. 链路诊断与实测结果

STM32 USART2 日志包含：

```text
uart1=OK txerr=0 ack=11 rxerr=0 ackerr=0 exp=11 got=10 af=0x00
```

| 字段 | 说明 |
| --- | --- |
| `uart1` / `txerr` | 当前帧发送状态与累计发送失败次数。 |
| `ack` | 通过完整校验的 ACK 累计次数。 |
| `rxerr` | UART 接收或协议解析错误累计次数。 |
| `ackerr` | ACK 语义校验失败累计次数。 |
| `exp` | 当前刚发送帧的期待 ACK 序号。 |
| `got` | 最近收到 ACK 所确认的序号。 |
| `af` | 最近 ACK 的错误标志；`0x00` 表示通过。 |

`exp` 与 `got` 在同一行可能相差 1：日志打印时已经登记当前帧，而 `got` 通常是上一帧刚通过校验的 ACK。稳定工作的标准是 `ack` 持续递增、`rxerr=0`、`ackerr=0`、`af=0x00`。

| `af` 标志 | 含义 |
| ---: | --- |
| `0x01` | ACK 载荷长度错误。 |
| `0x02` | 被确认的消息类型错误。 |
| `0x04` | 未处于等待 ACK 状态。 |
| `0x08` | ACK 序号与期待序号不一致。 |
| `0x10` | ACK 结果码非 0。 |

2026-08-23 已完成实测：AHT20、BH1750 数据均有效，STM32 连续输出 `ack` 递增，`rxerr=0 ackerr=0 af=0x00`；ESP32 同步解析并打印温湿度、照度和有效标志。

## 6. 实际故障定位复盘

首次联调现象为：ESP32 能收到环境帧，STM32 `rxerr=0`，但出现 `ackerr` 增长和 `af=0x08`，且 `got` 总比 `exp` 小 1。

定位步骤：

1. `rxerr=0` 表明接线、115200 配置、帧头、长度和 CRC 均正常，排除物理层和解析层问题。
2. 增加 `exp`、`got`、`af` 诊断字段，确定问题是 ACK 序号落后一帧。
3. 对照 ESP32 日志，发现多个样本在同一 ESP 时间戳输出，说明 UART 数据被批量处理。
4. 检查 `uart_read_bytes(..., 128, pdMS_TO_TICKS(1000))`：单帧不足 128 字节时，最多会等待 1000 ms，ACK 因此落后一个采样周期。
5. 将最大等待改为 20 ms，并把 ESP32 的 ACK 发送移动到日志输出之前；STM32 同时改为发送前登记期待序号。

修复后链路稳定。以后按以下顺序排查：供电与共地 → TX/RX 交叉和串口参数 → STM32/ESP32 独立日志 → `rxerr` → `ackerr` → `exp/got/af`。源码修改后必须分别 Build 和 Flash；仅复位不会更新 Flash 中的程序。

## 7. 后续网络阶段

ESP32 后续应把最近一次有效 `ENV_REPORT` 转成 JSON 上报 MQTT，并保留有效标志和错误计数。Wi-Fi 与 MQTT 断线处理不得阻塞 UART2；网络状态通过 `NET_STATUS` 回传 STM32，供 OLED 和 USART2 日志显示。
