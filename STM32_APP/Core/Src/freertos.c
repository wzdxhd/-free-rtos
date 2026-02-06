/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : DMA 版本的 OTA 逻辑 - 专治数据溢出
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
#include "ota.h"
#include "dht11.h"
#include "BH1750.h"
#include "oled.h"
#include "SendMessage.h"
#include <stdio.h>
#include <string.h>

extern volatile DHT11_Data_t g_SensorData;
extern volatile float g_LightLux;
extern UART_HandleTypeDef huart2;
extern void OLED_Show_EnvData(DHT11_Data_t *dht, float light_lux);

// 引用自身任务句柄，用于 DMA 完成后唤醒
extern osThreadId CommTaskHandle; 
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
volatile OTA_State_t ota_state = OTA_IDLE;
volatile uint32_t ota_firmware_len = 0;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// 串口接收相关
static uint8_t rx_byte;           // 用于单字节中断接收指令
static char cmd_buf[64];          // 命令缓冲
static uint8_t cmd_idx = 0;

static uint8_t ota_rx_buf[256];   // DMA 目标缓冲区
static uint8_t flash_cache_buf[256]; // Flash 拼装缓冲区
static uint16_t flash_cache_len = 0;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
// 辅助宏：清除串口错误标志，防止 DMA 启动失败
#define FLUSH_UART_ERRORS(__HANDLE__) do{ \
    volatile uint32_t tmpreg; \
    tmpreg = (__HANDLE__)->Instance->SR; \
    tmpreg = (__HANDLE__)->Instance->DR; \
    (void)tmpreg; \
}while(0)
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
osThreadId SensorTaskHandle;
osThreadId DisplayTaskHandle;
osThreadId CommTaskHandle;
osMutexId myI2CMutexHandle;

/* Private function prototypes -----------------------------------------------*/
void StartSensorTask(void const * argument);
void StartDisplayTask(void const * argument);
void StartCommTask(void const * argument);
void MX_FREERTOS_Init(void); 

static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize ) {
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void MX_FREERTOS_Init(void) {
  osMutexDef(myI2CMutex);
  myI2CMutexHandle = osMutexCreate(osMutex(myI2CMutex));

  osThreadDef(SensorTask, StartSensorTask, osPriorityAboveNormal, 0, 1024);
  SensorTaskHandle = osThreadCreate(osThread(SensorTask), NULL);

  osThreadDef(DisplayTask, StartDisplayTask, osPriorityLow, 0, 512);
  DisplayTaskHandle = osThreadCreate(osThread(DisplayTask), NULL);

  osThreadDef(CommTask, StartCommTask, osPriorityNormal, 0, 2048); 
  CommTaskHandle = osThreadCreate(osThread(CommTask), NULL);
}

// 传感器任务
void StartSensorTask(void const * argument) {
    DHT11_Data_t temp_dht;
    float temp_lux;
    for(;;) {
        osMutexWait(myI2CMutexHandle, osWaitForever);
        temp_lux = BH1750_ReadLux();
        osMutexRelease(myI2CMutexHandle);
        taskENTER_CRITICAL(); 
        uint8_t res = DHT11_Read_Data(&temp_dht);
        taskEXIT_CRITICAL(); 
        if(res == 0) g_SensorData = temp_dht;
        g_LightLux = temp_lux;
        osDelay(2000); 
    }
}

// 显示任务
void StartDisplayTask(void const * argument) {
    for(;;) {
        osMutexWait(myI2CMutexHandle, osWaitForever);
        OLED_Show_EnvData((DHT11_Data_t*)&g_SensorData, g_LightLux);
        osMutexRelease(myI2CMutexHandle);
        osDelay(500);
    }
}

/* USER CODE BEGIN Header_StartCommTask */
/**
* @brief DMA 版 CommTask
*/
/* USER CODE END Header_StartCommTask */
#define FLUSH_UART_ERRORS(__HANDLE__) do{ \
    volatile uint32_t tmpreg; \
    tmpreg = (__HANDLE__)->Instance->SR; \
    tmpreg = (__HANDLE__)->Instance->DR; \
    (void)tmpreg; \
}while(0)

