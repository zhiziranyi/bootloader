/**
 * CLI - FlashSafe Pro Bootloader
 *
 * Serial command interface for debugging and OTA operations.
 */

#include "cli.h"
#include "upgrade.h"
#include "jump.h"
#include "partition.h"
#include "config.h"
#include "pubkey.h"
#include "http_client.h"
#include "download.h"
#include "drivers/w25q64.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define RX_BUF_SIZE 128
#define TX_BUF_SIZE 256

static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;
static uint8_t line_buf[RX_BUF_SIZE];
static uint8_t line_len = 0;

extern UART_HandleTypeDef huart1;

static uint8_t ring_read(void)
{
    if (rx_head == rx_tail) return 0;
    uint8_t ch = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
    return ch;
}

static void send_str(const char *str)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)str, strlen(str), 100);
}

static void cmd_info(void)
{
    char buf[TX_BUF_SIZE];
    const boot_config_t *cfg = config_get();
    extern const char *version_string;

    snprintf(buf, sizeof(buf),
        "\r\n--- FlashSafe Pro ---\r\n"
        "Version: %s\r\n"
        "Active Slot: %c\r\n"
        "Upgrade State: %d\r\n"
        "FW: %lu.%lu.%lu\r\n"
        "Rollbacks: %lu\r\n"
        "Key: %s\r\n",
        version_string ? version_string : "1.0.0",
        cfg->active_slot == SLOT_A ? 'A' : 'B',
        cfg->upgrade_state,
        cfg->fw_version_major,
        cfg->fw_version_minor,
        cfg->fw_version_patch,
        cfg->rollback_count,
        pubkey_is_loaded() ? "loaded" : "none");
    send_str(buf);
}

static void cmd_status(void)
{
    char buf[TX_BUF_SIZE];
    const boot_config_t *cfg = config_get();

    snprintf(buf, sizeof(buf),
        "\r\n--- Upgrade Status ---\r\n"
        "State: %d\r\n"
        "Progress: %lu / %lu bytes\r\n"
        "Verify Fails: %lu\r\n"
        "Download CRC: 0x%08lX\r\n",
        cfg->upgrade_state,
        cfg->download_progress,
        cfg->download_total,
        cfg->verify_fail_count,
        (unsigned long)download_get_crc());
    send_str(buf);
}

static void cmd_help(void)
{
    send_str(
        "\r\n--- Commands ---\r\n"
        "info        - Show version and config\r\n"
        "status      - Show upgrade progress\r\n"
        "upgrade net <url> - OTA upgrade (e.g. 192.168.1.100:8080/firmware/app.pkg)\r\n"
        "rollback    - Switch active slot\r\n"
        "factory     - Factory restore\r\n"
        "key show    - Show public key status\r\n"
        "reboot      - Software reset\r\n"
        "help        - Show this list\r\n"
    );
}

static void cmd_upgrade_net(const char *args)
{
    char url[128] = {0};

    /* Parse: upgrade net <url> */
    int n = sscanf(args, "%127s", url);
    if (n < 1) {
        send_str("\r\nUsage: upgrade net <url>\r\n");
        send_str("\r\nExample: upgrade net 192.168.1.100:8080/firmware/firmware_v1.1.pkg\r\n");
        return;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "\r\nStarting upgrade from %s...\r\n", url);
    send_str(buf);

    /* Init network */
    if (http_init() != 0) {
        send_str("[ERROR] Network init failed\r\n");
        return;
    }

    /* Get file size */
    int32_t size = http_get_file_size(url, 0);
    if (size <= 0) {
        send_str("[ERROR] Cannot get firmware size\r\n");
        return;
    }

    snprintf(buf, sizeof(buf), "[INFO] Firmware size: %ld bytes\r\n", (long)size);
    send_str(buf);

    /* Start download */
    download_init();
    if (download_start(url, 0, (uint32_t)size) != 0) {
        send_str("[ERROR] Download start failed\r\n");
        return;
    }

    /* Process download chunks */
    while (!download_is_complete()) {
        download_process();
        HAL_Delay(10);

        /* Print progress every ~10% */
        static uint32_t last_pct = 0;
        uint32_t pct = download_get_progress();
        if (pct >= last_pct + 10) {
            snprintf(buf, sizeof(buf), "[DL] %lu%%\r\n", (unsigned long)pct);
            send_str(buf);
            last_pct = pct;
        }
    }

    send_str("\r\n[OK] Download complete\r\n");

    /* Verify right away so the user gets immediate feedback */
    send_str("[INFO] Verifying signature...\r\n");
    int vret = upgrade_verify();
    if (vret == 0) {
        send_str("[OK] Firmware verified. Type 'reboot' to install.\r\n");
    } else {
        snprintf(buf, sizeof(buf),
                 "[ERROR] Verification failed (%d). Firmware rejected.\r\n",
                 vret);
        send_str(buf);
    }
}

