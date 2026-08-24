# 实现路线图

## 阶段 1：HAL 基线

- 编译生成的 STM32F103 工程
- 控制 PC13 闪烁并输出 UART 启动/心跳日志
- 封装一个基于 `HAL_UART_Transmit` 的简单日志函数

## 阶段 2：本地采集和显示

- 配置 I2C1，接入 AHT20、BH1750 和 SSD1306
- 编写独立的 AHT20、BH1750 驱动，加入超时和数据范围检查
- 在 OLED 上显示温度、湿度和光照值

当前状态：已完成硬件实测。三个 I2C 设备共用 PB6/PB7，总线速率为 400 kHz；AHT20、BH1750 读数有效，SSD1306 可显示实时数据。

## 阶段 3：FreeRTOS 架构

- 直接 HAL 版本稳定后再加入 FreeRTOS
- 拆分传感器、显示、UART/Modem、日志和健康监测任务
- 通过消息队列传递完整采样值，避免任务直接共享可变数据

## 阶段 4：ESP32 与 MQTT

- 定义带版本号的 STM32-ESP32 UART 帧或命令协议
- 两端都校验帧长度、校验和/CRC 以及字段范围
- 实现 ESP32 Wi-Fi 配网、MQTT 发布、断线检测和有限退避重连
- 到这一阶段再使用 MQTTX 检查主题和消息内容

当前状态：UART 协议与设备端网络链路均已硬件实测。STM32 每秒上报 `ENV_REPORT`，ESP32 使用 UART2（GPIO25/GPIO26）解析后返回 ACK；ESP32 已连接 Wi-Fi、以 TLS 8883 连接 EMQX，并由 MQTTX 持续订阅到环境 JSON。已完成 Wi-Fi 断开、自动重连、MQTT 会话恢复与 32 条 FreeRTOS 遥测队列补发验证。LWT 异常断电、错误凭据和长时稳定性仍待单独测试。

## 阶段 5：可靠性和项目材料

- 统计传感器、UART、队列和网络故障次数
- 为每类故障定义恢复动作，并在本地显示健康状态
- 记录通信协议、任务调度、测试用例和实际故障现象

当前状态：ESP32 已统计队列入队、溢出、发布接受、发布错误、放弃和 Broker PUBACK。`NET_STATUS` 已实现并硬件实测：状态变化时立即回传、每 10 个样本发送快照；STM32 可显示 `--`、`W-`、`WM` 以及断线计数。一次短时断网测试中，ESP32 队列峰值为 14、`overflow=0`，恢复后 `queue=0` 且所有已入队样本被接受发布。STM32 的 200 ms ACK 超时、最多 3 次同序号重传与 ESP32 相邻重复帧去重已实现；本轮网络断网测试中未触发 UART 重传。

## 建议的测量消息

MQTT 阶段使用明确的数据结构：

```json
{
  "device_id": "stm32-env-gateway-001",
  "sample": 12,
  "uptime_ms": 12083,
  "temperature_c": 23.777,
  "humidity_rh": 43.892,
  "illuminance_lux": 48.333,
  "valid": {
    "climate": true,
    "illuminance": true,
    "display": true
  },
  "errors": {
    "aht20_comm": 0,
    "bh1750_comm": 0,
    "aht20_data": 0,
    "bh1750_data": 0
  }
}
```

当前遥测主题为 `stm32-env-monitor/telemetry`，状态主题为 `stm32-env-monitor/status`。目前使用 STM32 `uptime_ms` 和采样序号，不伪造 Unix 时间；后续可在 ESP32 完成 SNTP 同步后增加绝对时间戳。
