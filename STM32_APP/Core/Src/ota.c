/*
 * ota.c
 * 重构说明：优化了擦除逻辑，确保与 ESP32 的 ACK 握手兼容
 */
#include "ota.h"

// 发送 ACK 给 ESP32
// ESP32 代码中 waitForAck 检测的是 "ACK_OK"
void OTA_Send_ACK(void) {
    HAL_UART_Transmit(OTA_UART_HANDLE, (uint8_t*)"ACK_OK", 6, 1000);
}

// 擦除下载区域
// 注意：ESP32 的超时时间是 5000ms，如果擦除时间过长可能导致 ESP32 超时
// 这里按需擦除，尽量减少耗时
uint8_t OTA_Erase_Download_Area(uint32_t size) {
    FLASH_EraseInitTypeDef EraseInit;
    uint32_t SectorError;
    HAL_StatusTypeDef status;

    HAL_FLASH_Unlock();
    
    EraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInit.Sector = OTA_DOWNLOAD_SECTOR_START; // Sector 8
    
    // 计算需要擦除的扇区数 (假设 Sector 8-11 均为 128KB)
    // 1个扇区(128KB)够用吗？
    if (size <= (128 * 1024)) {
        EraseInit.NbSectors = 1;
    } else if (size <= (256 * 1024)) {
        EraseInit.NbSectors = 2;
    } else {
        EraseInit.NbSectors = 3; // 支持到 384KB
    }

    status = HAL_FLASHEx_Erase(&EraseInit, &SectorError);
    
    HAL_FLASH_Lock();

    return (status == HAL_OK) ? 1 : 0;
}

// 写入 Flash (保持原逻辑，增加了对齐检查)
uint8_t OTA_Write_Flash(uint32_t address_offset, uint8_t *data, uint16_t len) {
    if (len == 0) return 1;

    HAL_FLASH_Unlock();
    uint32_t base_addr = OTA_DOWNLOAD_ADDR + address_offset;
    HAL_StatusTypeDef status = HAL_OK;

    for (int i = 0; i < len; i += 4) {
        uint32_t write_data = 0xFFFFFFFF;
        uint16_t remain = len - i;
        
        if (remain >= 4) {
            memcpy(&write_data, data + i, 4);
        } else {
            memcpy(&write_data, data + i, remain);
        }

        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, base_addr + i, write_data);
        if (status != HAL_OK) break;
    }

    HAL_FLASH_Lock();
    return (status == HAL_OK) ? 1 : 0;
}

// 写入标志位并重启
void OTA_Set_Flag_And_Reset(uint32_t firmware_len) {
    FLASH_EraseInitTypeDef EraseInit;
    uint32_t SectorError;

    __disable_irq(); // 关中断，防止打断
    HAL_FLASH_Unlock();

    // 擦除标志位扇区
    EraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInit.Sector = OTA_FLAG_SECTOR; 
    EraseInit.NbSectors = 1;

    if (HAL_FLASHEx_Erase(&EraseInit, &SectorError) == HAL_OK) {
        // 写入 Magic Number 和 长度
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, OTA_FLAG_ADDR, OTA_FLAG_MAGIC);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, OTA_FLAG_ADDR + 4, firmware_len);
    }

    HAL_FLASH_Lock();
    __DSB(); 
    HAL_NVIC_SystemReset();
}