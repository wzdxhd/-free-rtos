#ifndef __DELAY_US_H
#define __DELAY_US_H
#include "stm32f4xx_hal.h"

// DWT 初始化与延时宏定义
#define DWT_DELAY_INIT() { \
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; \
    DWT->CYCCNT = 0; \
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; \
}

// 168MHz / 1000000 = 168 cycles per us
#define delay_us(us) { \
    uint32_t start = DWT->CYCCNT; \
    uint32_t cycles = (us) * 168; \
    while((DWT->CYCCNT - start) < cycles); \
}
#endif