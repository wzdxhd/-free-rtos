#include "stmflash.h"
#include <stdio.h>

// 获取某个地址所在的 Flash Sector 编号
static uint32_t GetSector(uint32_t Address)
{
    if((Address >= 0x08000000) && (Address < 0x08004000)) return FLASH_SECTOR_0;
    if((Address >= 0x08004000) && (Address < 0x08008000)) return FLASH_SECTOR_1;
    if((Address >= 0x08008000) && (Address < 0x0800C000)) return FLASH_SECTOR_2;
    if((Address >= 0x0800C000) && (Address < 0x08010000)) return FLASH_SECTOR_3;
    if((Address >= 0x08010000) && (Address < 0x08020000)) return FLASH_SECTOR_4;
    if((Address >= 0x08020000) && (Address < 0x08040000)) return FLASH_SECTOR_5;
    if((Address >= 0x08040000) && (Address < 0x08060000)) return FLASH_SECTOR_6;
    if((Address >= 0x08060000) && (Address < 0x08080000)) return FLASH_SECTOR_7;
    // --- 我们的目标区域 ---
    if((Address >= 0x08080000) && (Address < 0x080A0000)) return FLASH_SECTOR_8;
    if((Address >= 0x080A0000) && (Address < 0x080C0000)) return FLASH_SECTOR_9;
    if((Address >= 0x080C0000) && (Address < 0x080E0000)) return FLASH_SECTOR_10;
    if((Address >= 0x080E0000) && (Address < 0x08100000)) return FLASH_SECTOR_11;
    return FLASH_SECTOR_11;
}

// 1. 擦除备份区 (OTA开始前必须先擦除)
// 这里简单暴力地擦除 Sector 8 (128KB) 和 Sector 9 (128KB)
// 如果你的 APP 大于 256KB，请继续擦除 Sector 10
void STMFLASH_Erase_OTA_Area(void)
{
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError;

    HAL_FLASH_Unlock(); // 解锁 Flash

    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3; // 2.7V to 3.6V
    EraseInitStruct.Sector = FLASH_SECTOR_8;              // 从 Sector 8 开始
    EraseInitStruct.NbSectors = 2;                        // 擦除 2 个 Sector (8, 9)

    // 执行擦除 (会阻塞几十毫秒到几百毫秒，建议在 Task 中调用并暂停其他任务)
    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK)
    {
        printf("[Error] Flash Erase Failed! Sector: %d\r\n", SectorError);
    }
    else
    {
        printf("[Info] Flash OTA Area Erased.\r\n");
    }

    HAL_FLASH_Lock(); // 上锁
}

// 2. 写入数据到 Flash
// WriteAddr: 写入的起始地址 (必须是 4 的倍数)
// pBuffer: 数据指针 (必须是 32位指针)
// NumToWrite: 要写入的字数 (Word, 1 Word = 4 Bytes)
void STMFLASH_Write(uint32_t WriteAddr, uint32_t *pBuffer, uint32_t NumToWrite)
{
    HAL_FLASH_Unlock();

    for (uint32_t i = 0; i < NumToWrite; i++)
    {
        // 每次写入 4 字节
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, WriteAddr, pBuffer[i]) == HAL_OK)
        {
            WriteAddr += 4; // 地址后移 4 字节
        }
        else
        {
            printf("[Error] Flash Write Failed at 0x%08X\r\n", WriteAddr);
            break;
        }
    }

    HAL_FLASH_Lock();
}
