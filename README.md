# stm32-env-monitor

基于 STM32F103C8T6 与 ESP32 的环境监测联网终端。STM32 负责传感器采集、本地 OLED 显示和设备状态管理；ESP32 负责 Wi-Fi 连接与 MQTT 数据上报。两者通过独立的二进制 UART 协议通信。

## 1. 当前状态

| 功能 | 状态 | 说明 |
| --- | --- | --- |
| STM32 工程构建与烧录 | 已完成 | 已通过 ST-LINK 烧录，并验证断电重启后可正常运行。 |
| 调试日志 | 已完成 | USART2（PA2/PA3），115200 8-N-1。 |
| BH1750 光照采集 | 已实测 | I2C1 实测可读取有效照度值。 |
| SSD1306 OLED 显示 | 已实测 | I2C1 实测显示环境数据和状态。 |
| AHT20 温湿度采集 | 已实测 | I2C1 实测可稳定读取温湿度。 |
| STM32—ESP32 协议 | 已实测 | USART1 双向帧收发、CRC、序号 ACK 与异常统计均已验证。 |
| ESP32 网关工程 | UART 已实测 | ESP-IDF 网关已解析环境帧并返回 ACK；Wi-Fi/MQTT 尚未接入。 |
| FreeRTOS | 未启用 | 计划在轮询功能稳定后引入。 |

## 2. 系统架构

```text
┌─────────┐  I2C1 (PB6/PB7)  ┌────────┐
│  AHT20  │──────────────────│        │
├─────────┤                  │ STM32  │── USART1 二进制协议 ──┐
│ BH1750  │──────────────────│ F103C8 │                       │
├─────────┤                  │        │── USART2 调试日志 ────┼── USB-TTL / PC
│ SSD1306 │──────────────────│        │                       │
└─────────┘                  └────────┘                       │
                                                           ┌───▼────┐
                                                           │ ESP32  │── Wi-Fi / MQTT
                                                           └────────┘
```

## 3. 硬件与接口

| 模块 | STM32 接口 | 参数 / 说明 |
| --- | --- | --- |
| 板载 LED | PC13 | 低电平点亮，用于启动故障与运行心跳指示。 |
| I2C1 | PB6=SCL、PB7=SDA | 400 kHz，连接 AHT20、BH1750、SSD1306。 |
| AHT20 | I2C 地址 `0x38` | 温度、相对湿度。 |
| BH1750 | I2C 地址 `0x23` | ADDR 接地时使用该地址；连续高分辨率模式。 |
| SSD1306 | I2C 地址 `0x3C` | 0.96 英寸、128×64、I2C 接口。 |
| STM32—ESP32 | USART1：PA9/PA10 | 115200、8-N-1；PA9 TX → ESP32 GPIO25，PA10 RX ← ESP32 GPIO26。 |
| 调试串口 | USART2：PA2/PA3 | 115200、8-N-1；目前使用 PA2 TX 输出日志。 |
| 调试下载 | SWD：PA13/PA14 | 连接 ST-LINK。 |

所有 I2C 外设使用 3.3 V 供电并共地。ESP32 必须使用自身 USB 或足够电流能力的独立 5 V 电源，不能由 Blue Pill 的 3.3 V 引脚供电。

STM32 与 ESP32 同时通过 USB 供电时，只连接两根 UART 信号线和一根 GND，不连接两块板之间的 `3.3V`、`5V` 或 `VIN`。

## 4. 软件组成

### STM32 固件

路径：`firmware/stm32-env-monitor`

| 模块 | 职责 |
| --- | --- |
| `app_i2c` | 对 HAL I2C 做设备探测、收发和寄存器访问封装；公共接口使用 7 位设备地址。 |
| `aht20` | AHT20 初始化、触发测量、忙状态轮询和温湿度定点换算。 |
| `bh1750` | BH1750 初始化、连续高分辨率读取和照度定点换算。 |
| `ssd1306` | 128×64 显存缓冲、命令/数据传输和字符显示。 |
| `app_display` | 将监测快照渲染为 OLED 页面，并处理显示器重试。 |
| `app_monitor` | 周期采样、有效性判断、通信错误和数据异常统计。 |
| `app_log` | USART2 阻塞式文本日志输出。 |
| `app_status` | PC13 LED 的非阻塞状态指示。 |
| `app_protocol` | STM32—ESP32 帧编码、CRC 校验、环境数据打包和字节流解析。 |
| `app_link` | USART1 单字节中断接收、ACK 状态机和链路错误统计。 |

