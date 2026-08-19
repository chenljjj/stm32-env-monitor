# CubeIDE 首次建工程与点灯串口基线

## 创建工程

1. 启动 STM32CubeIDE，选择工作区：`D:\workspace\codex_workspace\embedded\workspace`。
2. 选择 **File > New > STM32 Project**。
3. 搜索并选择 `STM32F103C8Tx`，封装选择 `LQFP48`。
4. 工程名填写 `stm32-env-monitor`，工程位置填写：`D:\workspace\codex_workspace\embedded\projects\stm32-env-monitor`。
5. 选择 HAL 驱动和 C 语言，保留默认的 CubeMX 初始化文件。

不要选择 Blue Pill 板卡模板，直接选择 MCU，便于明确管理引脚配置。

## 初始 .ioc 配置

| 区域 | 配置 |
| --- | --- |
| SYS | Debug 选择 Serial Wire |
| RCC | HSE 选择 Crystal/Ceramic Resonator，Blue Pill 外部晶振为 8 MHz |
| Clock Configuration | SYSCLK 72 MHz，APB1 36 MHz，APB2 72 MHz |
| GPIOC Pin 13 | GPIO_Output，默认高电平，推挽输出 |
| USART1 | Asynchronous；PA9 为 TX，PA10 为 RX；115200；8 数据位；无校验；1 停止位；无流控 |
| NVIC | 第一阶段采用轮询日志，暂不打开 USART1 全局中断 |
| Project Manager | 工具链选择 STM32CubeIDE；启用成对的 `.c/.h` 外设初始化文件 |

PC13 连接的 Blue Pill 板载 LED 为低电平点亮：写入 `GPIO_PIN_RESET` 点亮，写入 `GPIO_PIN_SET` 熄灭。

第一阶段不要启用 FreeRTOS，先完成直接 HAL 工程的编译与检查。

## 生成和编译

1. 保存 `.ioc` 文件，在 CubeIDE 提示时选择 **Generate Code**。
2. 选择 **Project > Build Project**。
3. 预期结果为 `Build Finished` 且错误数为 0。
4. 确认生成了 `Core/Src/main.c`、`Core/Src/gpio.c`、`Core/Src/usart.c`，以及 `STM32F103C8Tx_FLASH.ld`。

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

## 生成后的第一次源码修改

先确认生成工程可以原样编译，再加入 PC13 闪烁和 `HAL_UART_Transmit` 启动消息，并重新编译。开发板和 USB-UART 转换器到货后，再进行烧录和串口终端验证。

