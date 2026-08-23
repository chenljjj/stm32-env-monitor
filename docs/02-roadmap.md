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

当前状态：UART 协议联调已完成。STM32 每秒上报 `ENV_REPORT`，ESP32 使用 UART2（GPIO25/GPIO26）解析后返回 ACK；后续工作从 Wi-Fi 与 MQTT 开始。

## 阶段 5：可靠性和项目材料

- 统计传感器、UART、队列和网络故障次数
- 为每类故障定义恢复动作，并在本地显示健康状态
- 记录通信协议、任务调度、测试用例和实际故障现象

## 建议的测量消息

MQTT 阶段使用明确的数据结构：

```json
{
  "device_id": "env-monitor-001",
  "ts_ms": 0,
  "temperature_c": 0.0,
  "humidity_rh": 0.0,
  "illuminance_lux": 0.0,
  "status": "ok"
}
```

时间戳来源和最终主题命名在 ESP32 网络部分实现时确定。