环境数据在内部采用定点整数，避免在驱动层依赖浮点运算：温度单位为 `milli °C`，湿度为 `milli %RH`，照度为 `milli lx`。

### ESP32 网关

路径：`firmware/esp32-env-gateway`

工程基于 ESP-IDF，目标芯片为 `esp32`（ESP32-WROOM-32E）。UART0 使用板载 CP2102 连接电脑，负责烧录和日志；UART2 重映射至 `GPIO25/RX`、`GPIO26/TX`，负责接收 STM32 环境帧并优先返回 ACK。后续将加入 Wi-Fi 状态机、MQTT 客户端、重连与上报逻辑。

## 5. STM32—ESP32 UART 协议

串口使用固定帧头、长度字段和 CRC-16/CCITT-FALSE，避免日志文本或线路噪声导致的粘包、错位和误解析。

```text
+--------+--------+---------+------+----------+-------------+---------+-------+
| SOF0   | SOF1   | Version | Type | Sequence | Payload Len | Payload | CRC16 |
+--------+--------+---------+------+----------+-------------+---------+-------+
| 0xA5   | 0x5A   | 1 byte  | 1 B  | 2 B, LE  | 2 B, LE     | 0~64 B  | 2 B   |
+--------+--------+---------+------+----------+-------------+---------+-------+
```

- 协议版本：`0x01`
- CRC 覆盖范围：`Version` 至 `Payload`，不包含帧头和 CRC 字段。
- CRC 参数：CRC-16/CCITT-FALSE，`poly=0x1021`、`init=0xFFFF`、不反射、`xorout=0x0000`。
- 多字节字段：显式采用小端序。
- 当前消息类型：`ENV_REPORT`、`PING`、`ACK`、`NET_STATUS`、`PONG`。
- `ENV_REPORT` 载荷固定为 40 字节，包含运行时间、采样序号、温湿度、照度、有效标志与错误计数。
- STM32 每秒发送一帧 `ENV_REPORT`；ESP32 成功解析后返回 `ACK`。STM32 发送前先登记期待的序号，收到 ACK 后校验其长度、消息类型、序号与结果码。

详细帧布局、ACK 机制、接线与实测调试记录见 [UART 协议与联调记录](docs/03-stm32-esp32-uart-protocol-debug.md)。

## 6. 构建与烧录

### STM32

1. 使用 STM32CubeIDE 导入或打开 `firmware/stm32-env-monitor`。
2. 选择 `Debug` 配置并执行构建。
3. 通过 ST-LINK 连接 `3.3V`、`GND`、`SWDIO(PA13)`、`SWCLK(PA14)`。
4. 在 CubeIDE 中调试/下载，或使用 STM32CubeProgrammer 烧录 `Debug/stm32-env-monitor.elf`。
5. 使用 CH340 USB-TTL 查看 USART2 日志：模块 `GND → GND`、`RXD → PA2`，终端设置为 115200 8-N-1、无流控。

### ESP32

1. 在 VS Code 中打开 `firmware/esp32-env-gateway`。
2. 确认 ESP-IDF 扩展使用已安装的 ESP-IDF 环境，目标选择 `esp32`。
3. 执行 `ESP-IDF: Build your project`，或在 IDF PowerShell 中运行：

   ```powershell
   idf.py build
   ```

ESP32 连接 USB 后应在设备管理器中识别为 CP2102 串口。选择对应 `COMx` 后执行：

```powershell
idf.py -p COMx flash monitor
```

本开发板实测 Flash 为 16 MB，ESP-IDF 的 `Serial flasher config > Flash size` 需设置为 `16 MB`。

## 7. 目录结构

```text
stm32-env-monitor/
├── firmware/
│   ├── stm32-env-monitor/       # STM32CubeIDE / CubeMX 工程
│   └── esp32-env-gateway/       # ESP-IDF 网关工程
├── docs/                        # 配置、路线与设计说明
└── README.md
```

构建产物、CubeIDE 工作区配置、ESP-IDF `build/` 目录及本机语言服务文件均由 `.gitignore` 排除。

## 8. 后续工作

1. 实现 ESP32 Wi-Fi/MQTT 连接、断线重连与网络状态回传。
2. 补充 UART 帧超时、重传和网络状态回传机制。
3. 将稳定的轮询流程拆分为 FreeRTOS 任务、队列与 I2C 互斥访问。

## 9. 相关文档

- [CubeIDE 基线配置](docs/01-cubeide-bringup.md)
- [开发路线](docs/02-roadmap.md)
- [STM32—ESP32 UART 协议与联调记录](docs/03-stm32-esp32-uart-protocol-debug.md)
