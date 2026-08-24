/**
 * HTTP Client - FlashSafe Pro
 *
 * Simple HTTP/1.1 client using LWIP raw TCP API (NO_SYS, polling).
 * Supports HEAD and GET requests with Range headers.
 *
 * URL format: [http://]host[:port][/path]
 *   - default port: 8080
 *   - default path: /firmware/firmware.pkg
 */

#include "http_client.h"
#include "ethernetif.h"
#include "eth_init.h"
#include "stm32f4xx_hal.h"

#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define HTTP_DEFAULT_PORT       8080
#define HTTP_DEFAULT_PATH       "/firmware/firmware.pkg"
#define HTTP_MAX_URL_LEN        128
#define HTTP_RECV_BUF_SIZE      2048
#define HTTP_CONNECT_TIMEOUT_MS 8000
#define HTTP_RECV_TIMEOUT_MS    15000

/* Parser state for HTTP response */
typedef enum {
    PARSE_STATUS_LINE,
    PARSE_HEADERS,
    PARSE_BODY,
    PARSE_DONE
} parse_state_t;

/* Per-connection context */
typedef struct {
    struct tcp_pcb *pcb;
    parse_state_t parse_state;
    int status_code;
    int32_t content_length;
    bool headers_done;
    bool connected;
    bool error;
    bool skip_linefeed;

    /* For range download */
    uint8_t *recv_buf;
    uint32_t recv_buf_len;
    uint32_t recv_offset;

    /* For streaming download */
    http_data_callback callback;

    /* Partial line buffer for header parsing */
    char line_buf[256];
    uint32_t line_len;
} http_conn_t;

/* The netif must persist for the lifetime of the network stack -
 * previously it was a stack local and went out of scope after http_init(). */
static struct netif s_netif;
static bool s_netif_ready = false;

/* ---- LWIP helpers ---- */

static void http_conn_init(http_conn_t *conn)
{
    memset(conn, 0, sizeof(http_conn_t));
    conn->status_code = -1;
    conn->content_length = -1;
}

/**
 * Parse a firmware URL into host / port / path.
 * Accepts "host", "host:port", "host/path", "host:port/path",
 * optionally prefixed with "http://".
 */
int http_parse_url(const char *url, char *host, size_t host_len,
                   uint16_t *port, char *path, size_t path_len)
{
    if (url == NULL || host == NULL || port == NULL || path == NULL) {
        return -1;
    }

    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    size_t host_part_len;
    if (slash != NULL && (colon == NULL || colon > slash)) {
        host_part_len = (size_t)(slash - p);
    } else if (colon != NULL) {
        host_part_len = (size_t)(colon - p);
    } else {
        host_part_len = strlen(p);
    }

    if (host_part_len == 0 || host_part_len >= host_len) {
        return -1;
    }
    memcpy(host, p, host_part_len);
    host[host_part_len] = '\0';

    *port = HTTP_DEFAULT_PORT;
    if (colon != NULL && (slash == NULL || colon < slash)) {
        *port = (uint16_t)atoi(colon + 1);
        if (*port == 0) {
            return -1;
        }
    }

    if (slash != NULL) {
        strncpy(path, slash, path_len - 1);
        path[path_len - 1] = '\0';
    } else {
        strncpy(path, HTTP_DEFAULT_PATH, path_len - 1);
        path[path_len - 1] = '\0';
    }
    return 0;
}

/* Wait in a polling loop for a condition or timeout.
 * Pumps LWIP timers AND the Ethernet receive path - without this the
 * responses never arrive in NO_SYS polling mode. */
static int http_wait(http_conn_t *conn, bool *flag, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while (!*flag && !conn->error) {
        sys_check_timeouts();
        ethernetif_input(&s_netif);
        if ((HAL_GetTick() - start) > timeout_ms) {
            return -1;
        }
    }
    return conn->error ? -1 : 0;
}

/* ---- TCP callbacks ---- */

static err_t http_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)arg;
    (void)pcb;
    (void)len;
    return ERR_OK;
}

static void http_err_cb(void *arg, err_t err)
{
    (void)err;
    http_conn_t *conn = (http_conn_t *)arg;
    if (conn) {
        conn->error = true;
        conn->pcb = NULL;
    }
}

static err_t http_poll_cb(void *arg, struct tcp_pcb *pcb)
{
    (void)arg;
    (void)pcb;
    return ERR_OK;
}

