/**
 * W25Q64 SPI Flash Driver
 * SPI3: PC10=SCK, PC11=MISO, PC12=MOSI, PD2=CS (software)
 */

#ifndef __W25Q64_H
#define __W25Q64_H

#include "stm32f4xx_hal.h"

/* W25Q64 Commands */
#define W25Q64_CMD_WRITE_ENABLE        0x06
#define W25Q64_CMD_WRITE_DISABLE       0x04
#define W25Q64_CMD_READ_STATUS_REG1    0x05
#define W25Q64_CMD_READ_STATUS_REG2    0x35
#define W25Q64_CMD_WRITE_STATUS_REG    0x01
#define W25Q64_CMD_READ_DATA           0x03
#define W25Q64_CMD_FAST_READ           0x0B
#define W25Q64_CMD_PAGE_PROGRAM        0x02
#define W25Q64_CMD_SECTOR_ERASE        0x20    /* 4KB */
#define W25Q64_CMD_BLOCK_ERASE_32K     0x52    /* 32KB */
#define W25Q64_CMD_BLOCK_ERASE_64K     0xD8    /* 64KB */
#define W25Q64_CMD_CHIP_ERASE          0xC7
#define W25Q64_CMD_POWER_DOWN          0xB9
#define W25Q64_CMD_RELEASE_PD          0xAB
#define W25Q64_CMD_DEVICE_ID           0xAB
#define W25Q64_CMD_MANUFACTURER_ID     0x90
#define W25Q64_CMD_JEDEC_ID            0x9F
#define W25Q64_CMD_UNIQUE_ID           0x4B

/* Status register bits */
#define W25Q64_STATUS_BUSY             0x01
#define W25Q64_STATUS_WEL              0x02

/* W25Q64 Parameters */
#define W25Q64_PAGE_SIZE               256
#define W25Q64_SECTOR_SIZE             4096
#define W25Q64_BLOCK_SIZE_32K          (32 * 1024)
#define W25Q64_BLOCK_SIZE_64K          (64 * 1024)
#define W25Q64_TOTAL_SIZE              (8 * 1024 * 1024)  /* 8MB */

/* Manufacturer/Device ID */
#define W25Q64_MANUFACTURER_ID         0xEF
#define W25Q64_DEVICE_ID               0x4017

/* CS Pin Port and Pin */
#define W25Q64_CS_PORT                 GPIOD
#define W25Q64_CS_PIN                  GPIO_PIN_2

/* Function Prototypes */
void W25Q64_Init(void);
uint32_t W25Q64_ReadID(void);
uint8_t W25Q64_ReadStatus(void);

void W25Q64_Read(uint32_t addr, uint8_t *buf, uint32_t len);
HAL_StatusTypeDef W25Q64_WritePage(uint32_t addr, const uint8_t *buf, uint32_t len);
HAL_StatusTypeDef W25Q64_Write(uint32_t addr, const uint8_t *buf, uint32_t len);
HAL_StatusTypeDef W25Q64_EraseSector(uint32_t addr);
HAL_StatusTypeDef W25Q64_EraseBlock32K(uint32_t addr);
HAL_StatusTypeDef W25Q64_EraseBlock64K(uint32_t addr);
HAL_StatusTypeDef W25Q64_EraseChip(void);

#endif /* __W25Q64_H */
