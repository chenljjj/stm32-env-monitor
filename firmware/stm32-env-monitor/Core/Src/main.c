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
#include "cmsis_os.h"
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
#include "app_runtime.h"
#include "app_status.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
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
/* 保存等待 ACK 的完整帧，超时后以原序号重传。 */
typedef struct
{
  uint8_t pending_frame[APP_PROTOCOL_MAX_FRAME_LENGTH];
  uint16_t pending_frame_length;
  uint32_t last_tx_tick;
  uint32_t retry_count;
  uint32_t timeout_count;
  uint32_t drop_count;
  uint8_t retry_attempts;
  uint8_t pending;
} app_protocol_reliability_t;

static app_protocol_reliability_t app_protocol_reliability;
/* USART1 接收解析器与 ACK 统计。 */
static app_link_t app_link;
/* 采样任务向显示/链路任务复制的完整快照。 */
typedef struct
{
  app_monitor_t monitor;
  uint8_t display_ready;
  /* ESP32 通过 USART1 回传的 Wi-Fi/MQTT 状态，供 OLED 显示。 */
  uint8_t network_flags;
} app_runtime_snapshot_t;

static osMutexId_t app_i2c_mutex;
static osMessageQueueId_t app_display_queue;
static osMessageQueueId_t app_link_queue;
static osThreadId_t app_sensor_task_handle;
static osThreadId_t app_display_task_handle;
static osThreadId_t app_link_task_handle;
static osThreadId_t app_health_task_handle;
static volatile uint32_t app_display_queue_drop_count;
static volatile uint32_t app_link_queue_drop_count;
static volatile uint32_t app_display_queue_peak_count;
static volatile uint32_t app_link_queue_peak_count;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#define APP_PROTOCOL_UART_TIMEOUT_MS 20U
#define APP_PROTOCOL_ACK_TIMEOUT_MS 200U
#define APP_PROTOCOL_MAX_ACK_RETRIES 3U
#define APP_PROTOCOL_FLAG_CLIMATE_VALID 0x01U
#define APP_PROTOCOL_FLAG_ILLUMINANCE_VALID 0x02U
#define APP_PROTOCOL_FLAG_DISPLAY_READY 0x04U
#define APP_RTOS_SENSOR_PERIOD_TICKS 1000U
#define APP_RTOS_DISPLAY_WAIT_TICKS 250U
#define APP_RTOS_LINK_WAIT_TICKS 20U
#define APP_RTOS_HEALTH_PERIOD_TICKS 100U
#define APP_RTOS_SNAPSHOT_QUEUE_LENGTH 3U
#define APP_RTOS_RESOURCE_LOG_SAMPLE_PERIOD 60U

static uint32_t app_runtime_stack_free_bytes(osThreadId_t task_handle);

