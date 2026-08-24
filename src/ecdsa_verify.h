/**
 * ECDSA Verification - FlashSafe Pro
 *
 * Thin wrapper around mbedTLS for ECDSA P-256 signature verification.
 * Minimal config: only ECDSA + SHA-256 + ASN1/ASN1 write.
 */

#ifndef __ECDSA_VERIFY_H
#define __ECDSA_VERIFY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Initialize the ECDSA module (called once at boot).
 * Returns 0 on success, -1 on error.
 */
int ecdsa_init(void);

/**
 * Verify an ECDSA P-256 signature.
 *
 * @param msg        Message that was signed (will be SHA-256 hashed internally)
 * @param msg_len    Length of message
 * @param sig        Raw ECDSA signature (r || s, 64 bytes)
 * @param sig_len    Length of signature (must be 64)
 * @param pubkey     Raw public key (64 bytes: x || y)
 * @param pubkey_len Length of public key (must be 64)
 * @return 0 if signature is valid, -1 otherwise
 */
int ecdsa_verify(const uint8_t *msg, size_t msg_len,
                 const uint8_t *sig, size_t sig_len,
                 const uint8_t *pubkey, size_t pubkey_len);

/**
 * Verify a pre-computed SHA-256 hash against an ECDSA P-256 signature.
 * Use this when the message is too large to fit in RAM.
 *
 * @param hash       Pre-computed SHA-256 hash (32 bytes)
 * @param sig        Raw ECDSA signature (r || s, 64 bytes)
 * @param sig_len    Length of signature (must be 64)
 * @param pubkey     Raw public key (64 bytes: x || y)
 * @param pubkey_len Length of public key (must be 64)
 * @return 0 if signature is valid, -1 otherwise
 */
int ecdsa_verify_hash(const uint8_t hash[32],
                      const uint8_t *sig, size_t sig_len,
                      const uint8_t *pubkey, size_t pubkey_len);

/**
 * Verify signature against firmware image.
 * Reads header from ext flash, verifies header CRC, image CRC, and ECDSA signature.
 *
 * @param base_addr  Base address of firmware in external flash
 * @return 0 if valid, error code otherwise
 */
int ecdsa_verify_firmware(uint32_t base_addr);

#endif /* __ECDSA_VERIFY_H */
