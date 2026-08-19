# FreeRTOS 任务契约

直接 HAL 基线编译通过后再启用 FreeRTOS。使用一个队列传递完整测量值，另一个队列缓存待发送的 Modem 帧。

| 任务 | 周期/触发方式 | 优先级 | 职责 |
| --- | --- | ---: | --- |
| `SensorTask` | 每 2 秒 | 3 | 读取 AHT20 和 BH1750，校验数据，更新故障计数，发送一个完整样本 |
| `DisplayTask` | 收到队列消息，最长等待 2 秒 | 2 | 在 SSD1306 显示最新有效样本或故障提示 |
| `ModemTask` | 收到消息或 UART RX 通知 | 3 | 编码样本、发送帧、解析 ESP32 应答和网络状态 |
| `HealthTask` | 每 1 秒 | 1 | 汇总故障计数，并把降级状态提供给显示和日志任务 |
| `LogTask` | 收到日志消息 | 1 | 将有界长度的日志输出到 USART1，不阻塞采集和 Modem 任务 |

建议测量队列长度为 2，Modem 队列长度为 4。测量队列满时说明显示已经滞后，应丢弃最旧样本并增加队列丢弃计数，而不是阻塞采集任务。Modem 队列满时说明网络发生背压，只保留最新测量值并报告降级状态。

## 故障行为

- AHT20/BH1750 超时：增加 `sensor_read_errors`，保留上一次样本，并将下一次样本标记为 `sensor_error`
- 数据越界：增加 `sensor_range_errors`，不将该样本放入队列
- UART CRC 错误：增加 `uart_crc_errors`，丢弃当前帧，等待下一个起始序列
- Wi-Fi/MQTT 断开：ESP32 按 1、2、4、8、16、30 秒有限退避重连，增加 `network_reconnects`，并向 STM32 报告状态
- MQTT 发布失败：增加 `mqtt_publish_errors`，STM32 任务不能忙等重试

任务之间应复制完整的 `env_measurement_t`，避免在没有互斥保护的情况下共享可变全局测量结构。

