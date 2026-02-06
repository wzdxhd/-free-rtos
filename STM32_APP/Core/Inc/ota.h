/*
 * ota.h
 * 统一管理Flash地址，增加状态机定义
 */
#ifndef __OTA_H
#define __OTA_H

#include "main.h"
#include <string.h>
#include <stdio.h>

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
#define OTA_PACKET_MAX_SIZE     1024 
#define OTA_TIMEOUT_MS          100  // 接收数据包的超时时间（毫秒）

// OTA 状态枚举
typedef enum {
    OTA_IDLE = 0,
    OTA_READY,   // 收到 Start 命令
    OTA_RUNNING, // 正在接收数据
    OTA_DONE,
    OTA_ERROR
} OTA_State_t;

// ================== 函数声明 ==================
void OTA_Send_ACK(void);
uint8_t OTA_Erase_Download_Area(uint32_t total_size);
uint8_t OTA_Write_Flash(uint32_t offset, uint8_t *data, uint16_t len);
void OTA_Set_Flag_And_Reset(uint32_t firmware_len);

#endif