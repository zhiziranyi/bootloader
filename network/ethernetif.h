/**
 * Ethernet Interface Header - Minimal, uses HAL defines
 */

#ifndef __ETHERNETIF_H
#define __ETHERNETIF_H

#include "lwip/netif.h"
#include "stm32f4xx_hal.h"

/* Global ETH handle (defined in ethernetif.c) */
extern ETH_HandleTypeDef heth;

/* LAN8720 PHY address */
#define LAN8742A_PHY_ADDRESS       0x00

/* Function Prototypes */
err_t ethernetif_init(struct netif *netif);
void ethernetif_input(struct netif *netif);
uint32_t ethernetif_get_link_status(void);

#endif /* __ETHERNETIF_H */
