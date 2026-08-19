# stm32-env-monitor

基于 STM32 的嵌入式环境监测联网终端。

目标硬件：STM32F103C8T6 Blue Pill、AHT20、BH1750、SSD1306 OLED，以及负责 Wi-Fi/MQTT 的 ESP32 DevKit V1。

## 第一阶段基线

第一阶段只建立可编译、可验证的最小系统：

- STM32F103C8T6 CubeIDE 工程能够成功编译
- PC13 控制 Blue Pill 板载 LED（低电平点亮）
- USART1 使用 PA9/PA10，配置为 115200 8-N-1
- 定期输出启动日志和心跳日志

FreeRTOS、传感器、OLED 和 ESP32 联调在这个基线通过后逐步加入。

## 目录说明

CubeMX 生成的 STM32CubeIDE 工程位于 `firmware/stm32-env-monitor`，其中包含 `Core`、`Drivers`、`.ioc` 和链接脚本，均应纳入 Git。

- `docs`：项目配置和开发过程文档

更多内容见：

- `docs/01-cubeide-bringup.md`
- `docs/02-roadmap.md`
