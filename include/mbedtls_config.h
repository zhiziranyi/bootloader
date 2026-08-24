/**
 * Minimal mbedTLS configuration for FlashSafe Pro bootloader
 * Only ECDSA P-256 + SHA-256 + minimal ASN1 + ECP
 */

#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* Enable required modules */
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_SHA256_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_MD_C

/* Disable everything else */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES

/* Platform settings */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_PLATFORM_C

/* AES and other ciphers not needed */
#undef MBEDTLS_AES_C
#undef MBEDTLS_DES_C
#undef MBEDTLS_ARC4_C
#undef MBEDTLS_CAMELLIA_C
#undef MBEDTLS_BLOWFISH_C
#undef MBEDTLS_XTEA_C
#undef MBEDTLS_RC4_C
#undef MBEDTLS_CCM_C
#undef MBEDTLS_CMAC_C

/* Hash only SHA-256 */
#undef MBEDTLS_MD5_C
#undef MBEDTLS_RIPEMD160_C
#undef MBEDTLS_SHA1_C
#undef MBEDTLS_SHA512_C
#undef MBEDTLS_MD2_C
#undef MBEDTLS_MD4_C

/* No PKCS */
#undef MBEDTLS_PKCS1_C
#undef MBEDTLS_PKCS5_C
#undef MBEDTLS_PKCS8_C
#undef MBEDTLS_PKCS11_C
#undef MBEDTLS_PKCS12_C

/* No RSA */
#undef MBEDTLS_RSA_C

/* No certificates */
#undef MBEDTLS_X509_CRT_PARSE_C
#undef MBEDTLS_X509_CRL_PARSE_C
#undef MBEDTLS_X509_CSR_PARSE_C
#undef MBEDTLS_X509_CREATE_C
#undef MBEDTLS_X509_CRT_WRITE_C
#undef MBEDTLS_X509_CRL_WRITE_C
#undef MBEDTLS_X509_CSR_WRITE_C

/* No TLS */
#undef MBEDTLS_SSL_TLS_C
#undef MBEDTLS_SSL_CLI_C
#undef MBEDTLS_SSL_SRV_C

/* No DTLS */
#undef MBEDTLS_SSL_PROTO_DTLS

/* No cookies */
#undef MBEDTLS_SSL_COOKIE_C

/* No net */
#undef MBEDTLS_NET_C

/* Debug off */
#undef MBEDTLS_DEBUG_C

/* Error strings off */
#undef MBEDTLS_ERROR_C

/* Selftest off */
#undef MBEDTLS_SELF_TEST

/* No entropy (verify-only, no hardware RNG needed) */
#undef MBEDTLS_ENTROPY_C

/* No PK - use raw ECDSA verify directly */
#undef MBEDTLS_PK_C
#undef MBEDTLS_PK_PARSE_C
#undef MBEDTLS_ECDSA_ALT

/* No ECDH */
#undef MBEDTLS_ECDH_C

/* No PEM */
#undef MBEDTLS_PEM_PARSE_C
#undef MBEDTLS_BASE64_C

/* ECP - minimal for P-256 verify */
#define MBEDTLS_ECP_MAX_BITS 256
#define MBEDTLS_ECP_WINDOW 1
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 0

/* Bignum - P-256 needs 32 bytes */
#define MBEDTLS_MPI_MAX_SIZE 32
#define MBEDTLS_MPI_WINDOW_SIZE 1

/* Include the mbedtls check config */
#include "mbedtls/check_config.h"

#endif /* MBEDTLS_CONFIG_H */
