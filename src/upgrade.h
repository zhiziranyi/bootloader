#ifndef __UPGRADE_H
#define __UPGRADE_H

#include "partition.h"
#include <stdbool.h>

int  upgrade_init(void);
int  upgrade_check_and_run(void);
int  upgrade_verify(void);
int  upgrade_swap(active_slot_t active_slot);
void upgrade_rollback(void);
int  upgrade_factory_restore(void);
int  upgrade_factory_provision(const char *url);
void upgrade_arm_install_power_cut_test(void);
bool upgrade_slot_has_valid_app(active_slot_t slot);

#endif /* __UPGRADE_H */
