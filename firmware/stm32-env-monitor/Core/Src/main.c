/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_log.h"
#include "app_link.h"
#include "app_display.h"
#include "app_monitor.h"
#include "app_protocol.h"
#include "app_status.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* 集中保存启动结果与 LED 心跳状态。 */
static app_status_t app_status;
/* 保存最新传感器快照及各设备的错误统计。 */
static app_monitor_t app_monitor;
/* OLED 页面状态与 128x64 显示缓存。 */
static app_display_t app_display;
/* USART1 二进制协议的发送序号和最近状态。 */
static uint16_t app_protocol_tx_sequence;
static HAL_StatusTypeDef app_protocol_tx_status;
static uint32_t app_protocol_tx_error_count;
/* USART1 接收解析器与 ACK 统计。 */
static app_link_t app_link;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#define APP_PROTOCOL_UART_TIMEOUT_MS 20U
#define APP_PROTOCOL_FLAG_CLIMATE_VALID 0x01U
#define APP_PROTOCOL_FLAG_ILLUMINANCE_VALID 0x02U
#define APP_PROTOCOL_FLAG_DISPLAY_READY 0x04U

/* 将最新采样快照打包为 ENV_REPORT，并通过 USART1 发送给 ESP32。 */
static HAL_StatusTypeDef app_protocol_send_env_report(const app_monitor_t *monitor,
                                                      const app_display_t *display,
                                                      app_link_t *link)
{
  app_protocol_env_report_t report = {0};
  uint8_t payload[APP_PROTOCOL_ENV_REPORT_PAYLOAD_LENGTH];
  uint8_t frame[APP_PROTOCOL_MAX_FRAME_LENGTH];
  uint16_t frame_length = 0U;
  uint16_t sequence;
  HAL_StatusTypeDef status;

  if ((monitor == NULL) || (display == NULL) || (link == NULL))
  {
    return HAL_ERROR;
  }

  report.uptime_ms = HAL_GetTick();
  report.sample_sequence = monitor->sample_sequence;
  report.temperature_milli_c = monitor->climate.temperature_milli_c;
  report.humidity_milli_rh = monitor->climate.humidity_milli_rh;
  report.illuminance_milli_lux = monitor->illuminance_milli_lux;
  report.aht20_status = (uint8_t)monitor->aht20_status;
  report.bh1750_status = (uint8_t)monitor->bh1750_status;
  report.aht20_comm_errors = monitor->aht20_error_count;
  report.bh1750_comm_errors = monitor->bh1750_error_count;
  report.aht20_data_errors = monitor->aht20_data_error_count;
  report.bh1750_data_errors = monitor->bh1750_data_error_count;

  if (monitor->climate_valid != 0U)
  {
    report.valid_flags |= APP_PROTOCOL_FLAG_CLIMATE_VALID;
  }
  if (monitor->illuminance_valid != 0U)
  {
    report.valid_flags |= APP_PROTOCOL_FLAG_ILLUMINANCE_VALID;
  }
  if (display->initialized != 0U)
  {
    report.valid_flags |= APP_PROTOCOL_FLAG_DISPLAY_READY;
  }

  app_protocol_pack_env_report(&report, payload);
  sequence = app_protocol_tx_sequence;
  status = app_protocol_encode(APP_PROTOCOL_TYPE_ENV_REPORT,
                               sequence,
                               payload,
                               sizeof(payload),
                               frame,
                               sizeof(frame),
                               &frame_length);
  if (status != HAL_OK)
  {
    ++app_protocol_tx_error_count;
    return status;
  }

  /* ESP32 回 ACK 很快，必须在发送前登记本帧序号。 */
  app_link_arm_env_report_ack(link, sequence);
  status = HAL_UART_Transmit(&huart1,
                             frame,
                             frame_length,
                             APP_PROTOCOL_UART_TIMEOUT_MS);
  if (status == HAL_OK)
  {
    ++app_protocol_tx_sequence;
  }
  else
  {
    app_link_cancel_env_report_ack(link);
    ++app_protocol_tx_error_count;
  }

  return status;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  uint8_t sample_updated;

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /* 日志失败仅改变状态指示，不阻止主程序继续运行。 */
  app_status_init(&app_status,
                  app_log_write(&huart2, "stm32-env-monitor boot\r\n"));
  app_monitor_init(&app_monitor);
  app_display_init(&app_display);
  app_protocol_tx_status = HAL_BUSY;
  /* USART1 的 ACK 接收使用中断，优先级高于 SysTick。 */
  HAL_NVIC_SetPriority(USART1_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
  app_link_init(&app_link, &huart1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 非阻塞地更新 LED 心跳与故障指示。 */
    app_status_update(&app_status, LED_PC13_GPIO_Port, LED_PC13_Pin);

    sample_updated = app_monitor_update(&app_monitor, &hi2c1);
    if (sample_updated != 0U)
    {
      int32_t temperature_fraction = app_monitor.climate.temperature_milli_c % 1000;

      app_protocol_tx_status = app_protocol_send_env_report(&app_monitor,
                                                             &app_display,
                                                             &app_link);

      if (temperature_fraction < 0)
      {
        temperature_fraction = -temperature_fraction;
      }

      /* 同时输出有效标志和错误次数，便于未接硬件时定位问题。 */
      (void)app_log_printf(&huart2,
                           "sample=%lu aht=%s valid=%u t=%ld.%03ldC h=%lu.%03lu%% "
                           "bh=%s valid=%u l=%lu.%03lulx comm=%lu/%lu data=%lu/%lu "
                           "uart1=%s txerr=%lu ack=%lu rxerr=%lu ackerr=%lu exp=%u got=%u af=0x%02X\r\n",
                           (unsigned long)app_monitor.sample_sequence,
                           app_monitor_status_name(app_monitor.aht20_status),
                           (unsigned int)app_monitor.climate_valid,
                           (long)(app_monitor.climate.temperature_milli_c / 1000),
                           (long)temperature_fraction,
                           (unsigned long)(app_monitor.climate.humidity_milli_rh / 1000U),
                           (unsigned long)(app_monitor.climate.humidity_milli_rh % 1000U),
                           app_monitor_status_name(app_monitor.bh1750_status),
                           (unsigned int)app_monitor.illuminance_valid,
                           (unsigned long)(app_monitor.illuminance_milli_lux / 1000U),
                           (unsigned long)(app_monitor.illuminance_milli_lux % 1000U),
                           (unsigned long)app_monitor.aht20_error_count,
                           (unsigned long)app_monitor.bh1750_error_count,
                           (unsigned long)app_monitor.aht20_data_error_count,
                           (unsigned long)app_monitor.bh1750_data_error_count,
                           app_monitor_status_name(app_protocol_tx_status),
                           (unsigned long)app_protocol_tx_error_count,
                           (unsigned long)app_link.ack_count,
                           (unsigned long)app_link.rx_error_count,
                           (unsigned long)app_link.ack_error_count,
                           (unsigned int)app_link.expected_ack_sequence,
                           (unsigned int)app_link.last_received_ack_sequence,
                           (unsigned int)app_link.last_ack_error_flags);
    }

    app_display_update(&app_display,
                       &hi2c1,
                       &app_monitor,
                       sample_updated);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USART1 每接收一个字节进入一次中断，协议解析器随后立即重新挂起接收。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    app_link_on_uart_rx_complete(&app_link, huart);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    app_link_on_uart_error(&app_link, huart);
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