void StartCommTask(void const * argument)
{
  // 1. 启动命令侦听
  HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
  
  // 调试：打印任务启动
  SendMessage_Printf("CommTask Started. DMA Mode.\r\n");

  uint32_t last_sensor_time = 0;

  for(;;)
  {
    if (ota_state == OTA_READY)
    {
        ota_state = OTA_RUNNING;
        
        // 挂起其他任务...
        osThreadSuspend(SensorTaskHandle);
        osThreadSuspend(DisplayTaskHandle);
        OLED_Clear();
        OLED_ShowString(0, 0, "OTA Start...", 16, 1);
        OLED_Refresh();
        
        // 擦除...
        OLED_ShowString(0, 2, "Erasing...", 16, 1);
        OLED_Refresh();
        if (!OTA_Erase_Download_Area(ota_firmware_len)) {
             SendMessage_Printf("Erase Failed!\r\n");
             HAL_NVIC_SystemReset();
        }

        uint32_t total_recved = 0;
        uint32_t flash_offset = 0;
        flash_cache_len = 0;
        
        SendMessage_Printf("OTA Loop Start. Size: %lu\r\n", ota_firmware_len);

        // 先清理一次
        FLUSH_UART_ERRORS(&huart2);
        
        // 发送握手
        OTA_Send_ACK();

        while (total_recved < ota_firmware_len)
        {
            uint32_t remain = ota_firmware_len - total_recved;
            uint16_t len = (remain > 256) ? 256 : remain;

            // --- 步骤 A: 强行清除错误 ---
            // 只要 SR 寄存器里有 ORE/NE/FE，DMA 就开启不了
            FLUSH_UART_ERRORS(&huart2);

            // --- 步骤 B: 启动 DMA ---
            if (HAL_UART_Receive_DMA(&huart2, ota_rx_buf, len) != HAL_OK)
            {
                // 如果启动失败，打印错误代码
                SendMessage_Printf("DMA Start Error: 0x%08X\r\n", huart2.ErrorCode);
                // 尝试复位接收状态
                HAL_UART_AbortReceive(&huart2);
                FLUSH_UART_ERRORS(&huart2);
                HAL_UART_Receive_DMA(&huart2, ota_rx_buf, len); // 再试一次
            }

            // --- 步骤 C: 告诉 ESP32 发数据 ---
            OTA_Send_ACK();

            // --- 步骤 D: 等待中断 ---
            // 如果 5秒 没等到，说明 DMA 中断没触发
            if (ulTaskNotifyTake(pdTRUE, 5000) == pdTRUE)
            {
                // ** 成功收到 **
                total_recved += len;
                
                // 写 Flash
                // ... (此处省略拼装逻辑，直接写，假设已对齐) ...
                // 建议保留你之前的拼装逻辑，这里简化演示：
                OTA_Write_Flash(flash_offset, ota_rx_buf, len);
                flash_offset += len;

                // UI
                if (total_recved % 1024 == 0) {
                     char s[16];
                     sprintf(s, "%lu%%", total_recved * 100 / ota_firmware_len);
                     OLED_ShowString(0, 4, s, 16, 1);
                     OLED_Refresh();
                }
            }
            else
            {
                // ** 超时 **
                SendMessage_Printf("TIMEOUT! Recv: %lu. UART State: 0x%X\r\n", total_recved, huart2.gState);
                OLED_ShowString(0, 4, "TIMEOUT", 16, 1);
                OLED_Refresh();
                
                // 停止 DMA，尝试复活
                HAL_UART_AbortReceive(&huart2);
                
                // 决定是重试还是重启
                HAL_NVIC_SystemReset();
            }
        }
        
        // 成功结束...
        OTA_Send_ACK();
        HAL_Delay(200);
        OTA_Set_Flag_And_Reset(ota_firmware_len);
    }

    // ==========================================
    // 正常模式
    // ==========================================
    if (HAL_GetTick() - last_sensor_time >= 5000)
    {
        last_sensor_time = HAL_GetTick();
        SendMessage_Printf2("%d.%d,%d.%d,%d,%.1f\n", 
            g_SensorData.temperature, g_SensorData.temp_dec, 
            g_SensorData.humidity, g_SensorData.humi_dec, 
            1, g_LightLux);
    }
    osDelay(50);
  }
}

/* USER CODE BEGIN Application */

// 串口接收完成回调 (DMA 和 IT 都会调用这个)
// 串口接收完成回调
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) return;

    if (ota_state == OTA_IDLE) 
    {
        // 模式1: 接收命令
        if (cmd_idx < sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_idx++] = rx_byte;
            cmd_buf[cmd_idx] = 0;
        } else {
            cmd_idx = 0; // 溢出保护
        }

        // 【关键修改】只有收到换行符才开始解析，防止解析出残缺的数字
        if (rx_byte == '\n') 
        {
            char* pStart = strstr(cmd_buf, "CMD_OTA_START:");
            if (pStart != NULL) {
                uint32_t len = 0;
                // 解析数字
                if (sscanf(pStart + 14, "%lu", &len) == 1 && len > 0) {
                    ota_firmware_len = len;
                    ota_state = OTA_READY; 
                    
                    // 清空缓冲，准备下一次
                    cmd_idx = 0;
                    memset(cmd_buf, 0, sizeof(cmd_buf));
                    return; // 退出，不再开启中断 (交由 DMA 接管)
                }
            }
            // 如果是换行但没匹配到命令，也清空缓冲区
            cmd_idx = 0;
            memset(cmd_buf, 0, sizeof(cmd_buf));
        }

        // 继续接收下一个字节
        HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
    }
    else if (ota_state == OTA_RUNNING) 
    {
        // 模式2: DMA 传输完成中断 -> 唤醒任务
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(CommTaskHandle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
/* USER CODE END Application */