/**
 * Upgrade State Machine - FlashSafe Pro
 *
 * Single source of truth for the config is config.c (s_config).
 * All state transitions are persisted to the config area so the bootloader
 * can resume/rollback after power loss.
 *
 * Flows:
 *   IDLE       -> nothing to do
 *   DOWNLOADING-> resume download (handled by main/CLI via download.c)
 *   DOWNLOADED / VERIFYING -> verify (CRC + pubkey hash + ECDSA)
 *   VERIFIED   / SWAPPING  -> swap image into inactive slot
 *   FAILED     -> manual recovery via CLI
 */

#include "upgrade.h"
#include "partition.h"
#include "config.h"
#include "jump.h"
#include "drivers/w25q64.h"
#include "drivers/flash_internal.h"
#include "ecdsa_verify.h"
#include "boot_display.h"
#include <stdio.h>
#include <string.h>

/* Consecutive verify failures before the bootloader locks upgrades */
#define UPGRADE_VERIFY_FAIL_LIMIT   3

static uint32_t get_download_base(void)
{
    return EXT_DOWNLOAD_BASE;
}

int upgrade_init(void)
{
    /* Config is already loaded and validated by config_read() in main. */
    const boot_config_t *cfg = config_get();
    if (cfg->upgrade_state == UPGRADE_SWAPPING ||
        cfg->upgrade_state == UPGRADE_VERIFIED) {
        /* These states mean "finish the job" - keep them as-is. */
        printf("[UPGRADE] Pending state: %d\r\n", (int)cfg->upgrade_state);
    }
    return 0;
}

/**
 * Full verification of the image in the external download area:
 *   header CRC -> pubkey hash -> image CRC -> ECDSA signature.
 * On success the state is set to VERIFIED and the version is checked
 * against the running firmware (anti-rollback).
 */
int upgrade_verify(void)
{
    boot_display_show(config_get(), "VERIFY");
    int ret = ecdsa_verify_firmware(get_download_base());
    if (ret != 0) {
        /* Record the failure; lock out after repeated failures. */
        const boot_config_t *cfg = config_get();
        boot_config_t tmp = *cfg;
        tmp.verify_fail_count++;
        tmp.upgrade_state = UPGRADE_FAILED;
        config_write(&tmp);

        printf("[UPGRADE] Verification failed (%d), fail count=%lu\r\n",
               ret, (unsigned long)tmp.verify_fail_count);
        if (tmp.verify_fail_count >= UPGRADE_VERIFY_FAIL_LIMIT) {
            printf("[UPGRADE] Locked after %d verify failures. "
                   "Recover via CLI: 'reboot' or 'factory'.\r\n",
                   UPGRADE_VERIFY_FAIL_LIMIT);
        }
        boot_display_show(config_get(), "VERIFY FAIL");
        return ret;
    }

    /* Read the header again for version/anti-rollback checks. */
    fw_header_t header;
    W25Q64_Read(get_download_base(), (uint8_t *)&header, sizeof(header));
    if (header.magic != FW_MAGIC) {
        return -1;
    }

    if (header.target_slot != FW_TARGET_SLOT_A &&
        header.target_slot != FW_TARGET_SLOT_B) {
        printf("[UPGRADE] Invalid package target slot %u\r\n", header.target_slot);
        boot_display_show(config_get(), "BAD PACKAGE");
        return -1;
    }

    const boot_config_t *cfg = config_get();
    uint32_t cur_major = cfg->fw_version_major;
    uint32_t cur_minor = cfg->fw_version_minor;
    uint32_t cur_patch = cfg->fw_version_patch;

    /* Anti-rollback: reject older firmware. Equal version is allowed
     * (reinstall is harmless) but still verified. */
    bool downgrade =
        (header.ver_major < cur_major) ||
        (header.ver_major == cur_major && header.ver_minor < cur_minor) ||
        (header.ver_major == cur_major && header.ver_minor == cur_minor &&
         header.ver_patch < cur_patch);

    if (downgrade) {
        printf("[UPGRADE] Downgrade rejected: new %u.%u.%u < current %lu.%lu.%lu\r\n",
               header.ver_major, header.ver_minor, header.ver_patch,
               (unsigned long)cur_major, (unsigned long)cur_minor,
               (unsigned long)cur_patch);
        boot_config_t tmp = *cfg;
        tmp.upgrade_state = UPGRADE_FAILED;
        config_write(&tmp);
        boot_display_show(config_get(), "DOWNGRADE");
        return -3;
    }

    /* Reset the failure counter after a successful verification. */
    boot_config_t tmp = *cfg;
    tmp.verify_fail_count = 0;
    tmp.upgrade_state = UPGRADE_VERIFIED;
    if (config_write(&tmp) != HAL_OK) {
        printf("[UPGRADE] Cannot persist VERIFIED state; installation is blocked\r\n");
        boot_display_show(config_get(), "CONFIG ERROR");
        return -4;
    }

    printf("[UPGRADE] Verified OK: v%u.%u.%u, size=%lu bytes\r\n",
           header.ver_major, header.ver_minor, header.ver_patch,
           (unsigned long)header.image_size);
    boot_display_show(config_get(), "VERIFY OK");
    return 0;
}

