/**
 * W25Q64 SPI flash driver.
 *
 * PC10=SCK, PC11=MISO, PC12=MOSI and PD2=CS are driven as SPI mode 0 GPIO.
 * The OTA path shares this MCU with Ethernet DMA. Keeping the entire flash
 * transaction in this small synchronous driver avoids dependence on the HAL
 * SPI full-duplex state machine while CS is held low for a page program.
 */

#include "w25q64.h"
#include <stdbool.h>
#include <stdio.h>

extern SPI_HandleTypeDef hspi3;

#define W25Q64_SCK_PORT       GPIOC
#define W25Q64_SCK_PIN        GPIO_PIN_10
#define W25Q64_MISO_PORT      GPIOC
#define W25Q64_MISO_PIN       GPIO_PIN_11
#define W25Q64_MOSI_PORT      GPIOC
#define W25Q64_MOSI_PIN       GPIO_PIN_12

#define W25Q64_CS_LOW()       HAL_GPIO_WritePin(W25Q64_CS_PORT, W25Q64_CS_PIN, GPIO_PIN_RESET)
#define W25Q64_CS_HIGH()      HAL_GPIO_WritePin(W25Q64_CS_PORT, W25Q64_CS_PIN, GPIO_PIN_SET)
#define W25Q64_SCK_LOW()      HAL_GPIO_WritePin(W25Q64_SCK_PORT, W25Q64_SCK_PIN, GPIO_PIN_RESET)
#define W25Q64_SCK_HIGH()     HAL_GPIO_WritePin(W25Q64_SCK_PORT, W25Q64_SCK_PIN, GPIO_PIN_SET)

#define W25Q64_PAGE_PROGRAM_TIMEOUT_MS  100U
#define W25Q64_SECTOR_ERASE_TIMEOUT_MS   1000U
#define W25Q64_BLOCK_ERASE_TIMEOUT_MS    5000U
#define W25Q64_CHIP_ERASE_TIMEOUT_MS     200000U

static void W25Q64_ClockDelay(void)
{
    for (volatile uint32_t i = 0U; i < 16U; ++i) {
        __NOP();
    }
}

/* Mode 0: data changes while SCK is low and is sampled on the rising edge. */
static uint8_t W25Q64_TransferByte(uint8_t tx)
{
    uint8_t rx = 0U;

    for (int bit = 7; bit >= 0; --bit) {
        HAL_GPIO_WritePin(W25Q64_MOSI_PORT, W25Q64_MOSI_PIN,
                          ((tx & (uint8_t)(1U << bit)) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        W25Q64_ClockDelay();
        W25Q64_SCK_HIGH();
        W25Q64_ClockDelay();
        if (HAL_GPIO_ReadPin(W25Q64_MISO_PORT, W25Q64_MISO_PIN) == GPIO_PIN_SET) {
            rx |= (uint8_t)(1U << bit);
        }
        W25Q64_SCK_LOW();
        W25Q64_ClockDelay();
    }
    return rx;
}

static void W25Q64_SendCommand(uint8_t cmd)
{
    W25Q64_CS_LOW();
    (void)W25Q64_TransferByte(cmd);
    W25Q64_CS_HIGH();
}

static void W25Q64_SendAddressCommand(uint8_t cmd, uint32_t addr)
{
    W25Q64_CS_LOW();
    (void)W25Q64_TransferByte(cmd);
    (void)W25Q64_TransferByte((uint8_t)(addr >> 16));
    (void)W25Q64_TransferByte((uint8_t)(addr >> 8));
    (void)W25Q64_TransferByte((uint8_t)addr);
    W25Q64_CS_HIGH();
}

void W25Q64_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Take ownership of these pins from the previous hardware-SPI setup. */
    (void)HAL_SPI_DeInit(&hspi3);
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    gpio.Pin = W25Q64_SCK_PIN | W25Q64_MOSI_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = W25Q64_MISO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = W25Q64_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(W25Q64_CS_PORT, &gpio);

    W25Q64_SCK_LOW();
    HAL_GPIO_WritePin(W25Q64_MOSI_PORT, W25Q64_MOSI_PIN, GPIO_PIN_RESET);
    W25Q64_CS_HIGH();

    W25Q64_SendCommand(W25Q64_CMD_RELEASE_PD);
    HAL_Delay(1U);
}

static bool W25Q64_WaitBusy(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while ((W25Q64_ReadStatus() & W25Q64_STATUS_BUSY) != 0U) {
        if ((HAL_GetTick() - start) >= timeout_ms) {
            return false;
        }
        HAL_Delay(1U);
    }
    return true;
}

static bool W25Q64_WriteEnable(void)
{
    W25Q64_SendCommand(W25Q64_CMD_WRITE_ENABLE);
    uint8_t flash_status = W25Q64_ReadStatus();
    if ((flash_status & W25Q64_STATUS_WEL) == 0U) {
        printf("[W25] WREN rejected status=0x%02X\r\n", flash_status);
        return false;
    }
    return true;
}

uint8_t W25Q64_ReadStatus(void)
{
    W25Q64_CS_LOW();
    (void)W25Q64_TransferByte(W25Q64_CMD_READ_STATUS_REG1);
    uint8_t status = W25Q64_TransferByte(0xFFU);
    W25Q64_CS_HIGH();
    return status;
}

uint32_t W25Q64_ReadID(void)
{
    W25Q64_CS_LOW();
    (void)W25Q64_TransferByte(W25Q64_CMD_JEDEC_ID);
    uint8_t manufacturer = W25Q64_TransferByte(0xFFU);
    uint8_t memory_type = W25Q64_TransferByte(0xFFU);
    uint8_t capacity = W25Q64_TransferByte(0xFFU);
    W25Q64_CS_HIGH();
    return ((uint32_t)manufacturer << 16) | ((uint32_t)memory_type << 8) | capacity;
}

void W25Q64_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len == 0U) {
        return;
    }

    W25Q64_CS_LOW();
    (void)W25Q64_TransferByte(W25Q64_CMD_READ_DATA);
    (void)W25Q64_TransferByte((uint8_t)(addr >> 16));
    (void)W25Q64_TransferByte((uint8_t)(addr >> 8));
    (void)W25Q64_TransferByte((uint8_t)addr);
    for (uint32_t i = 0U; i < len; ++i) {
        buf[i] = W25Q64_TransferByte(0xFFU);
    }
    W25Q64_CS_HIGH();
}

