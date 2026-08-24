/**
 * HTTP Client - FlashSafe Pro
 *
 * Simple HTTP/1.1 client using LWIP raw API.
 */

#ifndef __HTTP_CLIENT_H
#define __HTTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* HTTP result codes */
typedef enum {
    HTTP_OK = 0,
    HTTP_ERR_CONNECT,
    HTTP_ERR_SEND,
    HTTP_ERR_RECV,
    HTTP_ERR_PARSE,
    HTTP_ERR_TIMEOUT
} http_result_t;

/* Download callback: called with data chunks, returns bytes consumed */
typedef int (*http_data_callback)(const uint8_t *data, uint32_t len);

/* Initialize LWIP netif with DHCP, wait for IP assignment */
int http_init(void);

/* Parse "host[:port][/path]" (optionally http://) into parts.
 * Returns 0 on success. */
int http_parse_url(const char *url, char *host, size_t host_len,
                   uint16_t *port, char *path, size_t path_len);

/* HTTP HEAD request - returns Content-Length or -1 on error */
int32_t http_get_file_size(const char *url, uint16_t port);

/* HTTP GET with Range header: downloads [start, end] into buf.
 * Returns bytes received or -1 on error. */
int32_t http_download_range(const char *url, uint16_t port,
                            uint32_t start, uint32_t end,
                            uint8_t *buf, uint32_t buf_len);

/* Full download with streaming callback.
 * Returns 0 on success, -1 on error. */
int http_download_full(const char *url, uint16_t port,
                       http_data_callback callback);

#endif /* __HTTP_CLIENT_H */
