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

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// 串口接收相关
static uint8_t rx_byte;           // 用于单字节中断接收指令
static char cmd_buf[64];          // 命令缓冲
static uint8_t cmd_idx = 0;

static uint8_t ota_rx_buf[260];   // DMA 目标缓冲区
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

	OTA_Init();
	
  for(;;)
  {
    switch(g_ota.state)
		{
				// --------------------------------------------------
          // 状态1：空闲态 (平常运行模式，上传传感器数据)
          // --------------------------------------------------
          case OTA_STATE_IDLE:
              if (HAL_GetTick() - last_sensor_time >= 5000)
              {
                  last_sensor_time = HAL_GetTick();
                  SendMessage_Printf2("%d.%d,%d.%d,%d,%.1f\n", 
                      g_SensorData.temperature, g_SensorData.temp_dec, 
                      g_SensorData.humidity, g_SensorData.humi_dec, 
                      1, g_LightLux);
              }
              osDelay(50); // 空闲时让出 CPU
              break;

          // --------------------------------------------------
          // 状态2：初始化态 (挂起其他任务并擦除 Flash)
          // --------------------------------------------------
          case OTA_STATE_INIT_ERASE:
              // 挂起传感器和显示任务，防止擦除时被中断打断
              osThreadSuspend(SensorTaskHandle);
              osThreadSuspend(DisplayTaskHandle);
              
              OLED_Clear();
              OLED_ShowString(0, 0, "OTA Start...", 16, 1);
              OLED_ShowString(0, 2, "Erasing...", 16, 1);
              OLED_Refresh();

              SendMessage_Printf("Erasing Flash, Size: %lu\r\n", g_ota.fw_total_size);

              // 擦除 Flash
              if (!OTA_Erase_Download_Area(g_ota.fw_total_size)) {
                   SendMessage_Printf("Erase Failed!\r\n");
                   g_ota.state = OTA_STATE_ERROR;
              } else {
                   // 擦除成功，切换到接收数据状态，并向 ESP32 发送第一条 ACK
                   FLUSH_UART_ERRORS(&huart2);
                   OTA_Send_ACK(); 
                   g_ota.state = OTA_STATE_WAIT_DATA;
              }
              break;

          // --------------------------------------------------
          // 状态3：接收与校验态 (基于 DMA 接收，收到后校验写入)
          // --------------------------------------------------
          case OTA_STATE_WAIT_DATA:
          {
              // 计算这一包需要接收的真实载荷长度
              uint32_t remain = g_ota.fw_total_size - g_ota.fw_received_size;
              uint16_t payload_len = (remain > OTA_PAYLOAD_MAX_SIZE) ? OTA_PAYLOAD_MAX_SIZE : remain;
              
              // 实际 DMA 配置接收长度 = 载荷长度 + 2字节CRC
              uint16_t dma_len = payload_len + 2; 

              // 清理上次可能遗留的溢出错误，并启动 DMA
              FLUSH_UART_ERRORS(&huart2);
              if (HAL_UART_Receive_DMA(&huart2, ota_rx_buf, dma_len) != HAL_OK)
              {
                  HAL_UART_AbortReceive(&huart2);
                  FLUSH_UART_ERRORS(&huart2);
                  HAL_UART_Receive_DMA(&huart2, ota_rx_buf, dma_len);
              }

              // 阻塞任务，等待串口 DMA 接收完成中断的通知（超时5000ms）
              if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) == pdTRUE)
              {
                  // 1. 收到数据，计算前 payload_len 个字节的 CRC16
                  uint16_t calc_crc = CRC16_Calculate(ota_rx_buf, payload_len);
                  // 2. 提取数据包尾部的 CRC16 (ESP32小端模式发出)
                  uint16_t recv_crc = ota_rx_buf[payload_len] | (ota_rx_buf[payload_len+1] << 8);

                  if (calc_crc == recv_crc) 
                  {
                      // 校验通过：写入 Flash
                      OTA_Write_Flash(g_ota.flash_write_addr, ota_rx_buf, payload_len);
                      
                      // 推进数据指针
                      g_ota.flash_write_addr += payload_len;
                      g_ota.fw_received_size += payload_len;

                      // 更新进度 UI
                      if (g_ota.fw_received_size % 1024 == 0 || g_ota.fw_received_size == g_ota.fw_total_size) {
                           char s[16];
                           sprintf(s, "%lu%%", g_ota.fw_received_size * 100 / g_ota.fw_total_size);
                           OLED_ShowString(0, 4, s, 16, 1);
                           OLED_Refresh();
                      }

                      // 判断是否收满
                      if (g_ota.fw_received_size >= g_ota.fw_total_size) {
                          g_ota.state = OTA_STATE_FINISH;
                      } else {
                          OTA_Send_ACK(); // 请求 ESP32 发送下一包
                      }
                  } 
                  else 
                  {
                      // 校验失败：数据出错，丢弃该包，发 NACK 请求重发
                      SendMessage_Printf("CRC Err: Calc %04X, Recv %04X\r\n", calc_crc, recv_crc);
                      HAL_UART_AbortReceive(&huart2);
                      OTA_Send_NACK();
                  }
              }
              else
              {
                  // 超时 5 秒没收到数据，认定 ESP32 卡死或断连，尝试发送 NACK 重启传输
                  SendMessage_Printf("TIMEOUT! Recv: %lu\r\n", g_ota.fw_received_size);
                  HAL_UART_AbortReceive(&huart2);
                  OTA_Send_NACK(); 
              }
              break;
          }

          // --------------------------------------------------
          // 状态4：完成态 (写标志位并重启)
          // --------------------------------------------------
          case OTA_STATE_FINISH:
              OLED_ShowString(0, 6, "Success! Reset", 16, 1);
              OLED_Refresh();
              
              OTA_Send_ACK(); // 最后一包确认
              HAL_Delay(500); 
              
              // 写入 Bootloader Flag 并重启芯片
              OTA_Set_Flag_And_Reset(g_ota.fw_total_size);
              break;

          // --------------------------------------------------
          // 状态5：错误态 (无法挽回的异常)
          // --------------------------------------------------
          case OTA_STATE_ERROR:
              OLED_ShowString(0, 6, "OTA Error!  ", 16, 1);
              OLED_Refresh();
              HAL_Delay(2000);
              HAL_NVIC_SystemReset();
              break;
				}
		}
}

