/**
 * ECDSA Verification - FlashSafe Pro
 *
 * Minimal ECDSA P-256 signature verification using mbedTLS.
 * Verifies firmware in-place from external flash (no SRAM copy).
 */

#include "ecdsa_verify.h"
#include "partition.h"
#include "pubkey.h"
#include "drivers/w25q64.h"
#include "drivers/crc32.h"
#include "stm32f4xx_hal.h"

#include <stdio.h>
#include <string.h>

/* mbedTLS includes */
#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/bignum.h"
#include "mbedtls/ecp.h"

/* Global ECDSA group (initialized once) */
static mbedtls_ecp_group ecdsa_grp;
static bool ecdsa_initialized = false;

/**
 * Initialize the ECDSA module
 */
int ecdsa_init(void)
{
    if (ecdsa_initialized) {
        return 0;
    }

    mbedtls_ecp_group_init(&ecdsa_grp);

    int ret = mbedtls_ecp_group_load(&ecdsa_grp, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) {
        printf("[ECDSA] Failed to load P-256 group: -0x%04X\r\n", -ret);
        return -1;
    }

    ecdsa_initialized = true;
    printf("[ECDSA] Initialized (P-256)\r\n");
    return 0;
}

/**
 * Parse a raw 64-byte public key (x || y) into mbedTLS point
 */
static int parse_raw_pubkey(mbedtls_ecp_point *Q,
                            const uint8_t *pubkey, size_t pubkey_len)
{
    if (pubkey_len != 64) {
        return -1;
    }

    /* Build uncompressed point: 0x04 || x || y */
    uint8_t point_buf[65];
    point_buf[0] = 0x04;
    memcpy(point_buf + 1, pubkey, 64);

    return mbedtls_ecp_point_read_binary(&ecdsa_grp, Q, point_buf, 65);
}

/**
 * Shared signature checks: reject non-64-byte sigs, trivial scalars and
 * high-S values (signature malleability hardening).
 */
static int check_signature_scalars(const uint8_t *sig, size_t sig_len)
{
    if (sig_len != 64) {
        printf("[ECDSA] Invalid signature length: %zu\r\n", sig_len);
        return -1;
    }

    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_mpi_read_binary(&r, sig, 32);
    mbedtls_mpi_read_binary(&s, sig + 32, 32);

    int bad = 0;
    if (mbedtls_mpi_cmp_int(&r, 0) <= 0 || mbedtls_mpi_cmp_int(&s, 0) <= 0) {
        printf("[ECDSA] Signature scalar out of range\r\n");
        bad = 1;
    } else {
        /* P-256 order n/2 (low-S means s <= n/2). */
        static const uint8_t n_half[32] = {
            0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0x5D, 0x57, 0x6E, 0x73, 0x57, 0xA4, 0x50, 0x1D,
            0xDF, 0xE9, 0x2F, 0x46, 0x68, 0x1B, 0x20, 0xA0
        };
        mbedtls_mpi half;
        mbedtls_mpi_init(&half);
        mbedtls_mpi_read_binary(&half, n_half, sizeof(n_half));
        if (mbedtls_mpi_cmp_mpi(&s, &half) > 0) {
            printf("[ECDSA] High-S signature rejected\r\n");
            bad = 1;
        }
        mbedtls_mpi_free(&half);
    }

    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return bad ? -1 : 0;
}

/**
 * Verify an ECDSA P-256 signature
 */
int ecdsa_verify(const uint8_t *msg, size_t msg_len,
                 const uint8_t *sig, size_t sig_len,
                 const uint8_t *pubkey, size_t pubkey_len)
{
    if (!ecdsa_initialized) {
        printf("[ECDSA] Not initialized\r\n");
        return -1;
    }

    /* Hash the message with SHA-256 */
    uint8_t hash[32];
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    int ret = mbedtls_sha256_starts_ret(&sha_ctx, 0);
    if (ret == 0) {
        ret = mbedtls_sha256_update_ret(&sha_ctx, msg, msg_len);
    }
    if (ret == 0) {
        ret = mbedtls_sha256_finish_ret(&sha_ctx, hash);
    }
    mbedtls_sha256_free(&sha_ctx);

    if (ret != 0) {
        printf("[ECDSA] SHA-256 failed: -0x%04X\r\n", -ret);
        return -1;
    }

    return ecdsa_verify_hash(hash, sig, sig_len, pubkey, pubkey_len);
}

/**
 * Verify a pre-computed SHA-256 hash against an ECDSA P-256 signature.
 * Use this when the message is too large to fit in RAM.
 */
int ecdsa_verify_hash(const uint8_t hash[32],
                      const uint8_t *sig, size_t sig_len,
                      const uint8_t *pubkey, size_t pubkey_len)
{
    if (!ecdsa_initialized) {
        printf("[ECDSA] Not initialized\r\n");
        return -1;
    }

    if (check_signature_scalars(sig, sig_len) != 0) {
        return -1;
    }

    /* Parse public key */
    mbedtls_ecp_point Q;
    mbedtls_ecp_point_init(&Q);
    int ret = parse_raw_pubkey(&Q, pubkey, pubkey_len);
    if (ret != 0) {
        printf("[ECDSA] Invalid public key: -0x%04X\r\n", -ret);
        mbedtls_ecp_point_free(&Q);
        return -1;
    }

    /* Parse raw r||s signature (32+32 = 64 bytes) */
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_mpi_read_binary(&r, sig, 32);
    mbedtls_mpi_read_binary(&s, sig + 32, 32);

    /* Verify */
    ret = mbedtls_ecdsa_verify(&ecdsa_grp, hash, 32, &Q, &r, &s);

    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);

    if (ret != 0) {
        printf("[ECDSA] Signature verification FAILED: -0x%04X\r\n", -ret);
        return -1;
    }

    printf("[ECDSA] Signature verification PASSED\r\n");
    return 0;
}

