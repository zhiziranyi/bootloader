/**
 * Boot Configuration Manager - FlashSafe Pro
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#include "partition.h"
#include "stm32f4xx_hal.h"
#include <stdbool.h>

/* Config functions */
HAL_StatusTypeDef config_read(boot_config_t *cfg);
HAL_StatusTypeDef config_write(const boot_config_t *cfg);
void config_set_default(boot_config_t *cfg);
void config_print(const boot_config_t *cfg);
HAL_StatusTypeDef config_update_state(upgrade_state_t state);
HAL_StatusTypeDef config_update_download_progress(uint32_t progress, uint32_t total);
HAL_StatusTypeDef config_set_upgrade_url(const char *url);
HAL_StatusTypeDef config_set_fw_version(uint32_t major, uint32_t minor, uint32_t patch);
HAL_StatusTypeDef config_mark_trial(active_slot_t slot,
                                    uint32_t major, uint32_t minor,
                                    uint32_t patch);
HAL_StatusTypeDef config_increment_trial_boot_count(void);
HAL_StatusTypeDef config_confirm_running_slot(active_slot_t slot);
HAL_StatusTypeDef config_clear_pending_as_failed(void);

/* Get current config (from RAM cache) */
const boot_config_t *config_get(void);

#endif /* __CONFIG_H */