/**
 * Copy the verified image from the external download area into the
 * inactive slot, then flip the active slot and persist the new state.
 */
int upgrade_swap(active_slot_t target)
{
    uint32_t base = get_download_base();
    fw_header_t header;

    W25Q64_Read(base, (uint8_t *)&header, sizeof(header));
    if (header.magic != FW_MAGIC) {
        printf("[UPGRADE] Swap aborted: bad header\r\n");
        return -1;
    }

    uint32_t image_size = header.image_size;
    uint32_t src_addr = base + sizeof(fw_header_t);
    uint32_t dst_addr = partition_get_slot_address(target);
    uint32_t dst_size = partition_get_slot_size(target);

    if (image_size == 0 || image_size > dst_size) {
        printf("[UPGRADE] Swap aborted: image size %lu invalid (slot %lu)\r\n",
               (unsigned long)image_size, (unsigned long)dst_size);
        return -1;
    }
    if (header.target_slot != (uint16_t)target) {
        printf("[UPGRADE] Package target does not match inactive slot\r\n");
        return -1;
    }

    config_update_state(UPGRADE_SWAPPING);
    boot_display_show_install_progress(0U, image_size);

    /* Erase only the sectors covered by the image. */
    uint32_t sector_start = Flash_Internal_GetSector(dst_addr);
    uint32_t sector_end = Flash_Internal_GetSector(dst_addr + image_size - 1);
    for (uint32_t s = sector_start; s <= sector_end; s++) {
        if (Flash_Internal_EraseSector(s) != HAL_OK) {
            printf("[UPGRADE] Sector erase failed: %lu\r\n", (unsigned long)s);
            config_update_state(UPGRADE_FAILED);
            return -1;
        }
    }

    /* Copy image_size bytes word-aligned. */
    uint32_t buf[128];
    uint32_t remaining = image_size;
    uint32_t offset = 0;

    while (remaining > 0) {
        uint32_t chunk = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        W25Q64_Read(src_addr + offset, (uint8_t *)buf, chunk);
        if (Flash_Internal_Write(dst_addr + offset, (const uint8_t *)buf,
                                 chunk) != HAL_OK) {
            printf("[UPGRADE] Flash write failed at 0x%08lX\r\n",
                   (unsigned long)(dst_addr + offset));
            config_update_state(UPGRADE_FAILED);
            return -1;
        }
        offset += chunk;
        remaining -= chunk;
        if ((offset % (64U * 1024U)) == 0U || remaining == 0U) {
            boot_display_show_install_progress(offset, image_size);
        }
    }

    /* Read-back verification. */
    remaining = image_size;
    offset = 0;
    while (remaining > 0) {
        uint32_t chunk = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        W25Q64_Read(src_addr + offset, (uint8_t *)buf, chunk);
        for (uint32_t i = 0; i < chunk / 4; i++) {
            uint32_t written = Flash_Internal_ReadWord(dst_addr + offset + i * 4);
            if (written != buf[i]) {
                printf("[UPGRADE] Read-back mismatch at offset %lu\r\n",
                       (unsigned long)(offset + i * 4));
                config_update_state(UPGRADE_FAILED);
                return -1;
            }
        }
        offset += chunk;
        remaining -= chunk;
    }

    /* Keep the confirmed slot active until the new app confirms its trial. */
    if (config_mark_trial(target, header.ver_major, header.ver_minor,
                          header.ver_patch) != HAL_OK) {
        return -1;
    }
    printf("[UPGRADE] Installed trial slot %c, fw v%lu.%lu.%lu\r\n",
           target == SLOT_A ? 'A' : 'B',
           (unsigned long)header.ver_major,
           (unsigned long)header.ver_minor,
           (unsigned long)header.ver_patch);
    boot_display_show(config_get(), "TRIAL READY");
    return 0;
}

/**
 * Run the state machine. Returns 0 when the bootloader can proceed to
 * jump to the active app, -1 on failure, -2 when a download resume is
 * required first (handled by the caller, which owns the network loop).
 */
