/* Append-only configuration journal for STM32F407 sector 11. */
#include "config.h"
#include "drivers/flash_internal.h"
#include "drivers/crc32.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_MAGIC    0x464C4153UL
#define CONFIG_VERSION  3UL

static boot_config_t s_config;
static uint32_t s_sequence;

static uint32_t config_calc_crc(const boot_config_t *cfg)
{
    return CRC32_Calculate((const uint8_t *)cfg, sizeof(*cfg) - sizeof(cfg->crc));
}

static uint32_t record_calc_crc(const config_record_t *record)
{
    return CRC32_Calculate((const uint8_t *)record, offsetof(config_record_t, crc));
}

static bool slot_is_valid(active_slot_t slot)
{
    return slot == SLOT_A || slot == SLOT_B || slot == SLOT_NONE;
}

static bool config_validate(const boot_config_t *cfg)
{
    return cfg->magic == CONFIG_MAGIC && cfg->version == CONFIG_VERSION &&
           cfg->active_slot != SLOT_NONE && slot_is_valid(cfg->active_slot) &&
           slot_is_valid(cfg->pending_slot) && config_calc_crc(cfg) == cfg->crc;
}

static bool record_validate(const config_record_t *record)
{
    return record->magic == CONFIG_RECORD_MAGIC &&
           record->schema == CONFIG_RECORD_SCHEMA &&
           record->commit == CONFIG_RECORD_COMMITTED &&
           record->crc == record_calc_crc(record) && config_validate(&record->config);
}

static const config_record_t *record_at(uint32_t index)
{
    return (const config_record_t *)(ADDR_CONFIG_JOURNAL + index * sizeof(config_record_t));
}

static uint32_t record_capacity(void)
{
    return SIZE_CONFIG_JOURNAL / sizeof(config_record_t);
}

static uint32_t first_erased_record(void)
{
    for (uint32_t i = 0; i < record_capacity(); ++i) {
        if (record_at(i)->magic == 0xFFFFFFFFUL) return i;
    }
    return record_capacity();
}

static HAL_StatusTypeDef write_record(uint32_t index, const boot_config_t *cfg,
                                      uint32_t sequence)
{
    config_record_t record;
    memset(&record, 0xFF, sizeof(record));
    record.magic = CONFIG_RECORD_MAGIC;
    record.schema = CONFIG_RECORD_SCHEMA;
    record.sequence = sequence;
    record.config = *cfg;
    record.config.crc = config_calc_crc(&record.config);
    record.crc = record_calc_crc(&record);
    record.commit = CONFIG_RECORD_COMMITTED;

    uint32_t address = ADDR_CONFIG_JOURNAL + index * sizeof(record);
    if (Flash_Internal_Write(address, (const uint8_t *)&record,
                             offsetof(config_record_t, commit)) != HAL_OK) return HAL_ERROR;
    return Flash_Internal_Write(address + offsetof(config_record_t, commit),
                                (const uint8_t *)&record.commit, sizeof(record.commit));
}

HAL_StatusTypeDef config_read(boot_config_t *cfg)
{
    const config_record_t *latest = NULL;
    for (uint32_t i = 0; i < record_capacity(); ++i) {
        const config_record_t *candidate = record_at(i);
        if (candidate->magic == 0xFFFFFFFFUL) break;
        if (record_validate(candidate) && (!latest || candidate->sequence > latest->sequence)) {
            latest = candidate;
        }
    }
    if (latest) {
        s_config = latest->config;
        s_sequence = latest->sequence;
        *cfg = s_config;
        return HAL_OK;
    }
    config_set_default(&s_config);
    s_sequence = 0;
    *cfg = s_config;
    return config_write(&s_config);
}

