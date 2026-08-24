/**
 * ECDSA Public Key - FlashSafe Pro
 *
 * The 64-byte public key (x || y) is embedded in the bootloader image.
 */

#ifndef __PUBKEY_H
#define __PUBKEY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PUBKEY_SIZE         64
#define PUBKEY_MAGIC        0x504B4559  /* "PKEY" */

/* Public key stored in flash */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  pubkey[PUBKEY_SIZE];
    uint32_t crc;
} stored_pubkey_t;

/**
 * Initialize key storage, loading from flash.
 * Returns 0 if valid key found, -1 otherwise.
 */
int pubkey_init(void);

/**
 * Replacing the embedded key at runtime is not supported.
 */
int pubkey_store(const uint8_t *pubkey, size_t len);

/**
 * Get the current public key.
 * Returns pointer to internal 64-byte buffer, or NULL if not loaded.
 */
const uint8_t *pubkey_get(void);

/**
 * Get the SHA-256 hash of the embedded public key (32 bytes).
 */
const uint8_t *pubkey_hash(void);

/**
 * Check if a public key is available (always true once built).
 */
bool pubkey_is_loaded(void);

#endif /* __PUBKEY_H */
