#ifndef __BH1750_H__
#define __BH1750_H__

#include "main.h"

/* ================= 用户配置区 ================= */
// 这里的 hi2c1 对应 CubeMX 生成的 I2C 句柄
// 如果你使用的是 I2C2，请修改为 hi2c2
extern I2C_HandleTypeDef hi2c1;
#define BH1750_I2C_HANDLE  &hi2c1  

// ADDR 引脚状态
// 如果 ADDR 接地，地址为 0x46 (0x23 << 1)
// 如果 ADDR 接 3.3V，地址为 0xB8 (0x5C << 1)
#define BH1750_ADDR_WRITE  0x46
#define BH1750_ADDR_READ   0x47
/* ============================================ */

/* BH1750 指令码 */
#define BH1750_POWER_ON           0x01  // 通电
#define BH1750_RESET              0x07  // 复位
#define BH1750_CONTINUOUS_HIGH_RES_MODE  0x10 // 连续高分辨率模式 (1Lux, 120ms)

/* 函数声明 */
void BH1750_Init(void);
float BH1750_ReadLux(void);

#endif /* __BH1750_H__ */
