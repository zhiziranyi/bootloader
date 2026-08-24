#include "boot_confirm.h"
#include "stm32f4xx_hal.h"
#include "config.h"

int boot_confirm_running_image(void)
{
    active_slot_t slot = SLOT_NONE;
    if (SCB->VTOR == ADDR_APP_A) slot = SLOT_A;
    if (SCB->VTOR == ADDR_APP_B) slot = SLOT_B;
    if (slot == SLOT_NONE) return -1;

    boot_config_t config;
    if (config_read(&config) != HAL_OK) return -1;
    return config_confirm_running_slot(slot) == HAL_OK ? 0 : -1;
}
