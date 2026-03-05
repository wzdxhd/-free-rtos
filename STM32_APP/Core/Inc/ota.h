/*
 * ota.h
 * 统一管理Flash地址，增加状态机定义
 */
#ifndef __OTA_H
#define __OTA_H

#include "main.h"
#include <stdio.h>
#include <string.h>



// ================== 资源配置 ==================
extern UART_HandleTypeDef huart2;
#define OTA_UART_HANDLE    &huart2

// ================== Flash 分区定义 ==================
// 根据 STM32F407 扇区定义 (需根据实际芯片手册确认)
// Sector 3 (16KB)
#define OTA_FLAG_SECTOR         FLASH_SECTOR_3
#define OTA_FLAG_ADDR           0x0800C000      
#define OTA_FLAG_MAGIC          0xAAAAAAAA

// Sector 8 (128KB) 起始地址
#define OTA_DOWNLOAD_SECTOR_START FLASH_SECTOR_8
#define OTA_DOWNLOAD_ADDR         0x08080000 

// ================== 协议定义 ==================
#define OTA_PAYLOAD_MAX_SIZE    256 
#define OTA_TIMEOUT_MS          100  // 接收数据包的超时时间（毫秒）

// OTA 状态枚举
typedef enum {
    OTA_STATE_IDLE = 0,       // 空闲：等待 ESP32 发送启动命令
    OTA_STATE_INIT_ERASE,     // 初始化：解析大小并擦除 Flash 
    OTA_STATE_WAIT_DATA,      // 接收：等待 DMA 数据块并计算 CRC，正确则写入 Flash
    OTA_STATE_FINISH,         // 完成：写入 Bootloader 标志位并准备重启
    OTA_STATE_ERROR           // 错误：处理异常
} OTA_StateTypeDef;

// 定义 OTA 上下文(Context)结构体，用于管理全局 OTA 过程
typedef struct {
    OTA_StateTypeDef state;      // 当前状态
    uint32_t fw_total_size;      // 预期的固件总大小
    uint32_t fw_received_size;   // 已接收的固件大小
    uint32_t flash_write_addr;   // 当前向 Flash 写入的地址
    uint32_t timeout_cnt;        // 超时计数器
} OTA_ContextTypeDef;

// 外部引用的 OTA 句柄
extern OTA_ContextTypeDef g_ota;

// ================== 函数声明 ==================
void OTA_Init(void);
void OTA_Send_ACK(void);
void OTA_Send_NACK(void);
uint16_t CRC16_Calculate(uint8_t *data, uint16_t length);

uint8_t OTA_Erase_Download_Area(uint32_t total_size);
uint8_t OTA_Write_Flash(uint32_t offset, uint8_t *data, uint16_t len);
void OTA_Set_Flag_And_Reset(uint32_t firmware_len);

#endif