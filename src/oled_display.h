#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define OLED_WIDTH       128U
#define OLED_HEIGHT       64U
#define OLED_SCL_PORT    GPIOB
#define OLED_SCL_PIN     GPIO_PIN_6
#define OLED_SDA_PORT    GPIOB
#define OLED_SDA_PIN     GPIO_PIN_7
#define OLED_ADDRESS_3C  0x78U  /* 7-bit address 0x3C shifted for I2C transfer */
#define OLED_ADDRESS_3D  0x7AU  /* 7-bit address 0x3D shifted for I2C transfer */

HAL_StatusTypeDef OLED_Init(void);
bool OLED_IsAvailable(void);
uint8_t OLED_GetAddress(void);
void OLED_Clear(void);
void OLED_DrawText(uint8_t x, uint8_t page, const char *text);
HAL_StatusTypeDef OLED_Refresh(void);

#endif /* OLED_DISPLAY_H */
