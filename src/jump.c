/**
 * App Jump - FlashSafe Pro
 *
 * Validates the target image, disables all interrupts, de-initializes
 * every peripheral the bootloader touched (ETH first - its DMA
 * descriptors must be released or the app's ETH init will conflict),
 * then transfers control to the app.
 */

#include "jump.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx.h"
#include "ethernetif.h"

typedef void (*entry_fn)(void);

extern UART_HandleTypeDef huart1;
extern SPI_HandleTypeDef hspi3;
extern uint8_t eth_initialized;

static void deinit_peripherals(void)
{
    /* ---- ETH: stop DMA, deinit, drop clocks ---- */
    if (eth_initialized) {
        HAL_ETH_Stop(&heth);
        HAL_ETH_DeInit(&heth);
        HAL_ETH_MspDeInit(&heth);
        eth_initialized = 0;
    }
    __HAL_RCC_ETH_CLK_DISABLE();

    /* ---- USART1 ---- */
    HAL_UART_DeInit(&huart1);
    __HAL_RCC_USART1_CLK_DISABLE();

    /* ---- SPI3 ---- */
    HAL_SPI_DeInit(&hspi3);
    __HAL_RCC_SPI3_CLK_DISABLE();

    /* ---- Release all pins to analog (highest impedance) ---- */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;

    /* PA1=ETH REF_CLK, PA2=ETH MDIO, PA7=ETH CRS_DV,
       PA9=USART1 TX, PA10=USART1 RX */
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7 |
               GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PB0=LED, PB11=ETH TX_EN, PB12=ETH TXD0, PB13=ETH TXD1 */
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PC1=ETH MDC, PC4=ETH RXD0, PC5=ETH RXD1,
       PC10=SPI3 SCK, PC11=SPI3 MISO, PC12=SPI3 MOSI */
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 |
               GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PD2=W25Q64 CS, PD3=LAN8720 nRST */
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOD, &gpio);
}

void jump_to_app(uint32_t app_addr)
{
    uint32_t sp = *(volatile uint32_t *)app_addr;
    entry_fn reset_handler = (entry_fn)(*(volatile uint32_t *)(app_addr + 4));

    if (sp < RAM_BASE_ADDR || sp > RAM_TOP_ADDR) {
        return;
    }

    /* Disable all interrupts */
    __disable_irq();
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    /* Stop SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    deinit_peripherals();

    /* Switch vector table and stack, then go */
    SCB->VTOR = app_addr;
    __DSB();
    __ISB();
    __set_MSP(sp);
    /* The application expects normal reset-time interrupt state.  All NVIC
     * sources and SysTick were stopped above, so releasing PRIMASK here is
     * safe and lets the app's HAL tick start once it configures SysTick. */
    __enable_irq();
    reset_handler();
}