HAL_StatusTypeDef W25Q64_WritePage(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len == 0U || len > W25Q64_PAGE_SIZE) {
        return HAL_ERROR;
    }

    uint32_t page_offset = addr % W25Q64_PAGE_SIZE;
    if ((page_offset + len) > W25Q64_PAGE_SIZE) {
        len = W25Q64_PAGE_SIZE - page_offset;
    }
    if (!W25Q64_WriteEnable()) {
        return HAL_ERROR;
    }

    /* Page Program holds CS continuously from instruction through final byte. */
    W25Q64_CS_LOW();
    (void)W25Q64_TransferByte(W25Q64_CMD_PAGE_PROGRAM);
    (void)W25Q64_TransferByte((uint8_t)(addr >> 16));
    (void)W25Q64_TransferByte((uint8_t)(addr >> 8));
    (void)W25Q64_TransferByte((uint8_t)addr);
    for (uint32_t i = 0U; i < len; ++i) {
        (void)W25Q64_TransferByte(buf[i]);
    }
    W25Q64_CS_HIGH();

    if (!W25Q64_WaitBusy(W25Q64_PAGE_PROGRAM_TIMEOUT_MS)) {
        printf("[W25] PP timeout addr=0x%06lX status=0x%02X\r\n",
               (unsigned long)addr, W25Q64_ReadStatus());
        return HAL_TIMEOUT;
    }
    return HAL_OK;
}

HAL_StatusTypeDef W25Q64_Write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (buf == NULL && len != 0U) {
        return HAL_ERROR;
    }
    while (len > 0U) {
        uint32_t page_offset = addr % W25Q64_PAGE_SIZE;
        uint32_t bytes_left = W25Q64_PAGE_SIZE - page_offset;
        uint32_t chunk = (len < bytes_left) ? len : bytes_left;
        HAL_StatusTypeDef status = W25Q64_WritePage(addr, buf, chunk);
        if (status != HAL_OK) {
            return status;
        }
        addr += chunk;
        buf += chunk;
        len -= chunk;
    }
    return HAL_OK;
}

HAL_StatusTypeDef W25Q64_EraseSector(uint32_t addr)
{
    addr &= ~(W25Q64_SECTOR_SIZE - 1U);
    if (!W25Q64_WriteEnable()) {
        return HAL_ERROR;
    }
    W25Q64_SendAddressCommand(W25Q64_CMD_SECTOR_ERASE, addr);
    return W25Q64_WaitBusy(W25Q64_SECTOR_ERASE_TIMEOUT_MS) ? HAL_OK : HAL_TIMEOUT;
}

HAL_StatusTypeDef W25Q64_EraseBlock32K(uint32_t addr)
{
    addr &= ~(W25Q64_BLOCK_SIZE_32K - 1U);
    if (!W25Q64_WriteEnable()) {
        return HAL_ERROR;
    }
    W25Q64_SendAddressCommand(W25Q64_CMD_BLOCK_ERASE_32K, addr);
    return W25Q64_WaitBusy(W25Q64_BLOCK_ERASE_TIMEOUT_MS) ? HAL_OK : HAL_TIMEOUT;
}

HAL_StatusTypeDef W25Q64_EraseBlock64K(uint32_t addr)
{
    addr &= ~(W25Q64_BLOCK_SIZE_64K - 1U);
    if (!W25Q64_WriteEnable()) {
        return HAL_ERROR;
    }
    W25Q64_SendAddressCommand(W25Q64_CMD_BLOCK_ERASE_64K, addr);
    if (!W25Q64_WaitBusy(W25Q64_BLOCK_ERASE_TIMEOUT_MS)) {
        printf("[W25] BE64 timeout addr=0x%06lX status=0x%02X\r\n",
               (unsigned long)addr, W25Q64_ReadStatus());
        return HAL_TIMEOUT;
    }
    return HAL_OK;
}

HAL_StatusTypeDef W25Q64_EraseChip(void)
{
    if (!W25Q64_WriteEnable()) {
        return HAL_ERROR;
    }
    W25Q64_SendCommand(W25Q64_CMD_CHIP_ERASE);
    return W25Q64_WaitBusy(W25Q64_CHIP_ERASE_TIMEOUT_MS) ? HAL_OK : HAL_TIMEOUT;
}
