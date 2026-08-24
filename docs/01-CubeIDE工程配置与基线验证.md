# CubeIDE 工程创建、配置与基线验证

## 创建工程

1. 启动 STM32CubeIDE，选择工作区：`D:\workspace\codex_workspace\embedded\workspace`。
2. 选择 **File > New > STM32 Project**。
3. 搜索并选择 `STM32F103C8Tx`，封装选择 `LQFP48`。
4. 工程名填写 `stm32-env-monitor`，工程位置填写：`D:\workspace\codex_workspace\embedded\projects\stm32-env-monitor`。
5. 选择 HAL 驱动和 C 语言，保留默认的 CubeMX 初始化文件。

不要选择 Blue Pill 板卡模板，直接选择 MCU，便于明确管理引脚配置。

## 当前 .ioc 与运行时配置

| 区域 | 配置 |
| --- | --- |
| SYS | Debug 选择 Serial Wire |
| RCC | HSE 选择 Crystal/Ceramic Resonator，Blue Pill 外部晶振为 8 MHz |
| Clock Configuration | SYSCLK 72 MHz，APB1 36 MHz，APB2 72 MHz |
| GPIOC Pin 13 | GPIO_Output，默认高电平，推挽输出 |
| I2C1 | PB6 为 SCL、PB7 为 SDA；400 kHz；连接 AHT20、BH1750、SSD1306 |
| USART1 | Asynchronous；PA9 为 TX，PA10 为 RX；115200、8-N-1、无流控；用于 STM32—ESP32 二进制协议 |
| USART2 | Asynchronous；PA2 为 TX，PA3 为 RX；115200、8-N-1、无流控；用于文本调试日志 |
| NVIC | USART1 使用单字节接收中断处理 ESP32 ACK；当前由 `main.c` 在运行时启用。若重新配置 CubeMX，应同步勾选 USART1 全局中断。 |
| FreeRTOS | CMSIS-RTOS v2 封装；`heap_4`；`configTOTAL_HEAP_SIZE=10240` 字节 |
| Project Manager | 工具链选择 STM32CubeIDE；启用成对的 `.c/.h` 外设初始化文件 |

PC13 连接的 Blue Pill 板载 LED 为低电平点亮：写入 `GPIO_PIN_RESET` 点亮，写入 `GPIO_PIN_SET` 熄灭。

## 当前 STM32 运行时结构

工程已从早期 HAL 轮询循环迁移到 FreeRTOS。`main()` 只完成 HAL、外设和运行时对象初始化，随后启动调度器；默认任务创建应用任务后退出。

| 任务 | 优先级 | 职责 |
| --- | --- | --- |
| `SensorTask` | AboveNormal | 每秒采集 AHT20、BH1750，生成完整环境快照并投递给显示和通信队列。 |
| `DisplayTask` | BelowNormal | 接收最新快照并刷新 SSD1306。 |
| `LinkTask` | AboveNormal | 处理 USART1 上报、ACK、重传和网络状态回传。 |
| `HealthTask` | Low | 更新 PC13 状态指示。 |

I2C1 被 AHT20、BH1750 与 SSD1306 共用，因此所有 I2C 访问通过互斥锁串行化。采集任务不直接操作 OLED 或网络链路，而是向长度为 3 的消息队列投递快照；队列满时计数并丢弃旧周期无法消费的快照，避免任务长期阻塞。

## 构建与下载验证

CubeIDE Debug 构建已通过，且已完成实板下载验证。当前 FreeRTOS 版本的典型镜像占用约为：

| 资源 | 占用 |
| --- | --- |
| Flash `.text + .data` | 约 41.4 KiB / 64 KiB |
| 静态 SRAM `.data + .bss` | 约 17.3 KiB / 20 KiB |

其中 10 KiB FreeRTOS 堆位于静态 SRAM；任务栈、队列和互斥锁对象从该堆动态分配。后续新增任务或大缓冲区前，应重新检查链接结果，并在实机上检查任务栈高水位与剩余堆空间。

## 当前端到端验证状态（2026-08-24）

当前 STM32 固件已使用 FreeRTOS 任务架构，且硬件联调已完成：AHT20、BH1750 与 SSD1306 通过 I2C1 正常工作；USART1 与 ESP32 的二进制协议持续 ACK 成功；STM32 可接收 ESP32 的 `NET_STATUS`，在日志中显示 `--`、`W-`、`WM` 三种网络状态。

已完成一次 Wi-Fi 断开与恢复测试：本地采样和 UART ACK 在网络离线期间保持运行，ESP32 重新获得 IP 并恢复 MQTT/TLS 会话后，STM32 状态由 `--` 依次恢复为 `W-`、`WM`。FreeRTOS 迁移后再次完成采样、显示、UART ACK 与 MQTT 上云回归。

## 生成和编译

1. 保存 `.ioc` 文件，在 CubeIDE 提示时选择 **Generate Code**。
2. 选择 **Project > Build Project**。
3. 预期结果为 `Build Finished` 且错误数为 0。
4. 确认生成了 `Core/Src/main.c`、`Core/Src/gpio.c`、`Core/Src/i2c.c`、`Core/Src/usart.c`，以及 `STM32F103C8Tx_FLASH.ld`。

首次生成工程并确认可以编译后，再执行下面的 Git 初始化。不要提交编译产物和本机 Eclipse 设置。

## CubeIDE 工程生成后的 Git 初始化

在工程根目录的 PowerShell 中执行：

```powershell
git init
git branch -M main
@'
Debug/
Release/
*.elf
*.hex
*.bin
*.map
*.list
*.d
*.su
*.o
.settings/
RemoteSystemsTempFiles/
'@ | Set-Content -NoNewline .gitignore
git add .
git commit -m "chore: initialize CubeIDE STM32F103 baseline"
```

应纳入 Git 的内容包括 `.project`、`.cproject`、`.ioc`、链接脚本、启动文件、HAL 文件和源代码。`.settings` 保存本机 Eclipse 工作区偏好，因此忽略它。

## 当前硬件验证与烧录

1. 使用 ST-LINK 连接 Blue Pill：`3.3V`、`GND`、`SWDIO(PA13)`、`SWCLK(PA14)`。
2. 在 CubeIDE 使用 `Run As > STM32 C/C++ Application` 下载，或在 STM32CubeProgrammer 中选择 `Debug/stm32-env-monitor.elf`。
3. 使用 CH340 USB-TTL 查看 USART2 日志：`GND → GND`、`RXD → PA2`，设置为 115200、8-N-1、无流控。
4. 已完成验证：PC13 状态指示、AHT20、BH1750、SSD1306 和断电重启均可正常工作。

STM32—ESP32 的接线、协议与联调记录见 [UART 协议与联调记录](03-STM32与ESP32串口协议与调试.md)。
