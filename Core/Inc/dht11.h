#ifndef __DHT11_H
#define __DHT11_H

#include "main.h"

// ================= 硬件接口定义（请根据实际连线修改） =================
// 假设你接的是 PG11，如果是 PA5 请改为 GPIOA 和 GPIO_PIN_5
#define DHT11_PORT      GPIOF
#define DHT11_PIN       GPIO_PIN_9
#define DHT11_CLK_ENABLE() __HAL_RCC_GPIOG_CLK_ENABLE() 
// ===================================================================

// 数据结构体
typedef struct {
    uint8_t temperature; // 温度整数部分
    uint8_t humidity;    // 湿度整数部分
    uint8_t temp_dec;    // 温度小数部分（DHT11通常为0）
    uint8_t humi_dec;    // 湿度小数部分（DHT11通常为0）
} DHT11_Data_t;

// 函数声明
void DHT11_Init(void);
uint8_t DHT11_Read_Data(DHT11_Data_t *DHT11_Data);

#endif