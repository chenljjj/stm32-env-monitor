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
| Project Manager | 工具链选择 STM32CubeIDE；启用成对的 `.c/.h` 外设初始化文件 |

PC13 连接的 Blue Pill 板载 LED 为低电平点亮：写入 `GPIO_PIN_RESET` 点亮，写入 `GPIO_PIN_SET` 熄灭。

当前未启用 FreeRTOS。先保持直接 HAL 轮询和中断版本稳定，再将采样、显示、串口和网络工作拆分为任务。

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

STM32—ESP32 的接线、协议与联调记录见 [UART 协议与联调记录](03-stm32-esp32-uart-protocol-debug.md)。