int upgrade_check_and_run(void)
{
    const boot_config_t *cfg = config_get();
    upgrade_state_t state = cfg->upgrade_state;

    if (state == UPGRADE_IDLE || state == UPGRADE_DONE ||
        state == UPGRADE_ROLLBACK) {
        return 0;
    }

    if (state == UPGRADE_DOWNLOADING) {
        return -2;  /* caller resumes the download */
    }

    if (state == UPGRADE_DOWNLOADED || state == UPGRADE_VERIFYING) {
        printf("[UPGRADE] Verifying downloaded image...\r\n");
        if (upgrade_verify() != 0) {
            printf("[UPGRADE] Verification failed; keeping current slot\r\n");
            return -1;
        }
        state = config_get()->upgrade_state;  /* should be VERIFIED */
    }

    if (state == UPGRADE_VERIFIED || state == UPGRADE_SWAPPING) {
        active_slot_t target = (config_get()->active_slot == SLOT_A)
                               ? SLOT_B : SLOT_A;
        printf("[UPGRADE] Installing to slot %c...\r\n",
               target == SLOT_A ? 'A' : 'B');
        return upgrade_swap(target);
    }

    if (state == UPGRADE_FAILED) {
        printf("[UPGRADE] Stuck in FAILED state. "
               "Use CLI: 'rollback', 'factory' or 'reboot'.\r\n");
        return -1;
    }

    return 0;
}

void upgrade_rollback(void)
{
    boot_config_t cfg = *config_get();
    cfg.active_slot = (cfg.active_slot == SLOT_A) ? SLOT_B : SLOT_A;
    cfg.rollback_count++;
    cfg.upgrade_state = UPGRADE_ROLLBACK;
    cfg.download_progress = 0;
    cfg.download_total = 0;
    config_write(&cfg);
    printf("[UPGRADE] Rollback -> slot %c (count=%lu)\r\n",
           cfg.active_slot == SLOT_A ? 'A' : 'B',
           (unsigned long)cfg.rollback_count);
}

/**
 * Restore the factory image from external flash into slot A.
 * Verifies the factory image first; never installs garbage.
 */
int upgrade_factory_restore(void)
{
    printf("[UPGRADE] Factory restore...\r\n");

    int ret = ecdsa_verify_firmware(EXT_FACTORY_BASE);
    if (ret != 0) {
        printf("[UPGRADE] Factory image invalid (%d), restore aborted\r\n", ret);
        return -1;
    }

    fw_header_t header;
    W25Q64_Read(EXT_FACTORY_BASE, (uint8_t *)&header, sizeof(header));

    uint32_t image_size = header.image_size;
    uint32_t src_addr = EXT_FACTORY_BASE + sizeof(fw_header_t);
    uint32_t dst_addr = partition_get_slot_address(SLOT_A);
    uint32_t dst_size = partition_get_slot_size(SLOT_A);

    if (image_size == 0 || image_size > dst_size) {
        printf("[UPGRADE] Factory image size invalid\r\n");
        return -1;
    }

    uint32_t sector_start = Flash_Internal_GetSector(dst_addr);
    uint32_t sector_end = Flash_Internal_GetSector(dst_addr + image_size - 1);
    for (uint32_t s = sector_start; s <= sector_end; s++) {
        if (Flash_Internal_EraseSector(s) != HAL_OK) {
            return -1;
        }
    }

    uint32_t buf[128];
    uint32_t remaining = image_size;
    uint32_t offset = 0;

    while (remaining > 0) {
        uint32_t chunk = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        W25Q64_Read(src_addr + offset, (uint8_t *)buf, chunk);
        if (Flash_Internal_Write(dst_addr + offset, (const uint8_t *)buf,
                                 chunk) != HAL_OK) {
            return -1;
        }
        offset += chunk;
        remaining -= chunk;
    }

    boot_config_t cfg = *config_get();
    cfg.active_slot = SLOT_A;
    cfg.fw_version_major = header.ver_major;
    cfg.fw_version_minor = header.ver_minor;
    cfg.fw_version_patch = header.ver_patch;
    cfg.download_progress = 0;
    cfg.download_total = 0;
    cfg.verify_fail_count = 0;
    cfg.upgrade_state = UPGRADE_DONE;
    config_write(&cfg);

    printf("[UPGRADE] Factory restore OK -> slot A, v%lu.%lu.%lu\r\n",
           (unsigned long)cfg.fw_version_major,
           (unsigned long)cfg.fw_version_minor,
           (unsigned long)cfg.fw_version_patch);
    return 0;
}

/**
 * Check whether a slot contains a plausible app (valid stack pointer
 * and reset handler inside the flash image region).
 */
bool upgrade_slot_has_valid_app(active_slot_t slot)
{
    uint32_t addr = partition_get_slot_address(slot);
    uint32_t size = partition_get_slot_size(slot);
    uint32_t sp = *(volatile uint32_t *)addr;
    uint32_t reset = *(volatile uint32_t *)(addr + 4);

    /* The reset stack pointer may legally equal the exclusive SRAM top. */
    if (sp <= 0x20000000 || sp > 0x20020000) {
        return false;
    }
    if (reset < addr || reset >= (addr + size)) {
        return false;
    }
    return true;
}