/* Process received data: parse status line, headers, body */
static void http_process_data(http_conn_t *conn, const uint8_t *data, uint32_t len)
{
    uint32_t i = 0;

    while (i < len && conn->parse_state != PARSE_DONE) {
        /* A TCP/pbuf boundary is allowed between '\r' and '\n'.  Consume
         * that pending LF before treating the next buffer as response body. */
        if (conn->skip_linefeed) {
            if (data[i] == '\n') {
                i++;
            }
            conn->skip_linefeed = false;
            if (i >= len) {
                break;
            }
        }

        if (conn->parse_state == PARSE_STATUS_LINE ||
            conn->parse_state == PARSE_HEADERS) {

            /* Accumulate until \r\n */
            while (i < len && conn->line_len < sizeof(conn->line_buf) - 1) {
                char c = (char)data[i++];
                if (c == '\r') {
                    conn->line_buf[conn->line_len] = '\0';
                    conn->skip_linefeed = true;

                    if (conn->parse_state == PARSE_STATUS_LINE) {
                        /* Parse "HTTP/1.x <status> ..." */
                        char *sp = strchr(conn->line_buf, ' ');
                        if (sp) {
                            conn->status_code = atoi(sp + 1);
                        }
                        conn->parse_state = PARSE_HEADERS;
                    } else {
                        /* Empty line = end of headers */
                        if (conn->line_len == 0) {
                            conn->headers_done = true;
                            conn->parse_state = PARSE_BODY;
                        } else {
                            /* Parse Content-Length */
                            if (strncasecmp(conn->line_buf,
                                            "Content-Length:", 15) == 0) {
                                conn->content_length = atoi(conn->line_buf + 15);
                            }
                        }
                    }
                    conn->line_len = 0;
                    break;
                } else if (c != '\n') {
                    conn->line_buf[conn->line_len++] = c;
                }
            }
        }

        /* When the header terminator is wholly in this pbuf, consume its
         * pending LF before calculating the body range.  If it is in the
         * next pbuf, leave the flag set for the loop above. */
        if (conn->parse_state == PARSE_BODY &&
            conn->skip_linefeed && i < len) {
            if (data[i] == '\n') {
                i++;
            }
            conn->skip_linefeed = false;
        }

        if (conn->parse_state == PARSE_BODY) {
            uint32_t body_len = len - i;
            if (body_len > 0) {
                if (conn->recv_buf) {
                    uint32_t space = conn->recv_buf_len - conn->recv_offset;
                    uint32_t copy = (body_len < space) ? body_len : space;
                    memcpy(conn->recv_buf + conn->recv_offset, data + i, copy);
                    conn->recv_offset += copy;
                    i += copy;
                } else if (conn->callback) {
                    int consumed = conn->callback(data + i, body_len);
                    if (consumed <= 0) {
                        conn->parse_state = PARSE_DONE;
                        break;
                    }
                    i += (uint32_t)consumed;
                } else {
                    i += body_len;
                }
            } else {
                break;
            }
        }
    }
}

static err_t http_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)err;
    http_conn_t *conn = (http_conn_t *)arg;

    if (p == NULL) {
        /* Remote closed connection - pcb is freed by lwIP. */
        if (conn) {
            conn->connected = false;
            conn->pcb = NULL;
        }
        return ERR_OK;
    }

    if (conn == NULL) {
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    struct pbuf *q;
    for (q = p; q != NULL; q = q->next) {
        http_process_data(conn, (const uint8_t *)q->payload, q->len);
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t http_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)err;
    (void)pcb;
    http_conn_t *conn = (http_conn_t *)arg;
    if (conn) {
        conn->connected = true;
    }
    return ERR_OK;
}

/* ---- Public API ---- */

int http_init(void)
{
    if (s_netif_ready) {
        return 0;
    }

    /* Initialize LWIP */
    lwip_init();

    /* Create and add the netif */
    ip_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 0, 0, 0, 0);
    IP4_ADDR(&netmask, 0, 0, 0, 0);
    IP4_ADDR(&gw, 0, 0, 0, 0);

    if (netif_add(&s_netif, &ipaddr, &netmask, &gw, NULL,
                  ethernetif_init, ethernet_input) == NULL) {
        printf("[HTTP] netif_add failed\r\n");
        return -1;
    }
    netif_set_default(&s_netif);

    /* Bring up link */
    netif_set_up(&s_netif);
    netif_set_link_up(&s_netif);

    /* Start DHCP */
    dhcp_start(&s_netif);

    printf("[HTTP] Waiting for DHCP...\r\n");
    uint32_t start = HAL_GetTick();
    while (ip_addr_isany(&s_netif.ip_addr)) {
        if ((HAL_GetTick() - start) > 10000) {
            printf("[HTTP] DHCP timeout, no IP address\r\n");
            return -1;
        }
        sys_check_timeouts();
        ethernetif_input(&s_netif);
        HAL_Delay(5);
    }

    printf("[HTTP] IP: %s\r\n", ipaddr_ntoa(&s_netif.ip_addr));
    s_netif_ready = true;
    return 0;
}

