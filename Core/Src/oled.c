#include "oled.h"
#include "i2c.h" // 引入 hi2c1
#include "oled_font.h" // 确保你有这个字库文件，或者把字库数组贴在这里
#include <stdio.h>
#include <string.h>
#include "dht11.h"

// 引用 HAL 库的 I2C 句柄
extern I2C_HandleTypeDef hi2c1;

// 显存
uint8_t OLED_GRAM[8][128];

// ===============================================
// 核心底层驱动：HAL库 硬件I2C发送
// ===============================================
void OLED_WR_Byte(uint8_t dat, uint8_t mode)
{
    // mode=0: 命令(寄存器0x00), mode=1: 数据(寄存器0x40)
    uint8_t mem_addr = (mode == OLED_CMD) ? 0x00 : 0x40;
    
    // 使用硬件 I2C 发送
     HAL_StatusTypeDef status = HAL_I2C_Mem_Write(&hi2c1, OLED_ADDR, mem_addr, I2C_MEMADD_SIZE_8BIT, &dat, 1, 100);
	
		// === 增加容错检查和微小延时 ===
    if(status != HAL_OK) {
        // 如果发送 OLED 失败，为了防止影响后面，清除一下错误标志
        __HAL_I2C_CLEAR_FLAG(&hi2c1, I2C_FLAG_AF);
        __HAL_I2C_CLEAR_FLAG(&hi2c1, I2C_FLAG_BERR);
    }
}

// ===============================================
// 以下为逻辑层 (保留了你原来的算法)
// ===============================================

// 更新显存到OLED (批量写入，速度更快)
void OLED_Refresh(void)
{
    uint8_t i;
    for(i = 0; i < 8; i++)
    {
        OLED_WR_Byte(0xB0 + i, OLED_CMD); // 设置页地址
        OLED_WR_Byte(0x00, OLED_CMD);     // 设置低列起始地址
        OLED_WR_Byte(0x10, OLED_CMD);     // 设置高列起始地址
        
        // 使用 HAL 库一次性写入 128 个字节，比一个字节一个字节发快得多
        HAL_I2C_Mem_Write(&hi2c1, OLED_ADDR, 0x40, I2C_MEMADD_SIZE_8BIT, OLED_GRAM[i], 128, 100);
    }
}

// 清屏（操作显存）
void OLED_Clear(void)
{
    uint8_t page, col;
    for(page = 0; page < 8; page++)
    {
        for(col = 0; col < 128; col++)
        {
            OLED_GRAM[page][col] = 0;
        }
    }
    OLED_Refresh();
}
// 仅仅清空显存数组（不刷新屏幕，用于防止闪烁）
void OLED_NewFrame(void) {
    memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
}

// 画点
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t t)
{
    uint8_t page, bit_offset;
    
    // 边界检查，防止数组越界导致死机
    if(x > 127 || y > 63) return; 

    page = y / 8;        // 计算在哪一页 (0-7)
    bit_offset = y % 8;  // 计算在该字节的哪一位 (0-7)

    if(t)
    {
        // 注意这里是 [page][x]，对应定义 u8 OLED_GRAM[8][128]
        OLED_GRAM[page][x] |= (1 << bit_offset);
    }
    else
    {
        OLED_GRAM[page][x] &= ~(1 << bit_offset);
    }
}

// 画线
void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t mode)
{
    uint16_t t; 
    int xerr = 0, yerr = 0, delta_x, delta_y, distance; 
    int incx, incy, uRow, uCol; 
    delta_x = x2 - x1; 
    delta_y = y2 - y1; 
    uRow = x1; 
    uCol = y1; 
    if(delta_x > 0) incx = 1; 
    else if(delta_x == 0) incx = 0; 
    else {incx = -1; delta_x = -delta_x;} 
    if(delta_y > 0) incy = 1; 
    else if(delta_y == 0) incy = 0; 
    else {incy = -1; delta_y = -delta_y;} 
    if(delta_x > delta_y) distance = delta_x; 
    else distance = delta_y; 
    for(t = 0; t <= distance; t++) 
    { 
        OLED_DrawPoint(uRow, uCol, mode); 
        xerr += delta_x; 
        yerr += delta_y; 
        if(xerr > distance) 
        { 
            xerr -= distance; 
            uRow += incx; 
        } 
        if(yerr > distance) 
        { 
            yerr -= distance; 
            uCol += incy; 
        } 
    } 
}

