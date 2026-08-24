/**
 * HAL Configuration for FlashSafe Pro Bootloader
 * STM32F407ZGT6
 */

#ifndef __STM32F4xx_HAL_CONF_H
#define __STM32F4xx_HAL_CONF_H

#ifdef __cplusplus
 extern "C" {
#endif

/* ########################## Module Selection ############################## */
#define HAL_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_ETH_MODULE_ENABLED
#define HAL_SD_MODULE_ENABLED
#define HAL_CRC_MODULE_ENABLED
#define HAL_RNG_MODULE_ENABLED
#define HAL_SRAM_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED

/* ########################## HSE/HSI Values ############################## */
#if !defined (HSE_VALUE)
  #define HSE_VALUE    ((uint32_t)25000000U)
#endif

#if !defined (HSE_STARTUP_TIMEOUT)
  #define HSE_STARTUP_TIMEOUT    ((uint32_t)100U)
#endif

#if !defined (HSI_VALUE)
  #define HSI_VALUE    ((uint32_t)16000000U)
#endif

#if !defined (LSI_VALUE)
  #define LSI_VALUE    ((uint32_t)32000U)
#endif

#if !defined (LSE_VALUE)
  #define LSE_VALUE    ((uint32_t)32768U)
#endif

#if !defined (LSE_STARTUP_TIMEOUT)
  #define LSE_STARTUP_TIMEOUT    ((uint32_t)5000U)
#endif

#if !defined (EXTERNAL_CLOCK_VALUE)
  #define EXTERNAL_CLOCK_VALUE   ((uint32_t)12288000U)
#endif

/* ########################### System Configuration ######################## */
#define VDD_VALUE                    ((uint32_t)3300U)
#define TICK_INT_PRIORITY            ((uint32_t)0U)
#define USE_RTOS                     0U
#define PREFETCH_ENABLE              1U
#define INSTRUCTION_CACHE_ENABLE     1U
#define DATA_CACHE_ENABLE            1U

/* ########################## Assert Selection ############################## */
/* #define USE_FULL_ASSERT    1U */

/* Includes ------------------------------------------------------------------*/
#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32f4xx_hal_rcc.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32f4xx_hal_gpio.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32f4xx_hal_dma.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32f4xx_hal_cortex.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32f4xx_hal_flash.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32f4xx_hal_pwr.h"
#endif

#ifdef HAL_SPI_MODULE_ENABLED
  #include "stm32f4xx_hal_spi.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
  #include "stm32f4xx_hal_uart.h"
#endif

#ifdef HAL_ETH_MODULE_ENABLED
  #include "stm32f4xx_hal_eth.h"
#endif

#ifdef HAL_SD_MODULE_ENABLED
  #include "stm32f4xx_hal_sd.h"
#endif

#ifdef HAL_CRC_MODULE_ENABLED
  #include "stm32f4xx_hal_crc.h"
#endif

#ifdef HAL_RNG_MODULE_ENABLED
  #include "stm32f4xx_hal_rng.h"
#endif

#ifdef HAL_SRAM_MODULE_ENABLED
  #include "stm32f4xx_hal_sram.h"
#endif

#ifdef HAL_IWDG_MODULE_ENABLED
  #include "stm32f4xx_hal_iwdg.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* __STM32F4xx_HAL_CONF_H */