/**
 * Verify firmware image in external flash (in-place, no SRAM copy)
 * Layout: [fw_header_t] [firmware body] [signature_block_t]
 */
int ecdsa_verify_firmware(uint32_t base_addr)
{
    /* Read firmware header */
    fw_header_t header;
    W25Q64_Read(base_addr, (uint8_t *)&header, sizeof(header));

    /* Verify magic */
    if (header.magic != FW_MAGIC) {
        printf("[ECDSA] Bad magic: 0x%08lX\r\n", (unsigned long)header.magic);
        return -2;
    }

    /* Verify header CRC (header without last field) */
    uint32_t hdr_calc = CRC32_Calculate(
        (const uint8_t *)&header,
        sizeof(header) - sizeof(uint32_t));
    if (header.header_crc != hdr_calc) {
        printf("[ECDSA] Header CRC mismatch\r\n");
        return -3;
    }

    if (header.image_size == 0 ||
        header.image_size > EXT_DOWNLOAD_SIZE) {
        printf("[ECDSA] Image size out of range: %lu\r\n",
               (unsigned long)header.image_size);
        return -4;
    }

    /* Compute image CRC from external flash (streaming) */
    uint32_t img_addr = base_addr + sizeof(fw_header_t);
    uint32_t remaining = header.image_size;
    uint32_t crc = CRC32_INIT_VALUE;
    uint8_t buf[512];

    while (remaining > 0) {
        uint32_t chunk = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        W25Q64_Read(img_addr, buf, chunk);
        crc = CRC32_Update(crc, buf, chunk);
        img_addr += chunk;
        remaining -= chunk;
    }

    if (header.image_crc != (crc ^ 0xFFFFFFFF)) {
        printf("[ECDSA] Image CRC mismatch\r\n");
        return -5;
    }

    /* Read signature block */
    signature_block_t sig_block;
    uint32_t sig_addr = base_addr + sizeof(fw_header_t) + header.image_size;
    W25Q64_Read(sig_addr, (uint8_t *)&sig_block, sizeof(sig_block));

    /* Verify signature block CRC */
    uint32_t sig_calc = CRC32_Calculate(
        (const uint8_t *)&sig_block,
        sizeof(sig_block) - sizeof(uint32_t));
    if (sig_block.sig_crc != sig_calc) {
        printf("[ECDSA] Signature block CRC mismatch\r\n");
        return -6;
    }

    /* Verify ECDSA signature using the embedded public key */
    const uint8_t *pubkey = pubkey_get();
    if (pubkey == NULL) {
        printf("[ECDSA] No public key embedded, refusing to verify\r\n");
        return -7;
    }

    /* Anti-tamper: the header must reference the embedded key. */
    {
        uint8_t key_hash[32];
        mbedtls_sha256_context kctx;
        mbedtls_sha256_init(&kctx);
        int kret = mbedtls_sha256_starts_ret(&kctx, 0);
        if (kret == 0) {
            kret = mbedtls_sha256_update_ret(&kctx, pubkey, PUBKEY_SIZE);
        }
        if (kret == 0) {
            kret = mbedtls_sha256_finish_ret(&kctx, key_hash);
        }
        mbedtls_sha256_free(&kctx);

        if (kret != 0 ||
            memcmp(header.pubkey_hash, key_hash, 32) != 0) {
            printf("[ECDSA] pubkey_hash mismatch (tampered header)\r\n");
            return -7;
        }
    }

    /* Compute SHA-256 of header + body from flash (streaming) */
    uint8_t hash[32];

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    int ret = mbedtls_sha256_starts_ret(&sha_ctx, 0);
    if (ret == 0) {
        ret = mbedtls_sha256_update_ret(&sha_ctx,
                                        (const uint8_t *)&header,
                                        sizeof(fw_header_t));
    }

    uint32_t body_remaining = header.image_size;
    uint32_t body_addr = base_addr + sizeof(fw_header_t);
    uint8_t hash_buf[512];

    while (body_remaining > 0 && ret == 0) {
        uint32_t chunk = (body_remaining > sizeof(hash_buf))
                         ? sizeof(hash_buf) : body_remaining;
        W25Q64_Read(body_addr, hash_buf, chunk);
        ret = mbedtls_sha256_update_ret(&sha_ctx, hash_buf, chunk);
        body_addr += chunk;
        body_remaining -= chunk;
    }

    if (ret == 0) {
        ret = mbedtls_sha256_finish_ret(&sha_ctx, hash);
    }
    mbedtls_sha256_free(&sha_ctx);

    if (ret != 0) {
        printf("[ECDSA] SHA-256 computation failed\r\n");
        return -8;
    }

    printf("[ECDSA] Header version: %d.%d.%d, size: %lu\r\n",
           header.ver_major, header.ver_minor, header.ver_patch,
           (unsigned long)header.image_size);
    printf("[ECDSA] Image CRC verified, ECDSA signature check...\r\n");

    /* Verify ECDSA signature against the pre-computed SHA-256 hash */
    int vret = ecdsa_verify_hash(hash,
                                 sig_block.signature, sig_block.sig_len,
                                 pubkey, PUBKEY_SIZE);
    if (vret != 0) {
        printf("[ECDSA] Firmware signature INVALID\r\n");
        return -9;
    }

    printf("[ECDSA] Firmware signature VALID\r\n");
    return 0;
}
