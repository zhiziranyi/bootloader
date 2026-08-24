/**
 * FlashSafe Pro Test App
 * STM32F407ZGT6
 *
 * Minimal test application at 0x08020000 (App_A).
 * Verifies bootloader jump works correctly.
 */

#include "stm32f4xx_hal.h"
#include "boot_confirm.h"
#include "app_version.h"
#include <stdio.h>
#include <string.h>

/* Global UART handle */
UART_HandleTypeDef huart1;

/* LED pin - PB0 */
#define LED_PORT    GPIOB
#define LED_PIN     GPIO_PIN_0

/* Private function prototypes */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);

/* Printf redirect to USART1 */
#ifdef __GNUC__
int _write(int fd, char *ptr, int len)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}
#else
int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
#endif

int main(void)
{
    /* Each artifact is linked for its own execution slot. */
#ifndef APP_BASE_ADDR
#error "APP_BASE_ADDR must be supplied by the PlatformIO environment"
#endif
    SCB->VTOR = APP_BASE_ADDR;

    /* HAL init */
    HAL_Init();

    /* Configure system clock */
    SystemClock_Config();

    /* Initialize peripherals */
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    if (boot_confirm_running_image() == 0) {
        printf("[APP] Trial firmware confirmed\r\n");
    }

    /* Print banner */
    printf("\r\n========================================\r\n");
    printf("  FlashSafe Pro App v%s\r\n", APP_VERSION_STRING);
    printf("  Running from 0x%08lX\r\n", (unsigned long)APP_BASE_ADDR);
    printf("  STM32F407ZGT6 @ 168MHz\r\n");
    printf("========================================\r\n");

    uint32_t counter = 0;

    /* Main loop */
    while (1)
    {
        /* Toggle LED every 500ms */
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        HAL_Delay(500);

        /* Print status every 5 seconds */
        counter++;
        if (counter >= 10)
        {
            printf("[APP] Heartbeat #%lu - still alive at 0x%08lX\r\n",
                   (unsigned long)counter,
                   (unsigned long)APP_BASE_ADDR);
            counter = 0;
        }
    }
}

/**
 * System Clock Configuration
 * HSE (8MHz) -> PLL -> 168MHz SYSCLK
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 7;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        while(1);
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        while(1);
    }
}

/**
 * GPIO Initialization - LED on PB0
 */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
}

/**
 * USART1 Initialization
 * PA9 = TX, PA10 = RX, 115200 baud
 */
static void MX_USART1_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART1_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        while(1);
    }
}