static void cmd_key_show(void)
{
    char buf[128];
    if (pubkey_is_loaded()) {
        const uint8_t *pk = pubkey_get();
        snprintf(buf, sizeof(buf),
            "\r\nPublic key: loaded\r\n"
            "  %02X%02X%02X%02X...%02X%02X%02X%02X\r\n",
            pk[0], pk[1], pk[2], pk[3],
            pk[60], pk[61], pk[62], pk[63]);
        send_str(buf);
    } else {
        send_str("\r\nNo public key loaded\r\n");
    }
}

static void process_line(void)
{
    if (line_len == 0) return;

    /* A standalone x is the documented CLI wake-up byte.  Keep the full
     * first command intact, but do not print an error for this convenience. */
    if (line_len == 1 && line_buf[0] == 'x') {
        line_len = 0;
        memset(line_buf, 0, sizeof(line_buf));
        return;
    }

    if (strncmp((char *)line_buf, "info", 4) == 0) {
        cmd_info();
    } else if (strncmp((char *)line_buf, "status", 6) == 0) {
        cmd_status();
    } else if (strncmp((char *)line_buf, "upgrade net ", 12) == 0) {
        cmd_upgrade_net((char *)line_buf + 12);
    } else if (strncmp((char *)line_buf, "rollback", 8) == 0) {
        upgrade_rollback();
        send_str("\r\nRolled back to other slot\r\n");
    } else if (strncmp((char *)line_buf, "factory", 7) == 0) {
        send_str("\r\nFactory restore...\r\n");
        if (upgrade_factory_restore() == 0) {
            send_str("[OK] Factory restore done, rebooting...\r\n");
            HAL_Delay(100);
            NVIC_SystemReset();
        } else {
            send_str("[ERROR] Factory restore failed\r\n");
        }
    } else if (strncmp((char *)line_buf, "key show", 8) == 0) {
        cmd_key_show();
    } else if (strncmp((char *)line_buf, "reboot", 6) == 0) {
        send_str("\r\nRebooting...\r\n");
        HAL_Delay(100);
        NVIC_SystemReset();
    } else if (strncmp((char *)line_buf, "help", 4) == 0) {
        cmd_help();
    } else {
        send_str("\r\nUnknown command. Type 'help'.\r\n");
    }

    line_len = 0;
    memset(line_buf, 0, sizeof(line_buf));
}

void cli_init(void)
{
    rx_head = 0;
    rx_tail = 0;
    line_len = 0;
    memset(line_buf, 0, sizeof(line_buf));

    HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_buf[rx_head], 1);
}

bool cli_has_input(void)
{
    return (rx_head != rx_tail);
}

void cli_process(void)
{
    uint8_t ch;
    while ((ch = ring_read()) != 0) {
        if (ch == '\r' || ch == '\n') {
            process_line();
            send_str("> ");
        } else if (ch == '\b' || ch == 0x7F) {
            if (line_len > 0) {
                line_len--;
                line_buf[line_len] = 0;
            }
        } else if (line_len < RX_BUF_SIZE - 1) {
            line_buf[line_len++] = ch;
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        rx_head = (rx_head + 1) % RX_BUF_SIZE;
        HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_buf[rx_head], 1);
    }
}
