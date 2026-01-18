/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "dht11.h"
#include "BH1750.h"
#include "oled.h"
#include "SendMessage.h"
#include <stdio.h>

extern volatile DHT11_Data_t g_SensorData;
extern volatile float g_LightLux;
extern void OLED_Show_EnvData(DHT11_Data_t *dht, float light_lux); // 引用你的显示辅助函数
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

/* USER CODE END Variables */
osThreadId SensorTaskHandle;
osThreadId DisplayTaskHandle;
osThreadId CommTaskHandle;
osMutexId myI2CMutexHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartSensorTask(void const * argument);
void StartDisplayTask(void const * argument);
void StartCommTask(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* definition and creation of myI2CMutex */
  osMutexDef(myI2CMutex);
  myI2CMutexHandle = osMutexCreate(osMutex(myI2CMutex));

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
  /* definition and creation of SensorTask */
  osThreadDef(SensorTask, StartSensorTask, osPriorityAboveNormal, 0, 1024);
  SensorTaskHandle = osThreadCreate(osThread(SensorTask), NULL);

  /* definition and creation of DisplayTask */
  osThreadDef(DisplayTask, StartDisplayTask, osPriorityLow, 0, 512);
  DisplayTaskHandle = osThreadCreate(osThread(DisplayTask), NULL);

  /* definition and creation of CommTask */
  osThreadDef(CommTask, StartCommTask, osPriorityNormal, 0, 512);
  CommTaskHandle = osThreadCreate(osThread(CommTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartSensorTask */
/**
  * @brief  Function implementing the SensorTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartSensorTask */
void StartSensorTask(void const * argument)
{
  /* USER CODE BEGIN StartSensorTask */
	
	DHT11_Data_t temp_dht;
  float temp_lux;
  /* Infinite loop */
  for(;;)
  {
		// --- 1. 读取 BH1750 (需要 I2C 锁) ---
    // osWaitForever 表示死等直到拿到锁
		
    osMutexWait(myI2CMutexHandle, osWaitForever);
    temp_lux = BH1750_ReadLux();
    osMutexRelease(myI2CMutexHandle); // 释放锁
		
    // --- 2. 读取 DHT11 (时序敏感，关中断保护) ---
    // 进入临界区，暂停所有中断和任务切换
    taskENTER_CRITICAL(); 
    uint8_t res = DHT11_Read_Data(&temp_dht);
    taskEXIT_CRITICAL(); // 退出临界区
    // --- 3. 更新全局变量 ---
    // 只有读取成功才更新，避免显示错误数据
    if(res == 0) {
        g_SensorData = temp_dht;
    }
    // BH1750 读取失败可能会返回特殊值，简单起见直接赋值
    g_LightLux = temp_lux;
		
    // --- 4. 采样周期 ---
    // DHT11 必须间隔 2秒以上
    osDelay(2000); 
  }
  /* USER CODE END StartSensorTask */
}

/* USER CODE BEGIN Header_StartDisplayTask */
/**
* @brief Function implementing the DisplayTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDisplayTask */
void StartDisplayTask(void const * argument)
{
  /* USER CODE BEGIN StartDisplayTask */
  /* Infinite loop */
  for(;;)
  {
		// --- 1. 获取 I2C 锁 ---
    // 只有拿到锁，才能操作 OLED，防止打断 BH1750 的通信
    osMutexWait(myI2CMutexHandle, osWaitForever);
    
    // --- 2. 刷屏 ---
    // 使用全局变量 g_SensorData 和 g_LightLux
    // 注意：这个函数里包含了 OLED_Refresh，涉及大量 I2C 写操作
    OLED_Show_EnvData((DHT11_Data_t*)&g_SensorData, g_LightLux);
    
    // --- 3. 释放锁 ---
    osMutexRelease(myI2CMutexHandle);

    // --- 4. 刷新率 ---
    // 屏幕不需要像传感器那么快，500ms 足够流畅
    osDelay(500);
  }
  /* USER CODE END StartDisplayTask */
}

/* USER CODE BEGIN Header_StartCommTask */
/**
* @brief Function implementing the CommTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartCommTask */
void StartCommTask(void const * argument)
{
  /* USER CODE BEGIN StartCommTask */
	int command = 1; // 定义的命令字

  // 等待传感器先采集一轮数据，避免一开始发 0
  osDelay(2500);
  /* Infinite loop */
  for(;;)
  {
		// --- 1. 发送到电脑 (UART1) ---
    // 这里使用局部副本来发送，防止发送过程中全局变量被修改（虽然概率低）
    DHT11_Data_t dht = g_SensorData;
    float lux = g_LightLux;

    SendMessage_Printf(
        "Temperature:%d.%d Humidity:%d.%d Light:%.1f\r\n",
        dht.temperature, dht.temp_dec,
        dht.humidity, dht.humi_dec,
        lux
    );

    // --- 2. 发送到 ESP32 (UART2) ---
    SendMessage_Printf2(
        "%d.%d,%d.%d,%d,%.1f",
        dht.temperature, dht.temp_dec,
        dht.humidity, dht.humi_dec,
        command, lux
    );

    // --- 3. 上传频率 ---
    // 云平台通常不需要太快，5秒一次比较合适，避免频繁发包
    osDelay(5000);
  }
  /* USER CODE END StartCommTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
