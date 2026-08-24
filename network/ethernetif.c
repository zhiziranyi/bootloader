/**
 * Ethernet Interface - HAL to LWIP glue layer
 * STM32F407 + LAN8720 RMII, NO_SYS=1 bare-metal
 * Uses STM32 HAL v2 ETH API (polling mode)
 */

#include "lwip/opt.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "ethernetif.h"
#include "eth_init.h"
#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* A stalled DMA descriptor must not prevent DHCP from reaching its timeout. */
#define ETH_TX_TIMEOUT_MS  100U

/* ETH handle */
ETH_HandleTypeDef heth;

/* Set once the ETH peripheral has been initialized (used by jump.c
 * to decide whether ETH de-initialization is required). */
uint8_t eth_initialized = 0;

/* HAL calls this hook before touching ETH registers.  The default weak
 * implementation does nothing, which leaves the MAC/DMA clocks disabled. */
void HAL_ETH_MspInit(ETH_HandleTypeDef *heth)
{
    (void)heth;
    __HAL_RCC_ETH_CLK_ENABLE();
    HAL_NVIC_SetPriority(ETH_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(ETH_IRQn);
}

/* DMA TX/RX descriptors - must be in normal SRAM (not CCM) */
__ALIGN_BEGIN ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT] __ALIGN_END;
__ALIGN_BEGIN ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT] __ALIGN_END;

/* RX buffers */
__ALIGN_BEGIN uint8_t Rx_Buff[ETH_RX_DESC_CNT][ETH_RX_BUF_SIZE] __ALIGN_END;
static uint32_t rx_alloc_index = 0;

/* Most LAN8720 boards strap PHYAD0 low (address 0), but inexpensive
 * modules do not all use the same strap.  Keep the detected address so
 * link checks and MDIO access follow the actual hardware. */
static uint32_t phy_address = LAN8742A_PHY_ADDRESS;

/* USE_HAL_ETH_REGISTER_CALLBACKS is disabled in this HAL configuration.
 * Override the weak HAL hook directly, otherwise ETH_UpdateDescriptor()
 * receives a NULL buffer and programs DESC2 as zero. */
void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    *buff = &Rx_Buff[rx_alloc_index][0];
    rx_alloc_index = (rx_alloc_index + 1U) % ETH_RX_DESC_CNT;
}

/* HAL returns the DMA buffer through this chain.  low_level_input copies it
 * into lwIP-owned storage before giving the frame to the network stack. */
void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
    if (*pStart == NULL) {
        *pStart = buff;
    }
    *pEnd = buff;
    (void)Length;
}

/* TX buffers point into lwIP pbufs and need no separate release. */
void HAL_ETH_TxFreeCallback(uint32_t *buff)
{
    (void)buff;
}

static void eth_report_phy_link(void)
{
    uint32_t bsr = 0;
    if (HAL_ETH_ReadPHYRegister(&heth, phy_address, 0x01, &bsr) != HAL_OK) {
        printf("[ETH] PHY read failed (err=0x%08lX)\r\n",
               (unsigned long)heth.ErrorCode);
        return;
    }

    /* A floating MDIO line is sampled as all ones.  Before reporting an
     * electrical fault, scan the full 5-bit PHY address space once. */
    if (bsr == 0x0000U || bsr == 0xFFFFU) {
        bool found = false;
        for (uint32_t address = 0U; address < 32U; address++) {
            uint32_t candidate = 0;
            if (HAL_ETH_ReadPHYRegister(&heth, address, 0x01, &candidate) == HAL_OK &&
                candidate != 0x0000U && candidate != 0xFFFFU) {
                phy_address = address;
                bsr = candidate;
                found = true;
                printf("[ETH] PHY found at addr %lu (BSR=0x%04lX)\r\n",
                       (unsigned long)phy_address, (unsigned long)bsr);
                break;
            }
        }
        if (!found) {
            printf("[ETH] PHY scan found no response (default BSR=0x%04lX)\r\n",
                   (unsigned long)bsr);
            return;
        }
    }

    printf("[ETH] PHY link %s (BSR=0x%04lX)\r\n",
           (bsr & 0x0004U) ? "UP" : "DOWN", (unsigned long)bsr);
}

/* The F407 DMA reports an RX-descriptor write fault.  Print the hardware
 * cursor and descriptor chain so a subsequent trace can distinguish an
 * invalid list address from a driver-state problem. */
static void eth_report_dma_fault(void)
{
    printf("[ETH] RX desc base=0x%08lX current=0x%08lX TX base=0x%08lX\r\n",
           (unsigned long)heth.Instance->DMARDLAR,
           (unsigned long)heth.Instance->DMACHRDR,
           (unsigned long)heth.Instance->DMATDLAR);
    printf("[ETH] RXD0 @0x%08lX d0=%08lX d1=%08lX d2=%08lX d3=%08lX\r\n",
           (unsigned long)&DMARxDscrTab[0],
           (unsigned long)DMARxDscrTab[0].DESC0,
           (unsigned long)DMARxDscrTab[0].DESC1,
           (unsigned long)DMARxDscrTab[0].DESC2,
           (unsigned long)DMARxDscrTab[0].DESC3);
}

