/**
 * Flash Partition Table Implementation
 */

#include "partition.h"

uint32_t partition_get_slot_address(active_slot_t slot)
{
    if (slot == SLOT_A) {
        return ADDR_APP_A;
    } else {
        return ADDR_APP_B;
    }
}

uint32_t partition_get_slot_size(active_slot_t slot)
{
    if (slot == SLOT_A) {
        return SIZE_APP_A;
    } else {
        return SIZE_APP_B;
    }
}
