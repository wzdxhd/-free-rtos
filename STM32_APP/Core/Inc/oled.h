#ifndef __OLED_H
#define __OLED_H

#include "main.h" // 包含 HAL 库定义

// 定义 OLED 地址
#define OLED_ADDR 0x78

#define OLED_CMD  0	//写命令
#define OLED_DATA 1	//写数据

// 屏幕分辨率
#define OLED_WIDTH  128
#define OLED_HEIGHT 64
#define OLED_PAGES  8

// 函数声明
void OLED_Init(void);
void OLED_Clear(void);
void OLED_NewFrame(void); // 清除显存
void OLED_Refresh(void);  // 更新显存到屏幕
void OLED_ColorTurn(uint8_t i);
void OLED_DisplayTurn(uint8_t i);

// 绘图函数
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t t);
void OLED_DrawLine(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t mode);
void OLED_DrawCircle(uint8_t x, uint8_t y, uint8_t r);

// 显示函数
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size1, uint8_t mode);
void OLED_ShowString(uint8_t x, uint8_t y, char *chr, uint8_t size1, uint8_t mode);
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size1, uint8_t mode);
void OLED_ShowFloat(uint8_t x, uint8_t y, float num, uint8_t precision, uint8_t size, uint8_t mode);

#endif
