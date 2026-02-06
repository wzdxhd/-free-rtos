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
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stmflash.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef void (*pFunction)(void); // 定义一个函数指针类型
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_ADDR        0x08010000  // 主程序运行区
#define OTA_ADDR        0x08080000  // 备份区(下载区)
#define FLAG_ADDR       0x0800C000  // 标志位存放区 (Sector 3)
#define OTA_FLAG_MAGIC  0xAAAAAAAA  // 升级标志
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
pFunction JumpToApplication;     // 定义跳转函数指针
uint32_t JumpAddress;            // 用来存 App 的复位中断向量地址
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void UART_Printf(char *str)
{
    uint16_t len = 0;
    // 手动计算字符串长度，代替 strlen
    while(str[len] != '\0')
    {
        len++;
    }
    HAL_UART_Transmit(&huart1, (uint8_t*)str, len, 100);
}

void UART_SendHex(uint32_t num)
{
    char hexDigits[] = "0123456789ABCDEF";
    char buffer[11]; // "0x" + 8位数字 + 结束符
    
    buffer[0] = '0';
    buffer[1] = 'x';
    
    // 从高位到低位转换
    for(int i = 0; i < 8; i++)
    {
        buffer[9 - i] = hexDigits[num & 0xF]; // 取最低4位
        num >>= 4; // 右移4位
    }
    buffer[10] = '\0'; // 字符串结束符
    
    UART_Printf(buffer);
}

void OTA_Copy_Flash(uint32_t SourceAddr, uint32_t DestAddr, uint32_t Len)
{
    uint32_t i = 0;
    uint32_t data;
    
    // 1. 先擦除目标区域 (APP 区: Sector 4~7, 448KB)
    // 注意：这里要小心，别把 Bootloader 自己擦了
    FLASH_EraseInitTypeDef EraseInit;
    uint32_t SectorError;
    
    HAL_FLASH_Unlock();
    
    UART_Printf("Erasing APP Area...\r\n");
    EraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInit.Sector = FLASH_SECTOR_4; // 从 Sector 4 开始
    EraseInit.NbSectors = 4;           // 擦除 4, 5, 6, 7 (直到 0x0807FFFF)
    
    if (HAL_FLASHEx_Erase(&EraseInit, &SectorError) != HAL_OK)
    {
				UART_Printf("Erase Failed! Sector Code: ");
        UART_SendHex(SectorError); // 打印错误扇区号
        UART_Printf("\r\n");
        HAL_FLASH_Lock();
        return;
    }
    
    // 2. 循环读取并写入
    UART_Printf("Copying Firmware...\r\n");
    for (i = 0; i < Len; i += 4)
    {
        // 从备份区读取 4 字节
        data = *(__IO uint32_t*)(SourceAddr + i);
        
        // 写入到运行区
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, DestAddr + i, data) != HAL_OK)
        {
						UART_Printf("Write Error at: ");
            UART_SendHex(DestAddr + i); // 打印出错地址
            UART_Printf("\r\n");
            break;
        }
        
        // (可选) 每搬运 1KB 翻转一下 LED，提示正在工作
        if (i % 1024 == 0) HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
    
    HAL_FLASH_Lock();
    UART_Printf("Copy Done!\r\n");
}
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  /* USER CODE BEGIN 2 */
	//打印调试信息，或闪烁 LED 表示进入了 Bootloader
  UART_Printf("Start Bootloader...\r\n");
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); 
  HAL_Delay(200); // 延时一下让人眼能看到 LED 亮过，确认 Bootloader 跑过了
	
	// --- 1. 检查升级标志位 ---
	uint32_t flag = *(__IO uint32_t*)FLAG_ADDR;
	
	if (flag == OTA_FLAG_MAGIC)
    {
        UART_Printf("[OTA] Update Flag Detect!\r\n");
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET); // 亮灯
        
        // 读取固件长度 (假设存在标志位的后 4 字节)
        // 如果你之前 APP 里没写长度，这里可以先写死一个值测试，比如 64KB (0x10000)
        // uint32_t firmware_len = *(__IO uint32_t*)(FLAG_ADDR + 4);
        uint32_t firmware_len = 0x20000; // 先假设固件有 128KB，宁多勿少
        
        // 开始搬运
        OTA_Copy_Flash(OTA_ADDR, APP_ADDR, firmware_len);
        
        // 搬运完成后，清除标志位 (防止无限重启升级)
        UART_Printf("[OTA] Clearing Flag...\r\n");
        HAL_FLASH_Unlock();
        FLASH_EraseInitTypeDef EraseInit;
        uint32_t SectorError;
        EraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
        EraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        EraseInit.Sector = FLASH_SECTOR_3; // 擦除存放 Flag 的 Sector 3
        EraseInit.NbSectors = 1;
        HAL_FLASHEx_Erase(&EraseInit, &SectorError);
        HAL_FLASH_Lock();
        
        UART_Printf("[OTA] Update Complete! Resetting...\r\n");
        HAL_Delay(500);
        HAL_NVIC_SystemReset(); // 软重启
    }
    else
    {
        UART_Printf("[Boot] No Update. Launching App...\r\n");
    }

  // 2. 检查 APP 地址是否合法
  // 原理：检查栈顶地址是否在 SRAM 范围内 (0x20000000 - 0x20030000)
  // STM32 的向量表第一个字存的是栈顶地址 (Stack Pointer)
  if (((*(__IO uint32_t*)APP_ADDR) & 0x2FF00000) == 0x20000000)
  {
      // 3. 准备跳转
      // 打印信息：即将跳转
      // HAL_UART_Transmit(&huart1, (uint8_t*)"Jumping to App...\r\n", 19, 100);
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);//跳转前熄灭LED
		
		  // 取出 App 的复位中断地址 (向量表的第二个字，偏移 +4)
      JumpAddress = *(__IO uint32_t*) (APP_ADDR + 4);
      JumpToApplication = (pFunction) JumpAddress;
		
			//外设去初始化
			HAL_UART_DeInit(&huart1); // 关闭串口1 (如果 APP 也用串口1，不关可能会冲突)
      HAL_GPIO_DeInit(GPIOC, GPIO_PIN_13); // 把 LED 引脚释放回默认状态
      HAL_RCC_DeInit();         // (建议) 复位时钟配置，让 APP 自己去配置

      // 4. 【关键】关闭所有中断
      __disable_irq(); 
      
      // 也可以为了保险，关闭 SysTick
      SysTick->CTRL = 0;

      // 5. 初始化 App 的堆栈指针 (MSP)
      __set_MSP(*(__IO uint32_t*) APP_ADDR);

      // 6. 执行跳转！从此不再回头
      JumpToApplication();
  }
  else
  {
      // 如果地址不合法（比如 App 还没烧录），就死循环闪灯报警
      while(1)
      {
          HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
          HAL_Delay(100);
      }
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

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

#ifdef  USE_FULL_ASSERT
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
