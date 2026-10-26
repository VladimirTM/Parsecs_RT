/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <demo_task.h>
#include <FreeRTOS_Helpers.h>
#include "PARSECS_LowLevelTask.h"
#include "PARSECS_HighLevelTask.h"
#include "log.h"
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
/* USER CODE BEGIN Variables */
/* Drains the LOG_SERIAL ring to USB CDC so logging never blocks myTestTask. */
osThreadId_t logTaskHandle;
const osThreadAttr_t logTask_attributes = {
  .name = "logTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Logs "heartbeat <n>" once per second. */
osThreadId_t heartbeatTaskHandle;
const osThreadAttr_t heartbeatTask_attributes = {
  .name = "heartbeatTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for myTestTask */
osThreadId_t myTestTaskHandle;
const osThreadAttr_t myTestTask_attributes = {
  .name = "myTestTask",
  .stack_size = 300 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void HeartbeatTaskStart(void *argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void myTestTaskStart(void *argument);

extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 4 */
/* Called by the kernel when a task overflows its stack. The system can no
 * longer be trusted, so stop here and flash LD3-LD6 fast. SysTick is off with
 * interrupts disabled, so the delay is a busy loop. pcTaskName names the task
 * in the debugger. */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;
  __disable_irq();
  for (;;)
  {
    HAL_GPIO_TogglePin(GPIOD, LD3_Pin | LD4_Pin | LD5_Pin | LD6_Pin);
    for (volatile uint32_t i = 0U; i < 200000U; i++)
    {
    }
  }
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
   /* vApplicationMallocFailedHook() will only be called if
   configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
   function that will get called if a call to pvPortMalloc() fails.
   pvPortMalloc() is called internally by the kernel whenever a task, queue,
   timer or semaphore is created. It is also called by various parts of the
   demo application. If heap_1.c or heap_2.c are used, then the size of the
   heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
   FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
   to query the size of free heap space that remains (although it does not
   provide information on how the remaining heap might be fragmented). */
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of myTestTask */
  myTestTaskHandle = osThreadNew(myTestTaskStart, NULL, &myTestTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  logTaskHandle = osThreadNew(LOG_TaskStart, NULL, &logTask_attributes);
  heartbeatTaskHandle = osThreadNew(HeartbeatTaskStart, NULL, &heartbeatTask_attributes);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_myTestTaskStart */
/**
* @brief Function implementing the myTestTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_myTestTaskStart */
void myTestTaskStart(void *argument)
{
  /* USER CODE BEGIN myTestTaskStart */
  /* LowLevelTask pumps L3/L2/L1; High Level job then USER_Send/Receive demo. */
  for(;;)
  {
    PARSECS_LowLevelTask();
    PARSECS_Protocol_Interface_Task();
    MyDemoTask();
    osDelay(1);
  }
  /* USER CODE END myTestTaskStart */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/* After 5 s myTestTask has run several round trips, so the stack report
 * reflects its deepest path (worst case with I2C_PROTOCOL_DEBUG on).
 * Values are free stack in words (4 bytes). */
static void HeartbeatTaskStart(void *argument)
{
  uint32_t count = 0U;
  (void)argument;
  for (;;)
  {
    osDelay(1000);
    count++;
    LOG_SERIAL("heartbeat %lu\n", (unsigned long)count);
    if (count == 5U)
    {
      LOG_SERIAL("stack free words: myTestTask=%lu logTask=%lu heartbeat=%lu\n",
                 (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)myTestTaskHandle),
                 (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)logTaskHandle),
                 (unsigned long)uxTaskGetStackHighWaterMark(NULL));
    }
  }
}
/* USER CODE END Application */

