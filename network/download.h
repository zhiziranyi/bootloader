/**
 * Download Manager - FlashSafe Pro
 *
 * Manages firmware download to external flash (W25Q64).
 * Supports resume from saved progress.
 */

#ifndef __DOWNLOAD_H
#define __DOWNLOAD_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize download manager */
void download_init(void);

/* Start a new download from URL with expected size in bytes */
int download_start(const char *url, uint16_t port, uint32_t expected_size);

/* Start a download into a caller-selected W25Q64 region.  Non-persistent
 * transfers are used for factory-image provisioning: an interrupted write
 * simply leaves an invalid factory image and must never affect OTA resume. */
int download_start_to(const char *url, uint16_t port, uint32_t expected_size,
                      uint32_t flash_base, uint32_t flash_size,
                      bool persist_upgrade_state);

/* Process download - call in main loop, processes one chunk */
void download_process(void);

/* Resume download from saved progress in config */
int download_resume(const char *url, uint16_t port);

/* Check if download is complete */
bool download_is_complete(void);

/* True after unrecoverable transport or external-flash failure. */
bool download_has_failed(void);

/* Get download progress as percentage (0-100) */
uint32_t download_get_progress(void);

/* Get accumulated CRC32 of downloaded data */
uint32_t download_get_crc(void);

/* Get total bytes downloaded */
uint32_t download_get_downloaded(void);

#endif /* __DOWNLOAD_H */
