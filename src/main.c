/**
 * FlashSafe Pro Bootloader - Main Entry Point
 * STM32F407ZGT6
 *
 * Boot flow:
 * 1. HAL init + clock config
 * 2. Peripheral init (USART1, SPI3, GPIO)
 * 3. W25Q64 init + read ID
 * 4. ECDSA / embedded public key init
 * 5. Config read from flash
 * 6. CLI check (timeout for user input)
 * 7. Upgrade state machine (resume/verify/swap)
 * 8. Slot validation with automatic rollback
 * 9. Jump to active App
 */

#include "stm32f4xx_hal.h"
#include "partition.h"
#include "config.h"
#include "jump.h"
#include "upgrade.h"
#include "cli.h"
#include "ecdsa_verify.h"
#include "pubkey.h"
#include "drivers/w25q64.h"
#include "http_client.h"
#include "download.h"
#include "boot_display.h"
#include <stdio.h>
#include <string.h>

/* Global handles */
UART_HandleTypeDef huart1;
SPI_HandleTypeDef hspi3;

/* Global config and version */
const char *version_string = "1.1.0";

/* LED pin - adjust based on your board */
#define LED_PORT    GPIOB
#define LED_PIN     GPIO_PIN_0

/* CLI timeout for user input (ms) */
#define CLI_TIMEOUT_MS  3000

/* Private function prototypes */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_SPI3_Init(void);

