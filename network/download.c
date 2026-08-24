/**
 * Download Manager - FlashSafe Pro
 *
 * Downloads firmware to external flash (W25Q64) via HTTP with Range
 * requests. Progress and the server URL are persisted to the config
 * area so a power loss can be resumed after reboot.
 */

#include "download.h"
#include "http_client.h"
#include "config.h"
#include "partition.h"
#include "drivers/crc32.h"
#include "drivers/w25q64.h"

#include <string.h>
#include <stdio.h>

/* Download chunk size: 4KB (fits in RAM) */
#define DL_CHUNK_SIZE       (4 * 1024)

/* Maximum retries per chunk */
#define DL_MAX_RETRIES      3

/* Persist progress to flash at least every 64KB to limit sector erases */
#define DL_PERSIST_INTERVAL (64 * 1024)

/* Download states */
typedef enum {
    DL_STATE_IDLE = 0,
    DL_STATE_DOWNLOADING,
    DL_STATE_COMPLETE,
    DL_STATE_ERROR
} dl_state_t;

/* Download context */
static struct {
    dl_state_t state;
    char url[128];
    uint16_t port;
    uint32_t total_size;
    uint32_t downloaded;
    uint32_t crc;
    uint8_t retry_count;
    uint8_t chunk_buf[DL_CHUNK_SIZE];
    uint32_t last_persist;
} s_dl;

void download_init(void)
{
    memset(&s_dl, 0, sizeof(s_dl));
    s_dl.state = DL_STATE_IDLE;
    s_dl.crc = CRC32_INIT_VALUE;
}

/* Compare the package header at the two critical boundaries: bytes received
 * from TCP and bytes read back from the external flash after programming. */
static void download_report_header_crc(const char *source,
                                       const uint8_t *data, uint32_t len)
{
    if (len < sizeof(fw_header_t)) {
        return;
    }

    uint32_t stored_crc;
    memcpy(&stored_crc,
           data + sizeof(fw_header_t) - sizeof(uint32_t),
           sizeof(stored_crc));
    uint32_t calculated_crc = CRC32_Calculate(
        data, sizeof(fw_header_t) - sizeof(uint32_t));

    printf("[DL] %s header CRC calc=0x%08lX stored=0x%08lX\r\n",
           source, (unsigned long)calculated_crc, (unsigned long)stored_crc);

    for (uint32_t offset = 0; offset < sizeof(fw_header_t); offset += 16U) {
        uint32_t count = sizeof(fw_header_t) - offset;
        if (count > 16U) {
            count = 16U;
        }
        printf("[DL] %s %02lu:", source, (unsigned long)offset);
        for (uint32_t i = 0; i < count; i++) {
            printf(" %02X", data[offset + i]);
        }
        printf("\r\n");
    }
}

/* Callback for streaming download - writes to external flash */
static int download_write_chunk(const uint8_t *data, uint32_t len)
{
    uint32_t ext_addr = EXT_DOWNLOAD_BASE + s_dl.downloaded;

    if (s_dl.downloaded == 0U) {
        download_report_header_crc("RX", data, len);
    }

    if (W25Q64_Write(ext_addr, data, len) != HAL_OK) {
        printf("[DL] Flash program failed at offset %lu\r\n",
               (unsigned long)s_dl.downloaded);
        return -1;
    }

    if (s_dl.downloaded == 0U) {
        uint8_t header[sizeof(fw_header_t)];
        W25Q64_Read(EXT_DOWNLOAD_BASE, header, sizeof(header));
        download_report_header_crc("FLASH", header, sizeof(header));
    }

    s_dl.crc = CRC32_Update(s_dl.crc, data, len);
    s_dl.downloaded += len;

    return (int)len;
}

/**
 * Erase the download area before writing. W25Q64 requires erased
 * sectors before programming - without this the write produces garbage.
 */
static bool download_erase_area(uint32_t size)
{
    uint32_t blocks = (size + (64 * 1024) - 1) / (64 * 1024);
    printf("[DL] Erasing %lu x 64KB blocks for %lu bytes...\r\n",
           (unsigned long)blocks, (unsigned long)size);
    for (uint32_t i = 0; i < blocks; i++) {
        if (W25Q64_EraseBlock64K(EXT_DOWNLOAD_BASE + i * (64 * 1024)) != HAL_OK) {
            printf("[DL] Flash erase failed at block %lu\r\n", (unsigned long)i);
            return false;
        }
    }
    return true;
}

int download_start(const char *url, uint16_t port, uint32_t expected_size)
{
    if (s_dl.state == DL_STATE_DOWNLOADING) {
        printf("[DL] Already downloading\r\n");
        return -1;
    }
    if (expected_size == 0 || expected_size > EXT_DOWNLOAD_SIZE) {
        printf("[DL] Invalid expected size: %lu\r\n",
               (unsigned long)expected_size);
        return -1;
    }
    if (url == NULL || url[0] == '\0') {
        printf("[DL] No URL\r\n");
        return -1;
    }

    memset(&s_dl, 0, sizeof(s_dl));
    strncpy(s_dl.url, url, sizeof(s_dl.url) - 1);
    s_dl.url[sizeof(s_dl.url) - 1] = '\0';
    s_dl.port = port;
    s_dl.total_size = expected_size;
    s_dl.downloaded = 0;
    s_dl.crc = CRC32_INIT_VALUE;
    s_dl.retry_count = 0;
    s_dl.state = DL_STATE_DOWNLOADING;

    /* Persist URL + state for power-loss resume */
    config_set_upgrade_url(s_dl.url);
    config_update_state(UPGRADE_DOWNLOADING);
    config_update_download_progress(0, expected_size);

    if (!download_erase_area(expected_size)) {
        s_dl.state = DL_STATE_ERROR;
        config_update_state(UPGRADE_FAILED);
        return -1;
    }

    printf("[DL] Starting download: %s (%lu bytes)\r\n",
           s_dl.url, (unsigned long)expected_size);
    return 0;
}