/* 将最新采样快照打包为 ENV_REPORT，并通过 USART1 发送给 ESP32。 */
static HAL_StatusTypeDef app_protocol_send_env_report(const app_monitor_t *monitor,
                                                      uint8_t display_ready,
                                                      app_link_t *link,
                                                      app_protocol_reliability_t *reliability)
{
  app_protocol_env_report_t report = {0};
  uint8_t payload[APP_PROTOCOL_ENV_REPORT_PAYLOAD_LENGTH];
  uint8_t frame[APP_PROTOCOL_MAX_FRAME_LENGTH];
  uint16_t frame_length = 0U;
  uint16_t sequence;
  HAL_StatusTypeDef status;

  if ((monitor == NULL) || (link == NULL) || (reliability == NULL))
  {
    return HAL_ERROR;
  }

  /* 上一帧尚未确认时只等待重传流程，不覆盖缓存帧。 */
  if (link->awaiting_ack != 0U)
  {
    return HAL_BUSY;
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
  if (display_ready != 0U)
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

  (void)memcpy(reliability->pending_frame, frame, frame_length);
  reliability->pending_frame_length = frame_length;
  reliability->retry_attempts = 0U;
  reliability->pending = 1U;

  /* ESP32 回 ACK 很快，必须在发送前登记本帧序号。 */
  app_link_arm_env_report_ack(link, sequence);
  status = HAL_UART_Transmit(&huart1,
                             frame,
                             frame_length,
                             APP_PROTOCOL_UART_TIMEOUT_MS);
  if (status == HAL_OK)
  {
    reliability->last_tx_tick = HAL_GetTick();
    ++app_protocol_tx_sequence;
  }
  else
  {
    app_link_cancel_env_report_ack(link);
    reliability->pending = 0U;
    reliability->pending_frame_length = 0U;
    ++app_protocol_tx_error_count;
  }

  return status;
}

/* 非阻塞检查 ACK 超时；每帧最多额外发送三次。 */
static void app_protocol_retry_pending_report(app_link_t *link,
                                              app_protocol_reliability_t *reliability)
{
  HAL_StatusTypeDef status;
  uint32_t now;

  if ((link == NULL) || (reliability == NULL) || (reliability->pending == 0U))
  {
    return;
  }

  /* ACK 中断已确认当前帧，释放本地缓存。 */
  if (link->awaiting_ack == 0U)
  {
    reliability->pending = 0U;
    reliability->pending_frame_length = 0U;
    return;
  }

  now = HAL_GetTick();
  if ((uint32_t)(now - reliability->last_tx_tick) < APP_PROTOCOL_ACK_TIMEOUT_MS)
  {
    return;
  }

  ++reliability->timeout_count;
  if (reliability->retry_attempts >= APP_PROTOCOL_MAX_ACK_RETRIES)
  {
    /* 重试耗尽后放弃旧帧，下一采样周期生成新帧继续工作。 */
    app_link_cancel_env_report_ack(link);
    reliability->pending = 0U;
    reliability->pending_frame_length = 0U;
    ++reliability->drop_count;
    return;
  }

  ++reliability->retry_attempts;
  ++reliability->retry_count;
  reliability->last_tx_tick = now;
  status = HAL_UART_Transmit(link->huart,
                             reliability->pending_frame,
                             reliability->pending_frame_length,
                             APP_PROTOCOL_UART_TIMEOUT_MS);
  app_protocol_tx_status = status;
  if (status != HAL_OK)
  {
    app_protocol_tx_status = status;
    ++app_protocol_tx_error_count;
  }
}

/* USART2 保持详细链路诊断，方便比较 RTOS 迁移前后的行为。 */
static void app_runtime_log_snapshot(const app_runtime_snapshot_t *snapshot)
{
  const app_monitor_t *monitor;
  int32_t temperature_fraction;

  if (snapshot == NULL)
  {
    return;
  }

  monitor = &snapshot->monitor;
  temperature_fraction = monitor->climate.temperature_milli_c % 1000;
  if (temperature_fraction < 0)
  {
    temperature_fraction = -temperature_fraction;
  }

  (void)app_log_printf(&huart2,
                       "sample=%lu aht=%s valid=%u t=%ld.%03ldC h=%lu.%03lu%% "
                       "bh=%s valid=%u l=%lu.%03lulx comm=%lu/%lu data=%lu/%lu "
                       "uart1=%s txerr=%lu ack=%lu rxerr=%lu ackerr=%lu exp=%u got=%u af=0x%02X "
                       "retry=%lu timeout=%lu drop=%lu pending=%u qdrop=%lu/%lu "
                       "net=%c%c ns=%lu nerr=%lu wd=%lu md=%lu nr=%u\r\n",
                       (unsigned long)monitor->sample_sequence,
                       app_monitor_status_name(monitor->aht20_status),
                       (unsigned int)monitor->climate_valid,
                       (long)(monitor->climate.temperature_milli_c / 1000),
                       (long)temperature_fraction,
                       (unsigned long)(monitor->climate.humidity_milli_rh / 1000U),
                       (unsigned long)(monitor->climate.humidity_milli_rh % 1000U),
                       app_monitor_status_name(monitor->bh1750_status),
                       (unsigned int)monitor->illuminance_valid,
                       (unsigned long)(monitor->illuminance_milli_lux / 1000U),
                       (unsigned long)(monitor->illuminance_milli_lux % 1000U),
                       (unsigned long)monitor->aht20_error_count,
                       (unsigned long)monitor->bh1750_error_count,
                       (unsigned long)monitor->aht20_data_error_count,
                       (unsigned long)monitor->bh1750_data_error_count,
                       app_monitor_status_name(app_protocol_tx_status),
                       (unsigned long)app_protocol_tx_error_count,
                       (unsigned long)app_link.ack_count,
                       (unsigned long)app_link.rx_error_count,
                       (unsigned long)app_link.ack_error_count,
                       (unsigned int)app_link.expected_ack_sequence,
                       (unsigned int)app_link.last_received_ack_sequence,
                       (unsigned int)app_link.last_ack_error_flags,
                       (unsigned long)app_protocol_reliability.retry_count,
                       (unsigned long)app_protocol_reliability.timeout_count,
                       (unsigned long)app_protocol_reliability.drop_count,
                       (unsigned int)app_protocol_reliability.pending,
                       (unsigned long)app_display_queue_drop_count,
                       (unsigned long)app_link_queue_drop_count,
                       (app_link.network_flags & APP_PROTOCOL_NET_FLAG_WIFI_CONNECTED) != 0U ? 'W' : '-',
                       (app_link.network_flags & APP_PROTOCOL_NET_FLAG_MQTT_CONNECTED) != 0U ? 'M' : '-',
                       (unsigned long)app_link.net_status_count,
                       (unsigned long)app_link.net_status_error_count,
                       (unsigned long)app_link.wifi_disconnect_count,
                       (unsigned long)app_link.mqtt_disconnect_count,
                       (unsigned int)app_link.last_network_reason);
}

/* 输出堆、栈和队列峰值，用于长稳测试而非功能判定。 */
static void app_runtime_log_resources(void)
{
  (void)app_log_printf(&huart2,
                       "rtos heap=%lu min=%lu stack=%lu/%lu/%lu/%lu "
                       "q=%lu/%lu peak=%lu/%lu qdrop=%lu/%lu\r\n",
                       (unsigned long)xPortGetFreeHeapSize(),
                       (unsigned long)xPortGetMinimumEverFreeHeapSize(),
                       (unsigned long)app_runtime_stack_free_bytes(app_sensor_task_handle),
                       (unsigned long)app_runtime_stack_free_bytes(app_display_task_handle),
                       (unsigned long)app_runtime_stack_free_bytes(app_link_task_handle),
                       (unsigned long)app_runtime_stack_free_bytes(app_health_task_handle),
                       (unsigned long)osMessageQueueGetCount(app_display_queue),
                       (unsigned long)osMessageQueueGetCount(app_link_queue),
                       (unsigned long)app_display_queue_peak_count,
                       (unsigned long)app_link_queue_peak_count,
                       (unsigned long)app_display_queue_drop_count,
                       (unsigned long)app_link_queue_drop_count);
}

/* FreeRTOS 返回的高水位以 StackType_t 个数为单位，转换为字节便于阅读。 */
static uint32_t app_runtime_stack_free_bytes(osThreadId_t task_handle)
{
  if (task_handle == NULL)
  {
    return 0U;
  }

  return (uint32_t)(uxTaskGetStackHighWaterMark((TaskHandle_t)task_handle) *
                    sizeof(StackType_t));
}

/* 采样任务是两条快照队列的唯一生产者，因此在此记录峰值。 */
static void app_runtime_update_queue_peak(osMessageQueueId_t queue,
                                          volatile uint32_t *peak_count)
{
  uint32_t current_count;

  if ((queue == NULL) || (peak_count == NULL))
  {
    return;
  }

  current_count = osMessageQueueGetCount(queue);
  if (current_count > *peak_count)
  {
    *peak_count = current_count;
  }
}

static void app_runtime_sensor_task(void *argument)
{
  app_runtime_snapshot_t snapshot;
  uint32_t next_tick = osKernelGetTickCount();

  (void)argument;
  for (;;)
  {
    if (osMutexAcquire(app_i2c_mutex, osWaitForever) == osOK)
    {
      if (app_monitor_update(&app_monitor, &hi2c1) != 0U)
      {
        snapshot.monitor = app_monitor;
        /* OLED 由显示任务独占，此标志允许链路任务使用最近已知状态。 */
        snapshot.display_ready = app_display.initialized;
        /* 8 位标志由 USART1 接收中断更新，读取是原子的，最多滞后一采样周期。 */
        snapshot.network_flags = app_link.network_flags;

        if (osMessageQueuePut(app_display_queue, &snapshot, 0U, 0U) != osOK)
        {
          ++app_display_queue_drop_count;
        }
        app_runtime_update_queue_peak(app_display_queue, &app_display_queue_peak_count);
        if (osMessageQueuePut(app_link_queue, &snapshot, 0U, 0U) != osOK)
        {
          ++app_link_queue_drop_count;
        }
        app_runtime_update_queue_peak(app_link_queue, &app_link_queue_peak_count);
      }
      (void)osMutexRelease(app_i2c_mutex);
    }

    next_tick += APP_RTOS_SENSOR_PERIOD_TICKS;
    (void)osDelayUntil(next_tick);
  }
}

static void app_runtime_display_task(void *argument)
{
  app_runtime_snapshot_t snapshot = {0};
  uint8_t has_snapshot = 0U;

  (void)argument;
  for (;;)
  {
    uint8_t data_changed = 0U;

    if (osMessageQueueGet(app_display_queue,
                          &snapshot,
                          NULL,
                          APP_RTOS_DISPLAY_WAIT_TICKS) == osOK)
    {
      has_snapshot = 1U;
      data_changed = 1U;
    }

    if ((has_snapshot != 0U) &&
        (osMutexAcquire(app_i2c_mutex, osWaitForever) == osOK))
    {
      app_display_update(&app_display,
                         &hi2c1,
                         &snapshot.monitor,
                         snapshot.network_flags,
                         data_changed);
      (void)osMutexRelease(app_i2c_mutex);
    }
  }
}

static void app_runtime_link_task(void *argument)
{
  app_runtime_snapshot_t snapshot;
  uint32_t last_resource_log_sample = 0U;

  (void)argument;
  for (;;)
  {
    app_protocol_retry_pending_report(&app_link, &app_protocol_reliability);

    if (osMessageQueueGet(app_link_queue,
                          &snapshot,
                          NULL,
                          APP_RTOS_LINK_WAIT_TICKS) == osOK)
    {
      app_protocol_tx_status = app_protocol_send_env_report(&snapshot.monitor,
                                                             snapshot.display_ready,
                                                             &app_link,
                                                             &app_protocol_reliability);
      app_runtime_log_snapshot(&snapshot);

      if ((last_resource_log_sample == 0U) ||
          ((snapshot.monitor.sample_sequence - last_resource_log_sample) >=
           APP_RTOS_RESOURCE_LOG_SAMPLE_PERIOD))
      {
        last_resource_log_sample = snapshot.monitor.sample_sequence;
        app_runtime_log_resources();
      }
    }
  }
}

static void app_runtime_health_task(void *argument)
{
  (void)argument;
  for (;;)
  {
    app_status_update(&app_status, LED_PC13_GPIO_Port, LED_PC13_Pin);
    (void)osDelay(APP_RTOS_HEALTH_PERIOD_TICKS);
  }
}

void app_runtime_init(void)
{
  /* 日志失败仅改变 LED 状态，不阻止后续任务创建。 */
  app_status_init(&app_status,
                  app_log_write(&huart2, "stm32-env-monitor boot\r\n"));
  app_monitor_init(&app_monitor);
  app_display_init(&app_display);
  app_protocol_tx_status = HAL_BUSY;

  /* ACK 字节只做协议解析，不调用 RTOS API，可保持高于内核的优先级。 */
  app_link_init(&app_link, &huart1);
  HAL_NVIC_SetPriority(USART1_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
}

void app_runtime_start(void)
{
  static const osThreadAttr_t sensor_attributes = {
    .name = "SensorTask",
    .stack_size = 384U * 4U,
    .priority = (osPriority_t)osPriorityAboveNormal,
  };
  static const osThreadAttr_t display_attributes = {
    .name = "DisplayTask",
    .stack_size = 384U * 4U,
    .priority = (osPriority_t)osPriorityBelowNormal,
  };
  static const osThreadAttr_t link_attributes = {
    .name = "LinkTask",
    .stack_size = 512U * 4U,
    .priority = (osPriority_t)osPriorityAboveNormal,
  };
  static const osThreadAttr_t health_attributes = {
    .name = "HealthTask",
    .stack_size = 192U * 4U,
    .priority = (osPriority_t)osPriorityLow,
  };

  app_i2c_mutex = osMutexNew(NULL);
  app_display_queue = osMessageQueueNew(APP_RTOS_SNAPSHOT_QUEUE_LENGTH,
                                        sizeof(app_runtime_snapshot_t),
                                        NULL);
  app_link_queue = osMessageQueueNew(APP_RTOS_SNAPSHOT_QUEUE_LENGTH,
                                     sizeof(app_runtime_snapshot_t),
                                     NULL);

  app_sensor_task_handle = osThreadNew(app_runtime_sensor_task, NULL, &sensor_attributes);
  app_display_task_handle = osThreadNew(app_runtime_display_task, NULL, &display_attributes);
  app_health_task_handle = osThreadNew(app_runtime_health_task, NULL, &health_attributes);
  /* 链路任务最后创建，首次输出资源日志时四个任务句柄均已有效。 */
  app_link_task_handle = osThreadNew(app_runtime_link_task, NULL, &link_attributes);

  if ((app_i2c_mutex == NULL) || (app_display_queue == NULL) ||
      (app_link_queue == NULL) || (app_sensor_task_handle == NULL) ||
      (app_display_task_handle == NULL) || (app_link_task_handle == NULL) ||
      (app_health_task_handle == NULL))
  {
    /* 堆不足时保留 defaultTask，使 LED 常亮并避免继续运行半初始化系统。 */
    app_status.startup_status = HAL_ERROR;
    (void)app_log_write(&huart2, "FreeRTOS resource creation failed\r\n");
    for (;;)
    {
      app_status_update(&app_status, LED_PC13_GPIO_Port, LED_PC13_Pin);
      (void)osDelay(APP_RTOS_HEALTH_PERIOD_TICKS);
    }
  }
}

/* 堆耗尽或任务栈越界后不再继续执行未知状态的业务逻辑。 */
void vApplicationMallocFailedHook(void)
{
  taskDISABLE_INTERRUPTS();
  HAL_GPIO_WritePin(LED_PC13_GPIO_Port, LED_PC13_Pin, GPIO_PIN_RESET);
  for (;;)
  {
    __NOP();
  }
}

void vApplicationStackOverflowHook(TaskHandle_t task_handle, char *task_name)
{
  (void)task_handle;
  (void)task_name;
  taskDISABLE_INTERRUPTS();
  HAL_GPIO_WritePin(LED_PC13_GPIO_Port, LED_PC13_Pin, GPIO_PIN_RESET);
  for (;;)
  {
    __NOP();
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
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
  app_runtime_init();
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 调度器启动后不应执行到此处。 */
    __WFI();
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
