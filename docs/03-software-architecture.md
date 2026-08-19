# 硬件到货前的软件架构

`app` 目录中的可移植代码刻意不依赖 STM32 HAL 和 FreeRTOS。CubeIDE 创建 MCU 工程后，可以把这些文件复制到 `Core/Inc` 和 `Core/Src`，再逐步接入硬件适配层。

## 数据流

`AHT20/BH1750 -> 采集任务 -> 测量消息队列 -> 显示任务`

`测量消息队列 -> UART 协议任务 -> ESP32 -> MQTT`

所有测量值使用定点单位，避免在 MCU 高频路径中依赖浮点格式化：

- `temperature_milli_c`：摄氏度乘以 1000，带符号
- `humidity_milli_rh`：相对湿度乘以 1000
- `illuminance_milli_lux`：照度乘以 1000

`env_measurement_validate` 会在样本进入队列或发送 UART 前拒绝不合理值。当前范围为 -40 到 85 摄氏度、0 到 100 %RH、最高 200000 lux。

## UART 帧

STM32 到 ESP32 的测量帧采用小端字节序：

| 偏移 | 长度 | 字段 |
| ---: | ---: | --- |
| 0 | 1 | 起始字节 `0xAA` |
| 1 | 1 | 起始字节 `0x55` |
| 2 | 1 | 协议版本 `0x01` |
| 3 | 1 | 消息类型 `0x01`（测量值） |
| 4 | 2 | Payload 长度（`22`） |
| 6 | 4 | 温度，单位 milli-Celsius，带符号 |
| 10 | 4 | 湿度，单位 milli-%RH |
| 14 | 4 | 光照，单位 milli-lux |
| 18 | 2 | 状态枚举 |
| 20 | 4 | 序号 |
| 24 | 4 | 毫秒时间戳 |
| 28 | 2 | CRC-16/CCITT-FALSE，计算偏移 2 到 27 |

完整帧长度为 30 字节。解析器按字节增量处理，可以接入 `HAL_UART_Receive_IT` 或 DMA 环形缓冲区，并在串口噪声后重新寻找起始序列。

## 接入顺序

1. 将 `app/include` 和 `app/src` 复制到 CubeIDE 工程。
2. 使用生成的 HAL 工程编译这些可移植模块。
3. 加入轮询 UART 日志和 PC13 心跳。
4. 加入 I2C 传感器和 OLED 适配层。
5. 启用 FreeRTOS，将适配层放到任务和消息队列后面。
6. ESP32 按上面的帧表实现，再用 MQTTX 联调。

