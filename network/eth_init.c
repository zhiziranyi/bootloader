/**
 * Ethernet Hardware Initialization
 * LAN8720 PHY with RMII interface
 * PA1=REF_CLK, PA2=MDIO, PA7=CRS_DV, PB11=TX_EN, PB12=TXD0, PB13=TXD1,
 * PC1=MDC, PC4=RXD0, PC5=RXD1, PD3=RESET
 */

#include "eth_init.h"
#include "lwip/opt.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "ethernetif.h"
#include <string.h>

/* ETH handle - defined in ethernetif.c */
extern ETH_HandleTypeDef heth;

/* Private function prototypes */
static void LAN8720_HWReset(void);
static void ETH_GPIO_Init(void);

/**
 * Initialize ETH peripheral and LAN8720 PHY
 */
void ETH_Init(void)
{
    /* Configure ETH GPIO pins */
    ETH_GPIO_Init();

    /* Reset LAN8720 PHY */
    LAN8720_HWReset();
}

/**
 * Hardware reset LAN8720 via PD3
 */
static void LAN8720_HWReset(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable GPIOD clock */
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* Configure reset pin as output */
    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    /* Reset sequence */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_SET);
    HAL_Delay(50);
}

/**
 * Configure ETH GPIO pins for RMII mode
 */
static void ETH_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable GPIO clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* Enable SYSCFG clock for ETH media interface */
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* Configure RMII pins:
     * PA1 = ETH_RMII_REF_CLK
     * PA2 = ETH_RMII_MDIO
     * PA7 = ETH_RMII_CRS_DV
     */
    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB11 = ETH_RMII_TX_EN
     * PB12 = ETH_RMII_TXD0
     * PB13 = ETH_RMII_TXD1
     */
    GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PC1 = ETH_RMII_MDC
     * PC4 = ETH_RMII_RXD0
     * PC5 = ETH_RMII_RXD1
     */
    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/**
 * De-initialize ETH peripheral (call before jumping to App)
 */
void ETH_DeInit(void)
{
    HAL_ETH_DeInit(&heth);

    /* GPIO de-init */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7);
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13);
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_3);
}
