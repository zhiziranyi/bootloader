#ifndef BOOT_DISPLAY_H
#define BOOT_DISPLAY_H

#include "config.h"

void boot_display_init(void);
void boot_display_show(const boot_config_t *cfg, const char *phase);
void boot_display_show_install_progress(uint32_t complete, uint32_t total);

#endif /* BOOT_DISPLAY_H */