// 画圆
void OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t r)
{
    int a, b, num;
    a = 0;
    b = r;
    while(2 * b * b >= r * r)      
    {
        OLED_DrawPoint(x + a, y - b, 1);
        OLED_DrawPoint(x - a, y - b, 1);
        OLED_DrawPoint(x - a, y + b, 1);
        OLED_DrawPoint(x + a, y + b, 1);
        OLED_DrawPoint(x + b, y + a, 1);
        OLED_DrawPoint(x + b, y - a, 1);
        OLED_DrawPoint(x - b, y - a, 1);
        OLED_DrawPoint(x - b, y + a, 1);
        a++;
        num = (a * a + b * b) - r * r;
        if(num > 0)
        {
            b--;
            a--;
        }
    }
}

// 显示字符
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size1, uint8_t mode)
{
    uint8_t i, m, temp, size2, chr1;
    uint8_t x0 = x, y0 = y;
    if(size1 == 8) size2 = 6;
    else size2 = (size1 / 8 + ((size1 % 8) ? 1 : 0)) * (size1 / 2); 
    chr1 = chr - ' '; 
    for(i = 0; i < size2; i++)
    {
        // 注意：这里需要 oled_font.h 中的字库数组
        // 如果没有 asc2_1608 等数组，请把你原来代码里的数组粘进来
        if(size1 == 8)       {temp = asc2_0806[chr1][i];} 
        else if(size1 == 12) {temp = asc2_1206[chr1][i];} 
        else if(size1 == 16) {temp = asc2_1608[chr1][i];} 
        else return;

        for(m = 0; m < 8; m++)
        {
            if(temp & 0x01) OLED_DrawPoint(x, y, mode);
            else OLED_DrawPoint(x, y, !mode);
            temp >>= 1;
            y++;
        }
        x++;
        if((size1 != 8) && ((x - x0) == size1 / 2))
        {
            x = x0; y0 = y0 + 8;
        }
        y = y0;
    }
}

// 显示字符串
void OLED_ShowString(uint8_t x, uint8_t y, char *chr, uint8_t size1, uint8_t mode)
{
    while((*chr >= ' ') && (*chr <= '~'))
    {
        OLED_ShowChar(x, y, *chr, size1, mode);
        if(size1 == 8) x += 6;
        else x += size1 / 2;
        chr++;
    }
}

// m^n
uint32_t OLED_Pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    while(n--) result *= m;
    return result;
}

// 显示数字
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size1, uint8_t mode)
{
    uint8_t t, temp, m = 0;
    if(size1 == 8) m = 2;
    for(t = 0; t < len; t++)
    {
        temp = (num / OLED_Pow(10, len - t - 1)) % 10;
        if(temp == 0) OLED_ShowChar(x + (size1 / 2 + m) * t, y, '0', size1, mode);
        else          OLED_ShowChar(x + (size1 / 2 + m) * t, y, temp + '0', size1, mode);
    }
}

// 显示浮点数
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t precision, uint8_t size, uint8_t mode)
{
    char buffer[32];
    // 你的工程需要启用 float printf 支持，或者手动实现 ftoa
    sprintf(buffer, "%.*f", precision, num); 
    OLED_ShowString(x, y, buffer, size, mode);
}

// 反显
void OLED_ColorTurn(uint8_t i)
{
    if(i == 0) OLED_WR_Byte(0xA6, OLED_CMD); // 正常显示
    if(i == 1) OLED_WR_Byte(0xA7, OLED_CMD); // 反色显示
}

