/**
 * Flash Partition Table - FlashSafe Pro
 * STM32F407ZGT6 (1MB Flash, 128KB RAM)
 */

#ifndef __PARTITION_H
#define __PARTITION_H

#include <stdint.h>

/* Internal Flash Layout */
#define ADDR_BOOTLOADER     0x08000000
#define SIZE_BOOTLOADER     (128 * 1024)    /* sectors 0-4 */

#define ADDR_APP_A          0x08020000
#define SIZE_APP_A          (384 * 1024)    /* sectors 5-7 */

#define ADDR_APP_B          0x08080000
#define SIZE_APP_B          (384 * 1024)    /* sectors 8-10 */

#define ADDR_CONFIG_JOURNAL 0x080E0000UL    /* STM32F407 sector 11 */
#define SIZE_CONFIG_JOURNAL (128UL * 1024UL)

/* External Flash Layout (W25Q64 - 8MB) */
#define EXT_FLASH_SIZE          (8 * 1024 * 1024)

#define EXT_DOWNLOAD_BASE       0x000000    /* Download cache area */
#define EXT_DOWNLOAD_SIZE       (2 * 1024 * 1024)  /* 2MB */

#define EXT_FACTORY_BASE        0x200000    /* Factory firmware backup */
#define EXT_FACTORY_SIZE        (2 * 1024 * 1024)  /* 2MB */

/* Firmware Header Magic */
#define FW_MAGIC                0x464C4153  /* "FLAS" */

/* Active Slot */
typedef enum {
    SLOT_A = 0,
    SLOT_B = 1,
    SLOT_NONE = 0xFFFFFFFFUL
} active_slot_t;

typedef enum {
    FW_TARGET_SLOT_A = SLOT_A,
    FW_TARGET_SLOT_B = SLOT_B
} fw_target_slot_t;

/* Upgrade States */
typedef enum {
    UPGRADE_IDLE = 0,
    UPGRADE_CHECKING,
    UPGRADE_DOWNLOADING,
    UPGRADE_DOWNLOADED,
    UPGRADE_VERIFYING,
    UPGRADE_VERIFIED,
    UPGRADE_TRIAL,
    UPGRADE_SWAPPING,
    UPGRADE_DONE,
    UPGRADE_FAILED,
    UPGRADE_ROLLBACK
} upgrade_state_t;

/* Boot Configuration Structure (stored in Config area) */
typedef struct __attribute__((packed)) {
    uint32_t magic;                 /* 0x464C4153 */
    uint32_t version;               /* Config version */
    active_slot_t active_slot;      /* Current active slot */
    upgrade_state_t upgrade_state;  /* Upgrade state machine state */
    uint32_t fw_version_major;      /* Firmware major version */
    uint32_t fw_version_minor;      /* Firmware minor version */
    uint32_t fw_version_patch;      /* Firmware patch version */
    uint32_t download_progress;     /* Download progress in bytes */
    uint32_t download_total;        /* Total download size */
    uint32_t rollback_count;        /* Number of rollbacks */
    uint32_t verify_fail_count;     /* Consecutive verify failures */
    char     upgrade_url[64];       /* Firmware URL for resume (http://ip:port/path) */
    active_slot_t pending_slot;     /* Candidate slot during a trial boot */
    uint32_t pending_version_major;
    uint32_t pending_version_minor;
    uint32_t pending_version_patch;
    uint32_t trial_boot_count;      /* Boots attempted without app confirmation */
    uint32_t crc;                   /* CRC32 of this structure */
} boot_config_t;

#define CONFIG_RECORD_MAGIC      0x4A4E4C31UL /* "JNL1" */
#define CONFIG_RECORD_SCHEMA     1UL
#define CONFIG_RECORD_COMMITTED  0x00000000UL

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t schema;
    uint32_t sequence;
    boot_config_t config;
    uint32_t crc;
    uint32_t commit;                /* Programmed last */
} config_record_t;

/* Firmware Header Structure */
typedef struct __attribute__((packed)) {
    uint32_t magic;                 /* 0x464C4153 */
    uint16_t ver_major;
    uint16_t ver_minor;
    uint16_t ver_patch;
    uint16_t target_slot;             /* fw_target_slot_t, covered by signature */
    uint32_t image_size;            /* Firmware body size */
    uint32_t image_crc;             /* CRC32 of firmware body */
    uint8_t  pubkey_hash[32];       /* SHA-256 of public key */
    uint32_t signature_offset;      /* Offset to signature block */
    uint32_t header_crc;            /* CRC32 of this header */
} fw_header_t;

/* Signature Block Structure */
typedef struct __attribute__((packed)) {
    uint32_t sig_len;               /* Signature length */
    uint8_t  signature[64];         /* Raw ECDSA signature (r || s, 32+32 bytes) */
    uint32_t sig_crc;               /* CRC32 of signature block */
} signature_block_t;

/* Function Prototypes */
uint32_t partition_get_slot_address(active_slot_t slot);
uint32_t partition_get_slot_size(active_slot_t slot);

#endif /* __PARTITION_H */
