#ifndef SENDMESSAGE_H
#define SENDMESSAGE_H

#include "main.h"
#include "usart.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// 串口句柄声明（在usart.c中定义）
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
// 波形相关常量定义
#define SINE_TABLE_SIZE 1024
#define WAVEFORM_COMMAND_BUFFER_SIZE 32

// 函数声明
void SendMessage_Init(void);
void SendMessage_String(const char* message);
void SendMessage_String2(const char* message);
void SendMessage_Data(uint8_t *data, uint16_t length);
void SendMessage_Printf(const char* format, ...);
void SendMessage_Printf2(const char* format, ...);
void SendMessage_Command(const char* command);
void SendMessage_CommandWithEnd(const char* command);

// 波形发送相关函数
void SendMessage_SendSineWave(void);

// ADC 数据发送相关函数
void SendMessage_SendADCWave(const uint16_t* adc_data, uint16_t size);
void SendMessage_SendADCWave_8bit(const uint8_t* adc_data, uint16_t size);

#endif