/**
 * Low-level output: transmit a pbuf chain
 */
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    struct pbuf *q;
    ETH_BufferTypeDef tx_buffer[4];
    uint32_t framelength = 0;
    uint32_t buf_idx = 0;

    memset(tx_buffer, 0, sizeof(tx_buffer));

    /* Copy pbuf chain into TX buffers */
    for (q = p; q != NULL && buf_idx < 4; q = q->next) {
        tx_buffer[buf_idx].buffer = (uint8_t *)q->payload;
        tx_buffer[buf_idx].len = q->len;
        tx_buffer[buf_idx].next = (q->next != NULL) ? &tx_buffer[buf_idx + 1] : NULL;
        framelength += q->len;
        buf_idx++;
    }

    /* Configure Tx */
    ETH_TxPacketConfigTypeDef txConfig;
    memset(&txConfig, 0, sizeof(txConfig));
    txConfig.Length = framelength;
    txConfig.TxBuffer = tx_buffer;
    /* lwIP emits software IP/UDP/TCP/ICMP checksums in this project.
     * Do not let the F4 MAC overwrite them: this otherwise produces packets
     * that a DHCP server can silently discard. */
    txConfig.ChecksumCtrl = ETH_CHECKSUM_DISABLE;
    txConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;

    /* Release any pending TX packets */
    HAL_ETH_ReleaseTxPacket(&heth);

    HAL_StatusTypeDef ret = HAL_ETH_Transmit(&heth, &txConfig, ETH_TX_TIMEOUT_MS);
    if (ret != HAL_OK) {
        printf("[ETH] TX failed (err=0x%08lX dma=0x%08lX dmasr=0x%08lX)\r\n",
               (unsigned long)heth.ErrorCode,
               (unsigned long)heth.DMAErrorCode,
               (unsigned long)heth.Instance->DMASR);
        eth_report_dma_fault();
    } else {
        printf("[ETH] TX queued (%lu bytes)\r\n", (unsigned long)framelength);
    }
    return (ret == HAL_OK) ? ERR_OK : ERR_IF;
}

/**
 * Low-level input: receive a pbuf from the network
 */
static struct pbuf *low_level_input(struct netif *netif)
{
    (void)netif;
    struct pbuf *p = NULL;
    struct pbuf *q;
    uint32_t len;
    uint8_t *buffer = NULL;

    if (HAL_ETH_ReadData(&heth, (void **)&buffer) != HAL_OK || buffer == NULL) {
        return NULL;
    }

    len = heth.RxDescList.RxDataLength;
    if (len > 0U && len < 0xFFFFU) {
        p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
        if (p != NULL) {
            uint32_t offset = 0U;
            for (q = p; q != NULL && offset < len; q = q->next) {
                uint32_t copy_len = (q->len < (len - offset)) ? q->len : (len - offset);
                memcpy((uint8_t *)q->payload, buffer + offset, copy_len);
                offset += copy_len;
            }
        }
    }

    return p;
}

/**
 * Initialize the ethernet interface and hardware
 */
err_t ethernetif_init(struct netif *netif)
{
    netif->linkoutput = low_level_output;
    netif->output = etharp_output;
    netif->name[0] = 'e';
    netif->name[1] = 'n';

    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    netif->hwaddr[0] = 0x02;
    netif->hwaddr[1] = 0x00;
    netif->hwaddr[2] = 0x00;
    netif->hwaddr[3] = 0x00;
    netif->hwaddr[4] = 0x00;
    netif->hwaddr[5] = 0x01;

    netif->mtu = 1500;
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

    /* Configure ETH GPIOs and reset the LAN8720 PHY first.
     * Previously ETH_Init() was never called anywhere, so the PHY was
     * never reset and the RMII pins stayed in their reset state. */
    ETH_Init();

    /* Init ETH hardware (MAC address is consumed during HAL_ETH_Init) */
    static const uint8_t macaddr[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

    heth.Instance = ETH;
    heth.Init.MACAddr = (uint8_t *)macaddr;
    heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
    heth.Init.TxDesc = DMATxDscrTab;
    heth.Init.RxDesc = DMARxDscrTab;
    heth.Init.RxBuffLen = ETH_RX_BUF_SIZE;

    printf("[ETH] Initializing MAC...\r\n");
    if (HAL_ETH_Init(&heth) != HAL_OK) {
        printf("[ETH] HAL init failed (err=0x%08lX)\r\n",
               (unsigned long)heth.ErrorCode);
        return ERR_IF;
    }

    /* USE_HAL_ETH_REGISTER_CALLBACKS is disabled, so HAL calls the strong
     * HAL_ETH_*Callback overrides above directly. */

    /* Start ETH in polling mode */
    if (HAL_ETH_Start(&heth) != HAL_OK) {
        printf("[ETH] MAC start failed (err=0x%08lX)\r\n",
               (unsigned long)heth.ErrorCode);
        return ERR_IF;
    }

    eth_initialized = 1;

    netif->flags |= NETIF_FLAG_LINK_UP;
    printf("[ETH] MAC started\r\n");
    eth_report_phy_link();
    return ERR_OK;
}

/**
 * Should be called periodically from the main loop
 */
void ethernetif_input(struct netif *netif)
{
    struct pbuf *p;

    do {
        p = low_level_input(netif);
        if (p != NULL) {
            if (netif->input(p, netif) != ERR_OK) {
                pbuf_free(p);
            }
        }
    } while (p != NULL);
}

/**
 * Get the current link status
 */
uint32_t ethernetif_get_link_status(void)
{
    uint32_t regvalue = 0;
    HAL_ETH_ReadPHYRegister(&heth, phy_address, 0x01, &regvalue);
    return (regvalue & 0x0004) ? 1 : 0;
}

/**
 * ETH IRQ Handler - called from ETH_IRQHandler
 */
void EthernetIF_IRQHandler(void)
{
    HAL_ETH_IRQHandler(&heth);
}
