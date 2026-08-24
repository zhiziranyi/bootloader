/**
 * Internal Flash Driver for STM32F407ZGT6
 * Used for config area and firmware storage
 */

#include "flash_internal.h"

static void Flash_Internal_ClearStatusFlags(void)
{
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
}

/**
 * Get sector number from address.
 *
 * STM32F407ZG has 1MB flash in 12 sectors:
 *   sectors 0-3: 16KB each, sector 4: 64KB, sectors 5-11: 128KB each.
 * The previous implementation only mapped 8 sectors, which made config
 * writes at 0x080F0000 erase the wrong sector (7 instead of 11) and then
 * program non-erased flash -> config corruption.
 */
uint32_t Flash_Internal_GetSector(uint32_t addr)
{
    if (addr < 0x08004000) return FLASH_SECTOR_0;
    if (addr < 0x08008000) return FLASH_SECTOR_1;
    if (addr < 0x0800C000) return FLASH_SECTOR_2;
    if (addr < 0x08010000) return FLASH_SECTOR_3;
    if (addr < 0x08020000) return FLASH_SECTOR_4;
    if (addr < 0x08040000) return FLASH_SECTOR_5;
    if (addr < 0x08060000) return FLASH_SECTOR_6;
    if (addr < 0x08080000) return FLASH_SECTOR_7;
    if (addr < 0x080A0000) return FLASH_SECTOR_8;
    if (addr < 0x080C0000) return FLASH_SECTOR_9;
    if (addr < 0x080E0000) return FLASH_SECTOR_10;
    return FLASH_SECTOR_11;
}

/**
 * Read a 32-bit word from flash
 */
uint32_t Flash_Internal_ReadWord(uint32_t addr)
{
    return *(__IO uint32_t *)addr;
}

/**
 * Write a 32-bit word to flash
 */
HAL_StatusTypeDef Flash_Internal_WriteWord(uint32_t addr, uint32_t word)
{
    const uint8_t data[4] = {
        (uint8_t)word,
        (uint8_t)(word >> 8),
        (uint8_t)(word >> 16),
        (uint8_t)(word >> 24)
    };
    return Flash_Internal_Write(addr, data, sizeof(data));
}

/**
 * Erase a flash sector
 */
HAL_StatusTypeDef Flash_Internal_EraseSector(uint32_t sector)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase;
    uint32_t error = 0;

    HAL_FLASH_Unlock();
    Flash_Internal_ClearStatusFlags();

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = sector;
    erase.NbSectors = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    status = HAL_FLASHEx_Erase(&erase, &error);

    HAL_FLASH_Lock();
    return status;
}

/**
 * Write data to flash using byte programming. This avoids the STM32F4
 * word-program parallelism requirement and also permits journal records to
 * remain robust if their layout changes in a future firmware revision.
 * Target area must be erased first.
 */
HAL_StatusTypeDef Flash_Internal_Write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    HAL_StatusTypeDef status = HAL_OK;

    if (data == NULL || len == 0U) {
        return (len == 0U) ? HAL_OK : HAL_ERROR;
    }

    HAL_FLASH_Unlock();
    Flash_Internal_ClearStatusFlags();

    uint32_t i = 0U;
    while (i < len) {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, addr + i, data[i]);
        if (status != HAL_OK) {
            break;
        }
        ++i;
    }

    HAL_FLASH_Lock();
    return status;
}
