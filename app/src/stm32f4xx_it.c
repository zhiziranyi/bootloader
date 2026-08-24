/**
 * Interrupt Handlers - FlashSafe Pro App
 * STM32F407ZGT6
 */

#include "stm32f4xx_hal.h"

/* Only override handlers we actually use */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* HardFault handler with debug info */
void HardFault_Handler(void)
{
    while(1);
}
