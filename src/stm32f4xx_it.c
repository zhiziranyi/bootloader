/**
 * Interrupt Handlers - FlashSafe Pro Bootloader
 * STM32F407ZGT6
 */

#include "stm32f4xx_hal.h"
#include <stdio.h>

/* External symbols */
extern void Default_Handler(void);
extern void EthernetIF_IRQHandler(void);
extern UART_HandleTypeDef huart1;

/* Only override handlers we actually use */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* Print the stacked exception context so the faulting PC can be resolved with
 * arm-none-eabi-addr2line against firmware.elf. */
void HardFault_Decode(uint32_t *stack)
{
    char message[160];
    int length = snprintf(message, sizeof(message),
                          "\r\n[FAULT] HardFault PC=0x%08lX LR=0x%08lX "
                          "CFSR=0x%08lX HFSR=0x%08lX BFAR=0x%08lX\r\n",
                          (unsigned long)stack[6],
                          (unsigned long)stack[5],
                          (unsigned long)SCB->CFSR,
                          (unsigned long)SCB->HFSR,
                          (unsigned long)SCB->BFAR);
    if (length > 0) {
        uint16_t count = (length < (int)sizeof(message)) ? (uint16_t)length :
                                                          (uint16_t)(sizeof(message) - 1U);
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)message, count, 100U);
    }
    while (1) {
    }
}

/* Pass the exception stack pointer to the C decoder.  The handler must be
 * naked so the compiler does not modify MSP before it is captured. */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4 \n"
        "ite eq     \n"
        "mrseq r0, msp \n"
        "mrsne r0, psp \n"
        "b HardFault_Decode \n");
}

/* ETH interrupt handler */
void ETH_IRQHandler(void)
{
    EthernetIF_IRQHandler();
}

/* USART1 interrupt handler - required for the CLI (HAL_UART_Receive_IT).
 * Without this the RXNE interrupt is never dispatched and CLI input is dead. */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}