static int32_t http_do_request(const char *url, uint16_t port,
                               const char *method,
                               const char *extra_headers,
                               http_conn_t *conn)
{
    char host[64];
    char path[128];
    uint16_t parsed_port;

    if (http_parse_url(url, host, sizeof(host), &parsed_port,
                       path, sizeof(path)) != 0) {
        printf("[HTTP] Invalid URL: %s\r\n", url);
        return -1;
    }
    if (port != 0) {
        parsed_port = port;  /* explicit port override */
    }

    /* Resolve hostname - simplified: expect IP in dotted notation */
    ip_addr_t remote_ip;
    if (ipaddr_aton(host, &remote_ip) != 1) {
        printf("[HTTP] Cannot resolve: %s\r\n", host);
        return -1;
    }

    conn->pcb = tcp_new();
    if (conn->pcb == NULL) {
        printf("[HTTP] tcp_new failed\r\n");
        return -1;
    }

    tcp_arg(conn->pcb, conn);
    tcp_recv(conn->pcb, http_recv_cb);
    tcp_err(conn->pcb, http_err_cb);
    tcp_sent(conn->pcb, http_sent_cb);
    tcp_poll(conn->pcb, http_poll_cb, 10);

    err_t ret = tcp_connect(conn->pcb, &remote_ip, parsed_port,
                            http_connected_cb);
    if (ret != ERR_OK) {
        printf("[HTTP] tcp_connect failed: %d\r\n", ret);
        tcp_close(conn->pcb);
        conn->pcb = NULL;
        return -1;
    }

    /* Wait for connection */
    if (http_wait(conn, &conn->connected, HTTP_CONNECT_TIMEOUT_MS) < 0) {
        printf("[HTTP] Connect timeout\r\n");
        if (conn->pcb) {
            tcp_abort(conn->pcb);
            conn->pcb = NULL;
        }
        return -1;
    }

    /* Build HTTP request with the real path */
    char request[512];
    snprintf(request, sizeof(request),
             "%s %s HTTP/1.1\r\n"
             "Host: %s:%u\r\n"
             "%s"
             "Connection: close\r\n"
             "\r\n",
             method, path, host, parsed_port,
             extra_headers ? extra_headers : "");

    err_t write_err = tcp_write(conn->pcb, request, strlen(request),
                                TCP_WRITE_FLAG_COPY);
    if (write_err != ERR_OK) {
        printf("[HTTP] tcp_write failed: %d\r\n", write_err);
        tcp_close(conn->pcb);
        conn->pcb = NULL;
        return -1;
    }
    tcp_output(conn->pcb);

    /* Wait for response (pumping RX) */
    uint32_t start = HAL_GetTick();
    while (conn->connected && conn->parse_state != PARSE_DONE) {
        sys_check_timeouts();
        ethernetif_input(&s_netif);
        if ((HAL_GetTick() - start) > HTTP_RECV_TIMEOUT_MS) {
            printf("[HTTP] Recv timeout\r\n");
            if (conn->pcb) {
                tcp_abort(conn->pcb);
                conn->pcb = NULL;
            }
            return -1;
        }
    }

    if (conn->pcb) {
        tcp_close(conn->pcb);
        conn->pcb = NULL;
    }

    return 0;
}

int32_t http_get_file_size(const char *url, uint16_t port)
{
    http_conn_t conn;
    http_conn_init(&conn);
    int32_t ret = http_do_request(url, port, "HEAD", "", &conn);
    if (ret < 0) {
        return -1;
    }

    if (conn.status_code != 200 && conn.status_code != 206) {
        printf("[HTTP] HEAD returned %d\r\n", conn.status_code);
        return -1;
    }

    return conn.content_length;
}

int32_t http_download_range(const char *url, uint16_t port,
                            uint32_t start, uint32_t end,
                            uint8_t *buf, uint32_t buf_len)
{
    http_conn_t conn;
    http_conn_init(&conn);
    conn.recv_buf = buf;
    conn.recv_buf_len = buf_len;
    conn.recv_offset = 0;

    char range_header[64];
    snprintf(range_header, sizeof(range_header),
             "Range: bytes=%lu-%lu\r\n",
             (unsigned long)start, (unsigned long)end);

    int32_t ret = http_do_request(url, port, "GET", range_header, &conn);
    if (ret < 0) {
        return -1;
    }

    /* A ranged GET must return 206. A 200 means the server ignored the
     * Range header and sent the whole file - the first chunk would be
     * duplicated forever, so reject it. */
    if (conn.status_code != 206) {
        printf("[HTTP] GET Range returned %d (expected 206)\r\n",
               conn.status_code);
        return -1;
    }

    return (int32_t)conn.recv_offset;
}

int http_download_full(const char *url, uint16_t port,
                       http_data_callback callback)
{
    http_conn_t conn;
    http_conn_init(&conn);
    conn.callback = callback;

    int32_t ret = http_do_request(url, port, "GET", "", &conn);
    if (ret < 0) {
        return -1;
    }

    if (conn.status_code != 200 && conn.status_code != 206) {
        printf("[HTTP] GET returned %d\r\n", conn.status_code);
        return -1;
    }

    return 0;
}