void download_process(void)
{
    if (s_dl.state != DL_STATE_DOWNLOADING) {
        return;
    }

    /* Calculate next range */
    uint32_t start = s_dl.downloaded;
    uint32_t end = start + DL_CHUNK_SIZE - 1;
    if (end >= s_dl.total_size) {
        end = s_dl.total_size - 1;
    }

    if (start >= s_dl.total_size) {
        s_dl.state = DL_STATE_COMPLETE;
        config_update_download_progress(s_dl.downloaded, s_dl.total_size);
        config_update_state(UPGRADE_DOWNLOADED);
        printf("[DL] Download complete: %lu bytes, CRC=0x%08lX\r\n",
               (unsigned long)s_dl.downloaded,
               (unsigned long)(s_dl.crc ^ 0xFFFFFFFF));
        return;
    }

    uint32_t chunk_size = end - start + 1;

    int32_t received = http_download_range(s_dl.url, s_dl.port,
                                           start, end,
                                           s_dl.chunk_buf,
                                           sizeof(s_dl.chunk_buf));

    if (received < 0 || (uint32_t)received != chunk_size) {
        s_dl.retry_count++;
        if (s_dl.retry_count >= DL_MAX_RETRIES) {
            printf("[DL] Failed after %d retries\r\n", DL_MAX_RETRIES);
            s_dl.state = DL_STATE_ERROR;
            config_update_state(UPGRADE_FAILED);
            return;
        }
        printf("[DL] Chunk failed (got %ld), retry %d\r\n",
               (long)received, s_dl.retry_count);
        return;
    }

    if (download_write_chunk(s_dl.chunk_buf, chunk_size) < 0) {
        s_dl.state = DL_STATE_ERROR;
        config_update_state(UPGRADE_FAILED);
        return;
    }

    s_dl.retry_count = 0;

    /* Throttled progress persistence */
    if ((s_dl.downloaded - s_dl.last_persist) >= DL_PERSIST_INTERVAL) {
        s_dl.last_persist = s_dl.downloaded;
        config_update_download_progress(s_dl.downloaded, s_dl.total_size);
        printf("[DL] Progress: %lu/%lu (%lu%%)\r\n",
               (unsigned long)s_dl.downloaded,
               (unsigned long)s_dl.total_size,
               (unsigned long)(s_dl.downloaded * 100 / s_dl.total_size));
    }
}

/**
 * Resume a download from config. If no bytes were downloaded yet this
 * behaves like a fresh download_start().
 */
int download_resume(const char *url, uint16_t port)
{
    const boot_config_t *cfg = config_get();

    if (cfg->upgrade_state != UPGRADE_DOWNLOADING) {
        printf("[DL] Nothing to resume (state=%d)\r\n",
               (int)cfg->upgrade_state);
        return -1;
    }
    if (cfg->download_total == 0) {
        printf("[DL] Invalid total size in config\r\n");
        return -1;
    }

    if (cfg->download_progress == 0) {
        /* Fresh download */
        return download_start(url ? url : cfg->upgrade_url, port,
                             cfg->download_total);
    }

    memset(&s_dl, 0, sizeof(s_dl));

    const char *resume_url = (url && url[0]) ? url : cfg->upgrade_url;
    if (resume_url == NULL || resume_url[0] == '\0') {
        printf("[DL] No URL stored for resume; run 'upgrade net <url>'\r\n");
        return -1;
    }

    strncpy(s_dl.url, resume_url, sizeof(s_dl.url) - 1);
    s_dl.url[sizeof(s_dl.url) - 1] = '\0';
    s_dl.port = port;
    s_dl.total_size = cfg->download_total;
    s_dl.downloaded = cfg->download_progress;
    s_dl.state = DL_STATE_DOWNLOADING;
    s_dl.retry_count = 0;

    /* Recalculate CRC from already-downloaded data */
    s_dl.crc = CRC32_INIT_VALUE;
    uint32_t remaining = s_dl.downloaded;
    uint32_t offset = 0;
    uint8_t read_buf[512];

    printf("[DL] Resuming from %lu/%lu\r\n",
           (unsigned long)s_dl.downloaded, (unsigned long)s_dl.total_size);

    while (remaining > 0) {
        uint32_t to_read = (remaining > sizeof(read_buf))
                           ? sizeof(read_buf) : remaining;
        W25Q64_Read(EXT_DOWNLOAD_BASE + offset, read_buf, to_read);
        s_dl.crc = CRC32_Update(s_dl.crc, read_buf, to_read);
        offset += to_read;
        remaining -= to_read;
    }
    s_dl.last_persist = s_dl.downloaded;

    return 0;
}

bool download_is_complete(void)
{
    return (s_dl.state == DL_STATE_COMPLETE);
}

uint32_t download_get_progress(void)
{
    if (s_dl.total_size == 0) {
        return 0;
    }
    return (s_dl.downloaded * 100 / s_dl.total_size);
}

uint32_t download_get_crc(void)
{
    return s_dl.crc ^ 0xFFFFFFFF;
}

uint32_t download_get_downloaded(void)
{
    return s_dl.downloaded;
}
