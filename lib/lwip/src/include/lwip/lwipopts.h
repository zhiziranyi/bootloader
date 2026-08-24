/**
 * lwipopts.h - Minimal LWIP configuration for bare-metal STM32 IPv4 only
 * with DHCP and ICMP support
 */

#ifndef LWIP_HDR_LWIPOPTS_H
#define LWIP_HDR_LWIPOPTS_H

/* Disable IPv6 */
#define LWIP_IPV6 0

/* Disable OS abstraction (bare-metal) */
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0

/* Enable DHCP client */
#define LWIP_DHCP 1

/* Enable ICMP */
#define LWIP_ICMP 1

/* Enable ARP (needed for Ethernet) */
#define LWIP_ARP 1

/* Memory settings - minimal for bootloader */
/*
 * The STM32F4 compiler emits double-word stores in dhcp_handle_ack().
 * DHCP control blocks are allocated from this heap, so 8-byte alignment
 * prevents a UsageFault on the Cortex-M4.
 */
#define MEM_ALIGNMENT 8
#define MEM_SIZE (16 * 1024)
#define MEMP_NUM_PBUF 16
#define MEMP_NUM_RAW_PCB 4
#define MEMP_NUM_UDP_PCB 4
#define MEMP_NUM_TCP_PCB 4
#define MEMP_NUM_TCP_PCB_LISTEN 2
#define MEMP_NUM_TCP_SEG 16
#define MEMP_NUM_SYS_TIMEOUT 6

/* Pbuf settings */
#define PBUF_POOL_SIZE 8
#define PBUF_POOL_BUFSIZE 1524

/* TCP settings - minimal */
#define TCP_MSS 1460
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN 8
#define TCP_WND (4 * TCP_MSS)

/* UDP settings */
#define UDP_TTL 255

/* IP settings */
#define IP_FORWARD 0
#define IP_OPTIONS_ALLOWED 0
#define IP_REASSEMBLY 0
#define IP_FRAG 0

/* ICMP settings */
#define LWIP_BROADCAST_PING 0
#define LWIP_MULTICAST_PING 0

/* DNS settings - disable for minimal bootloader */
#define LWIP_DNS 0

/* Statistics */
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 0

/* Checksum settings - use hardware if available */
#define CHECKSUM_GEN_IP 1
#define CHECKSUM_GEN_UDP 1
#define CHECKSUM_GEN_TCP 1
#define CHECKSUM_GEN_ICMP 1
#define CHECKSUM_CHECK_IP 1
#define CHECKSUM_CHECK_UDP 1
#define CHECKSUM_CHECK_TCP 1
#define CHECKSUM_CHECK_ICMP 1

/* Sequential API - disable for bare-metal */
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0

/* Debug options - disable for release */
#define LWIP_DEBUG 0

#endif /* LWIP_HDR_LWIPOPTS_H */
