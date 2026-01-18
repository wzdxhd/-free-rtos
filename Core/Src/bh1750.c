#include "BH1750.h"

/**
 * @brief  初始化 BH1750 光照传感器
 * @note   使用前请确保 I2C 硬件已初始化
 */
void BH1750_Init(void)
{
    uint8_t t_Data;
    
    // 1. 发送通电指令
    t_Data = BH1750_POWER_ON;
    HAL_I2C_Master_Transmit(BH1750_I2C_HANDLE, BH1750_ADDR_WRITE, &t_Data, 1, HAL_MAX_DELAY);
    
    // 2. 发送复位指令 (可选，但在重新初始化时很有用)
    t_Data = BH1750_RESET;
    HAL_I2C_Master_Transmit(BH1750_I2C_HANDLE, BH1750_ADDR_WRITE, &t_Data, 1, HAL_MAX_DELAY);
    
    // 3. 设置为连续高分辨率模式
    // 这种模式下，传感器会一直进行测量，我们随时可以读取最新结果
    t_Data = BH1750_CONTINUOUS_HIGH_RES_MODE;
    HAL_I2C_Master_Transmit(BH1750_I2C_HANDLE, BH1750_ADDR_WRITE, &t_Data, 1, HAL_MAX_DELAY);
	
		//第一次测量需要等待至少 180ms
		HAL_Delay(180);
}

/**
 * @brief  读取光照强度值
 * @return float 光照强度 (单位: Lux)，若读取失败返回 -1.0
 */
float BH1750_ReadLux(void)
{
    uint8_t dt[2];
    uint16_t val = 0;
    float lux = 0.0;

    // 从 I2C 读取 2 个字节的数据
    // HAL_I2C_Master_Receive 会自动处理读写位，这里传入写地址即可（HAL库内部处理）
    if(HAL_I2C_Master_Receive(BH1750_I2C_HANDLE, BH1750_ADDR_WRITE, dt, 2, HAL_MAX_DELAY) == HAL_OK)
    {
        // 拼接数据：高字节在前，低字节在后
        val = (dt[0] << 8) | dt[1];
        
        // 转换公式：寄存器值 / 1.2
        lux = (float)val / 1.2f;
        
        return lux;
    }
    else
    {
        // 读取失败
        return -1.0f;
    }
}