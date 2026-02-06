#ifndef __STMFLASH_H__
#define __STMFLASH_H__

#include "main.h"

// 定义备份区(Download)的起始地址：Sector 8
#define OTA_DOWNLOAD_ADDR  0x08080000 

// STM32F407 Sector 8 的大小是 128KB，Sector 9 也是 128KB
// 假设你的固件最大不超过 256KB，我们擦除 Sector 8 和 9 即可
void STMFLASH_Erase_OTA_Area(void);
void STMFLASH_Write(uint32_t WriteAddr, uint32_t *pBuffer, uint32_t NumToWrite);

#endif
