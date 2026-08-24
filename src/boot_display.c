#include "boot_display.h"
#include "oled_display.h"

#include <stdio.h>

static const char *boot_display_state_name(upgrade_state_t state)
{
    switch (state) {
    case UPGRADE_IDLE:        return "IDLE";
    case UPGRADE_DOWNLOADING: return "DOWNLOAD";
    case UPGRADE_DOWNLOADED:  return "DOWNLOADED";
    case UPGRADE_VERIFYING:   return "VERIFY";
    case UPGRADE_VERIFIED:    return "VERIFIED";
    case UPGRADE_TRIAL:       return "TRIAL";
    case UPGRADE_SWAPPING:    return "INSTALL";
    case UPGRADE_DONE:        return "DONE";
    case UPGRADE_FAILED:      return "FAILED";
    case UPGRADE_ROLLBACK:    return "ROLLBACK";
    default:                  return "CHECK";
    }
}

static void boot_display_line(uint8_t page, const char *text)
{
    OLED_DrawText(0U, page, text);
}

void boot_display_init(void)
{
    if (OLED_Init() == HAL_OK) {
        printf("[OLED] SSD1306 detected at 0x%02X on PB6/PB7\r\n",
               OLED_GetAddress());
    } else {
        printf("[OLED] Not detected at 0x3C or 0x3D\r\n");
    }
}

void boot_display_show(const boot_config_t *cfg, const char *phase)
{
    char line[22];

    if (!OLED_IsAvailable()) return;
    OLED_Clear();
    boot_display_line(0U, "FLSAFE BOOT");
    boot_display_line(1U, (phase != NULL) ? phase : "START");
    if (cfg != NULL) {
        snprintf(line, sizeof(line), "ACTIVE %c", cfg->active_slot == SLOT_A ? 'A' : 'B');
        boot_display_line(2U, line);
        snprintf(line, sizeof(line), "STATE %s", boot_display_state_name(cfg->upgrade_state));
        boot_display_line(3U, line);
        snprintf(line, sizeof(line), "V%lu.%lu.%lu",
                 (unsigned long)cfg->fw_version_major,
                 (unsigned long)cfg->fw_version_minor,
                 (unsigned long)cfg->fw_version_patch);
        boot_display_line(4U, line);
        if (cfg->download_total > 0U) {
            uint32_t percent = (cfg->download_progress * 100U) / cfg->download_total;
            snprintf(line, sizeof(line), "DL %lu%%", (unsigned long)percent);
            boot_display_line(5U, line);
        }
        if (cfg->pending_slot != SLOT_NONE) {
            snprintf(line, sizeof(line), "PENDING %c",
                     cfg->pending_slot == SLOT_A ? 'A' : 'B');
            boot_display_line(6U, line);
        }
    }
    boot_display_line(7U, "PA9 PA10 CLI");
    (void)OLED_Refresh();
}

void boot_display_show_install_progress(uint32_t complete, uint32_t total)
{
    char line[22];
    uint32_t percent = (total == 0U) ? 0U : (complete * 100U) / total;

    if (!OLED_IsAvailable()) return;
    OLED_Clear();
    boot_display_line(0U, "FLSAFE BOOT");
    boot_display_line(1U, "INSTALLING");
    snprintf(line, sizeof(line), "COPY %lu%%", (unsigned long)percent);
    boot_display_line(3U, line);
    boot_display_line(7U, "DO NOT POWER OFF");
    (void)OLED_Refresh();
}