HAL_StatusTypeDef config_write(const boot_config_t *cfg)
{
    boot_config_t next = *cfg;
    next.crc = config_calc_crc(&next);
    uint32_t index = first_erased_record();
    HAL_StatusTypeDef ret;
    if (index == record_capacity()) {
        ret = Flash_Internal_EraseSector(FLASH_SECTOR_11);
        if (ret != HAL_OK) {
            printf("[CONFIG] journal erase failed status=%d flash_err=0x%08lX\r\n",
                   (int)ret, (unsigned long)HAL_FLASH_GetError());
            return ret;
        }
        index = 0;
    }
    ret = write_record(index, &next, s_sequence + 1U);
    if (ret != HAL_OK) {
        printf("[CONFIG] journal write failed index=%lu status=%d flash_err=0x%08lX\r\n",
               (unsigned long)index, (int)ret,
               (unsigned long)HAL_FLASH_GetError());
        return ret;
    }

    if (!record_validate(record_at(index))) {
        printf("[CONFIG] journal read-back validation failed index=%lu\r\n",
               (unsigned long)index);
        return HAL_ERROR;
    }

    s_config = next;
    ++s_sequence;
    return HAL_OK;
}

void config_set_default(boot_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic = CONFIG_MAGIC;
    cfg->version = CONFIG_VERSION;
    cfg->active_slot = SLOT_A;
    cfg->pending_slot = SLOT_NONE;
    cfg->upgrade_state = UPGRADE_IDLE;
    cfg->fw_version_minor = 1;
    cfg->crc = config_calc_crc(cfg);
}

void config_print(const boot_config_t *cfg)
{
    printf("[CONFIG] seq=%lu active=%c pending=%c state=%d trial=%lu\r\n",
           (unsigned long)s_sequence, cfg->active_slot == SLOT_A ? 'A' : 'B',
           cfg->pending_slot == SLOT_A ? 'A' : (cfg->pending_slot == SLOT_B ? 'B' : '-'),
           (int)cfg->upgrade_state, (unsigned long)cfg->trial_boot_count);
}

HAL_StatusTypeDef config_update_state(upgrade_state_t state)
{ s_config.upgrade_state = state; return config_write(&s_config); }

HAL_StatusTypeDef config_update_download_progress(uint32_t progress, uint32_t total)
{ s_config.download_progress = progress; s_config.download_total = total; return config_write(&s_config); }

const boot_config_t *config_get(void) { return &s_config; }

HAL_StatusTypeDef config_set_upgrade_url(const char *url)
{
    if (url) {
        strncpy(s_config.upgrade_url, url, sizeof(s_config.upgrade_url) - 1U);
        s_config.upgrade_url[sizeof(s_config.upgrade_url) - 1U] = '\0';
    } else s_config.upgrade_url[0] = '\0';
    return config_write(&s_config);
}

HAL_StatusTypeDef config_set_fw_version(uint32_t major, uint32_t minor, uint32_t patch)
{
    s_config.fw_version_major = major; s_config.fw_version_minor = minor;
    s_config.fw_version_patch = patch; return config_write(&s_config);
}

HAL_StatusTypeDef config_mark_trial(active_slot_t slot, uint32_t major,
                                    uint32_t minor, uint32_t patch)
{
    if (slot != SLOT_A && slot != SLOT_B) return HAL_ERROR;
    s_config.pending_slot = slot;
    s_config.pending_version_major = major;
    s_config.pending_version_minor = minor;
    s_config.pending_version_patch = patch;
    s_config.trial_boot_count = 0;
    s_config.upgrade_state = UPGRADE_TRIAL;
    return config_write(&s_config);
}

HAL_StatusTypeDef config_increment_trial_boot_count(void)
{ ++s_config.trial_boot_count; return config_write(&s_config); }

HAL_StatusTypeDef config_confirm_running_slot(active_slot_t slot)
{
    if (slot != s_config.pending_slot) return HAL_ERROR;
    s_config.active_slot = slot;
    s_config.fw_version_major = s_config.pending_version_major;
    s_config.fw_version_minor = s_config.pending_version_minor;
    s_config.fw_version_patch = s_config.pending_version_patch;
    s_config.pending_slot = SLOT_NONE;
    s_config.trial_boot_count = 0;
    s_config.upgrade_state = UPGRADE_DONE;
    return config_write(&s_config);
}

HAL_StatusTypeDef config_clear_pending_as_failed(void)
{
    s_config.pending_slot = SLOT_NONE;
    s_config.trial_boot_count = 0;
    s_config.upgrade_state = UPGRADE_FAILED;
    return config_write(&s_config);
}
