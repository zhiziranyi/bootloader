/**
 * Internal Flash Driver for STM32F407ZGT6
 */

#ifndef __FLASH_INTERNAL_H
#define __FLASH_INTERNAL_H

#include "stm32f4xx_hal.h"

/* STM32F407ZG (1MB) Flash Sector Sizes */
#define FLASH_SECTOR_0_SIZE     (16 * 1024)     /* 16KB */
#define FLASH_SECTOR_1_SIZE     (16 * 1024)     /* 16KB */
#define FLASH_SECTOR_2_SIZE     (16 * 1024)     /* 16KB */
#define FLASH_SECTOR_3_SIZE     (16 * 1024)     /* 16KB */
#define FLASH_SECTOR_4_SIZE     (64 * 1024)     /* 64KB */
#define FLASH_SECTOR_5_SIZE     (128 * 1024)    /* 128KB */
#define FLASH_SECTOR_6_SIZE     (128 * 1024)    /* 128KB */
#define FLASH_SECTOR_7_SIZE     (128 * 1024)    /* 128KB */
#define FLASH_SECTOR_8_SIZE     (128 * 1024)    /* 128KB */
#define FLASH_SECTOR_9_SIZE     (128 * 1024)    /* 128KB */
#define FLASH_SECTOR_10_SIZE    (128 * 1024)    /* 128KB */
#define FLASH_SECTOR_11_SIZE    (128 * 1024)    /* 128KB */

/* Flash sector start addresses (STM32F407ZG - 1MB) */
#define FLASH_SECTOR_0_ADDR     0x08000000
#define FLASH_SECTOR_1_ADDR     0x08004000
#define FLASH_SECTOR_2_ADDR     0x08008000
#define FLASH_SECTOR_3_ADDR     0x0800C000
#define FLASH_SECTOR_4_ADDR     0x08010000
#define FLASH_SECTOR_5_ADDR     0x08020000
#define FLASH_SECTOR_6_ADDR     0x08040000
#define FLASH_SECTOR_7_ADDR     0x08060000
#define FLASH_SECTOR_8_ADDR     0x08080000
#define FLASH_SECTOR_9_ADDR     0x080A0000
#define FLASH_SECTOR_10_ADDR    0x080C0000
#define FLASH_SECTOR_11_ADDR    0x080E0000

/* Function Prototypes */
uint32_t Flash_Internal_ReadWord(uint32_t addr);
HAL_StatusTypeDef Flash_Internal_EraseSector(uint32_t sector);
HAL_StatusTypeDef Flash_Internal_Write(uint32_t addr, const uint8_t *data, uint32_t len);
HAL_StatusTypeDef Flash_Internal_WriteWord(uint32_t addr, uint32_t word);
uint32_t Flash_Internal_GetSector(uint32_t addr);

#endif /* __FLASH_INTERNAL_H */
