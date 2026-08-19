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

CubeIDE 工程生成后，`Core`、`Drivers` 和工程元数据应直接位于本目录并纳入 Git。当前可移植的软件层位于 `app`，后续可以复制到生成工程的 `Core/Inc` 和 `Core/Src`。

- `app/include`：与硬件无关的头文件
- `app/src`：数据校验、UART 协议、MQTT JSON 和故障统计实现
- `docs`：CubeIDE 配置、架构和实现路线
- `tools`：不依赖开发板的主机侧检查脚本

## 硬件到货前的检查

在项目根目录运行：

```powershell
python tools/protocol_selftest.py
```

该检查不需要 STM32 工具链或开发板，用于验证 STM32 与 ESP32 之间共用的 UART 帧格式、CRC 参数和字段偏移。

更多内容见：

- `docs/01-cubeide-bringup.md`
- `docs/02-roadmap.md`
- `docs/03-software-architecture.md`
- `docs/04-freertos-task-contract.md`