/* USER CODE BEGIN Application */

// 串口接收完成回调 (DMA 和 IT 都会调用这个)
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) return;

    if (g_ota.state == OTA_STATE_IDLE) 
    {
        // 模式1: Idle 下用单字节中断收命令
        if (cmd_idx < sizeof(cmd_buf) - 1) {
            cmd_buf[cmd_idx++] = rx_byte;
            cmd_buf[cmd_idx] = 0;
        } else {
            cmd_idx = 0; // 溢出保护
        }

        // 只有收到换行符才开始解析
        if (rx_byte == '\n') 
        {
            char* pStart = strstr(cmd_buf, "CMD_OTA_START:");
            if (pStart != NULL) {
                uint32_t len = 0;
                // 解析数字
                if (sscanf(pStart + 14, "%lu", &len) == 1 && len > 0) {
                    
                    // 【更新全局结构体状态】
                    g_ota.fw_total_size = len;
                    g_ota.state = OTA_STATE_INIT_ERASE; 
                    
                    // 清空缓冲
                    cmd_idx = 0;
                    memset(cmd_buf, 0, sizeof(cmd_buf));
                    
                    // 匹配成功直接返回，不再开启中断 (交由后续的 WAIT_DATA DMA 接管)
                    return; 
                }
            }
            // 如果是换行但没匹配到命令，清空缓冲区继续
            cmd_idx = 0;
            memset(cmd_buf, 0, sizeof(cmd_buf));
        }

        // 继续接收下一个字节
        HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
    }
    else if (g_ota.state == OTA_STATE_WAIT_DATA) 
    {
        // 模式2: 等待数据态下，DMA 收完了一整包 (256载荷+2CRC字节) 触发中断
        // 唤醒 CommTask 任务去算 CRC 和 写 Flash
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(CommTaskHandle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
/* USER CODE END Application */