/* Printf redirect to USART1 */
#ifdef __GNUC__
int _write(int fd, char *ptr, int len)
{
    (void)fd;
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}
#else
int fputc(int ch, FILE *f)
{
    (void)f;
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
#endif

/* LWIP sys_now - returns milliseconds since boot */
uint32_t sys_now(void)
{
    return HAL_GetTick();
}

int main(void)
{
    /* HAL init */
    HAL_Init();

    /* Configure system clock */
    SystemClock_Config();

    /* Initialize peripherals */
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_SPI3_Init();
    boot_display_init();
    boot_display_show(NULL, "START");

    /* LED on during boot */
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);

    /* Print banner */
    printf("\r\n========================================\r\n");
    printf("  FlashSafe Pro Bootloader v%s\r\n", version_string);
    printf("  STM32F407ZGT6 @ 168MHz\r\n");
    printf("========================================\r\n");

    /* Initialize W25Q64 */
    W25Q64_Init();
    uint32_t flash_id = W25Q64_ReadID();
    printf("[INIT] W25Q64 ID: 0x%06lX\r\n", (unsigned long)flash_id);
    boot_display_show(NULL, "FLASH OK");

    /* Initialize security */
    ecdsa_init();
    pubkey_init();

    /* Initialize config */
    boot_config_t cfg;
    config_read(&cfg);
    config_print(&cfg);
    boot_display_show(&cfg, "CONFIG OK");

    /* Initialize CLI */
    cli_init();

    /* CLI timeout - wait for user commands */
    printf("[CLI] Press any key within %dms for CLI mode...\r\n", CLI_TIMEOUT_MS);
    boot_display_show(&cfg, "CLI WAIT");
    uint32_t start = HAL_GetTick();
    bool cli_mode = false;
    while ((HAL_GetTick() - start) < CLI_TIMEOUT_MS) {
        /* Do not consume bytes here: any received byte selects CLI mode. */
        if (cli_has_input()) {
            cli_mode = true;
            break;
        }
    }
    if (cli_mode) {
        printf("[CLI] Entering CLI mode. Type 'help' for commands.\r\n");
        boot_display_show(config_get(), "CLI MODE");
        while (1) {
            cli_process();
            HAL_Delay(10);
        }
    }

    /* Upgrade state machine */
    upgrade_init();

    cfg = *config_get();
    if (cfg.upgrade_state == UPGRADE_DOWNLOADING) {
        printf("[UPGRADE] Resuming download...\r\n");
        boot_display_show(&cfg, "RESUME DL");
        if (http_init() == 0) {
            download_init();
            if (download_resume(cfg.upgrade_url, 0) == 0) {
                while (!download_is_complete() && !download_has_failed()) {
                    download_process();
                    HAL_Delay(10);
                }
                if (download_has_failed()) {
                    printf("[UPGRADE] Resume download failed; keeping current slot\r\n");
                }
            }
        } else {
            printf("[UPGRADE] Network init failed, cannot resume. "
                   "Use CLI: 'upgrade net <url>'\r\n");
        }
    }

    /* Verify + swap if a completed download is pending */
    upgrade_check_and_run();

    /* Boot slot selection with automatic rollback */
    cfg = *config_get();
    active_slot_t slot = cfg.active_slot;

    if (cfg.upgrade_state == UPGRADE_TRIAL && cfg.pending_slot != SLOT_NONE) {
        if (cfg.trial_boot_count == 0 && upgrade_slot_has_valid_app(cfg.pending_slot)) {
            config_increment_trial_boot_count();
            slot = cfg.pending_slot;
            printf("[BOOT] Trial boot slot %c\r\n", slot == SLOT_A ? 'A' : 'B');
            boot_display_show(config_get(), "TRIAL BOOT");
        } else {
            config_clear_pending_as_failed();
            printf("[BOOT] Trial was not confirmed; reverting to slot %c\r\n",
                   slot == SLOT_A ? 'A' : 'B');
            boot_display_show(config_get(), "ROLLBACK");
        }
    }

    if (!upgrade_slot_has_valid_app(slot)) {
        printf("[BOOT] Active slot %c invalid\r\n",
               slot == SLOT_A ? 'A' : 'B');
        active_slot_t other = (slot == SLOT_A) ? SLOT_B : SLOT_A;

        if (upgrade_slot_has_valid_app(other)) {
            printf("[BOOT] Rolling back to slot %c\r\n",
                   other == SLOT_A ? 'A' : 'B');
            boot_config_t tmp = cfg;
            tmp.active_slot = other;
            tmp.rollback_count++;
            tmp.upgrade_state = UPGRADE_ROLLBACK;
            config_write(&tmp);
            slot = other;
            boot_display_show(config_get(), "ROLLBACK");
        } else if (upgrade_factory_restore() == 0) {
            printf("[BOOT] Factory image restored, booting slot A\r\n");
            slot = SLOT_A;
            boot_display_show(config_get(), "FACTORY OK");
        } else {
            printf("[BOOT] No valid app in either slot. Entering CLI mode.\r\n");
            boot_display_show(config_get(), "NO APP");
            while (1) {
                cli_process();
                HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
                HAL_Delay(200);
            }
        }
    }

    /* Jump to the active application */
    uint32_t app_addr = partition_get_slot_address(slot);
    printf("[BOOT] Jumping to slot %c @ 0x%08lX...\r\n",
           slot == SLOT_A ? 'A' : 'B', (unsigned long)app_addr);
    boot_display_show(config_get(), slot == SLOT_A ? "BOOT SLOT A" : "BOOT SLOT B");

    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    HAL_Delay(100);
    jump_to_app(app_addr);

    /* Should never reach here - LED blink to indicate error */
    printf("[BOOT] Jump failed!\r\n");
    while (1) {
        cli_process();
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        HAL_Delay(500);
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
 * GPIO Initialization
 */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

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

    /* HAL_UART_Receive_IT() in cli_init() needs this IRQ to reach its callback. */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/**
 * SPI3 Initialization
 * PC10 = SCK, PC11 = MISO, PC12 = MOSI, PD2 = CS
 */
static void MX_SPI3_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_SPI3_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    hspi3.Instance = SPI3;
    hspi3.Init.Mode = SPI_MODE_MASTER;
    hspi3.Init.Direction = SPI_DIRECTION_2LINES;
    hspi3.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi3.Init.CLKPolarity = SPI_POLARITY_HIGH;
    hspi3.Init.CLKPhase = SPI_PHASE_2EDGE;
    hspi3.Init.NSS = SPI_NSS_SOFT;
    /* SPI3 is on a jumper-wired external W25Q64 module.  APB1/4 is 10.5 MHz;
     * use APB1/32 (1.3125 MHz) so a long page-program transfer has generous
     * edge margin while keeping OTA throughput practical. */
    hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
    hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;

    if (HAL_SPI_Init(&hspi3) != HAL_OK) {
        while(1);
    }
}

/* SysTick_Handler is in stm32f4xx_it.c */