// 屏幕旋转180度
void OLED_DisplayTurn(uint8_t i)
{
    if(i == 0)
    {
        OLED_WR_Byte(0xC8, OLED_CMD);
        OLED_WR_Byte(0xA1, OLED_CMD);
    }
    if(i == 1)
    {
        OLED_WR_Byte(0xC0, OLED_CMD);
        OLED_WR_Byte(0xA0, OLED_CMD);
    }
}

// OLED初始化 (只保留寄存器配置，删除 GPIO 配置)
void OLED_Init(void)
{
    HAL_Delay(200); // 使用 HAL 延时

    OLED_WR_Byte(0xAE, OLED_CMD);
    OLED_WR_Byte(0x00, OLED_CMD);
    OLED_WR_Byte(0x10, OLED_CMD);
    OLED_WR_Byte(0x40, OLED_CMD);
    OLED_WR_Byte(0x81, OLED_CMD);
    OLED_WR_Byte(0xCF, OLED_CMD);
    OLED_WR_Byte(0xA1, OLED_CMD);
    OLED_WR_Byte(0xC8, OLED_CMD);
    OLED_WR_Byte(0xA6, OLED_CMD);
    OLED_WR_Byte(0xA8, OLED_CMD);
    OLED_WR_Byte(0x3f, OLED_CMD);
    OLED_WR_Byte(0xD3, OLED_CMD);
    OLED_WR_Byte(0x00, OLED_CMD);
    OLED_WR_Byte(0xd5, OLED_CMD);
    OLED_WR_Byte(0x80, OLED_CMD);
    OLED_WR_Byte(0xD9, OLED_CMD);
    OLED_WR_Byte(0xF1, OLED_CMD);
    OLED_WR_Byte(0xDA, OLED_CMD);
    OLED_WR_Byte(0x12, OLED_CMD);
    OLED_WR_Byte(0xDB, OLED_CMD);
    OLED_WR_Byte(0x30, OLED_CMD);
    OLED_WR_Byte(0x20, OLED_CMD);
    OLED_WR_Byte(0x02, OLED_CMD);
    OLED_WR_Byte(0x8D, OLED_CMD);
    OLED_WR_Byte(0x14, OLED_CMD);
    OLED_WR_Byte(0xA4, OLED_CMD);
    OLED_WR_Byte(0xAF, OLED_CMD);
    
    OLED_Clear();
}

void OLED_Show_EnvData(DHT11_Data_t *dht, float light_lux)
{
    OLED_NewFrame();   // 只清显存，不立刻刷屏

    /* ===== 标题 ===== */
    OLED_ShowString(0, 0, "STM32 Env Monitor", 16, 1);
    OLED_DrawLine(0, 16, 128, 16, 1);

    /* ===== 温度 ===== */
    OLED_ShowString(0, 20, "Temp :", 12, 1);
    OLED_ShowNum(56, 20, dht->temperature, 2, 12, 1);
    OLED_ShowChar(72, 20, '.', 12, 1);
    OLED_ShowNum(80, 20, dht->temp_dec, 1, 12, 1);
    OLED_ShowChar(88, 20, 'C', 12, 1);

    /* ===== 湿度 ===== */
    OLED_ShowString(0, 36, "Humi :", 12, 1);
    OLED_ShowNum(56, 36, dht->humidity, 2, 12, 1);
    OLED_ShowChar(72, 36, '.', 12, 1);
    OLED_ShowNum(80, 36, dht->humi_dec, 1, 12, 1);
    OLED_ShowChar(88, 36, '%', 12, 1);

    /* ===== 光照 ===== */
    OLED_ShowString(0, 52, "Light:", 12, 1);
    OLED_ShowFloat(56, 52, light_lux, 1, 12, 1);

    OLED_Refresh();   // 一次性刷屏
}