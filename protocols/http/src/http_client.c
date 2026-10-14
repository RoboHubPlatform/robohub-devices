/*
 * http_client.c — a small HTTP GET over the network contract.
 *
 * One request, one response, no keep-alive and no chunked transfer: enough to
 * read a device's status document. The response is accumulated in the caller's
 * buffer and the body is moved to the front of it when the call returns, so the
 * caller sees the document and not the headers.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "http_client.h"

#include <stddef.h>
#include <string.h>

/* Append `text` to a bounded buffer; return the bytes added, or the remaining
 * space is exceeded and the caller sees a full buffer. */
static rh_size append(char* out, rh_size cap, rh_size at, const char* text)
{
    rh_size len = (rh_size)strlen(text);
    if (at + len >= cap) {
        rh_size room = (cap > at) ? (cap - at - 1u) : 0u;
        if (room > 0u) {
            memcpy(out + at, text, room);
            at += room;
            out[at] = '\0';
        }
        return cap; /* signals "no more room" to the caller */
    }
    memcpy(out + at, text, len);
    out[at + len] = '\0';
    return at + len;
}

/* The byte just past the blank line that ends the headers, or NULL. */
static const char* header_end(const char* text, rh_size len)
{
    for (rh_size i = 0; i + 3u < len; i++) {
        if (text[i] == '\r' && text[i + 1u] == '\n' &&
            text[i + 2u] == '\r' && text[i + 3u] == '\n') {
            return text + i + 4u;
        }
    }
    return NULL;
}

/* The Content-Length, or -1 when it is absent. */
static rh_i32 content_length(const char* text, const char* body_at)
{
    const char* p = text;
    while (p < body_at) {
        if ((p[0] == 'C' || p[0] == 'c') &&
            strncmp(p, "Content-Length:", 15) == 0) {
            p += 15;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            rh_i32 value = 0;
            rh_bool digits = RH_FALSE;
            while (*p >= '0' && *p <= '9') {
                value = value * 10 + (*p - '0');
                digits = RH_TRUE;
                p++;
            }
            return digits ? value : -1;
        }
        /* Advance a line at a time. */
        const char* nl = strchr(p, '\n');
        if (nl == NULL || nl >= body_at) {
            break;
        }
        p = nl + 1;
    }
    return -1;
}

RoboHubError robohub_http_get(const char* host,
                              rh_u16 port,
                              const char* path,
                              char* out,
                              rh_size cap,
                              rh_size* out_len,
                              rh_u32 timeout_ms)
{
    if (host == NULL || path == NULL || out == NULL || cap < 2u) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (timeout_ms == 0u) {
        timeout_ms = ROBOHUB_HTTP_TIMEOUT_MS;
    }
    out[0] = '\0';
    if (out_len != NULL) {
        *out_len = 0u;
    }

    RoboHubSocket socket = robohub_socket_create(ROBOHUB_SOCKET_TCP);
    if (socket == NULL) {
        return ROBOHUB_ERR_NOT_FOUND;
    }
    RoboHubError err = robohub_socket_connect(socket, host, port);
    if (ROBOHUB_FAILED(err)) {
        robohub_socket_close(socket);
        return err;
    }

    char request[256];
    rh_size at = 0;
    at = append(request, sizeof(request), at, "GET ");
    at = append(request, sizeof(request), at, path);
    at = append(request, sizeof(request), at, " HTTP/1.0\r\nHost: ");
    at = append(request, sizeof(request), at, host);
    at = append(request, sizeof(request), at, "\r\nConnection: close\r\n\r\n");
    if (at >= sizeof(request)) {
        robohub_socket_close(socket);
        return ROBOHUB_ERR_INVALID_ARG;
    }

    int sent = robohub_socket_send(socket, request, at);
    if (sent < 0) {
        robohub_socket_close(socket);
        return ROBOHUB_ERR_IO;
    }

    rh_size total = 0u;
    rh_bool timed_out = RH_TRUE;
    while (total + 1u < cap) {
        int got = robohub_socket_recv(socket, out + total, cap - total - 1u, timeout_ms);
        if (got <= 0) {
            break;
        }
        timed_out = RH_FALSE;
        total += (rh_size)got;
        out[total] = '\0';

        const char* body = header_end(out, total);
        if (body != NULL) {
            rh_i32 length = content_length(out, body);
            if (length >= 0 && (rh_size)length <= (rh_size)((out + total) - body)) {
                break;
            }
        }
    }
    robohub_socket_close(socket);

    if (total == 0u && timed_out) {
        return ROBOHUB_ERR_TIMEOUT;
    }

    const char* body = header_end(out, total);
    if (body == NULL) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    rh_size body_at = (rh_size)(body - out);
    rh_size body_len = total - body_at;
    rh_i32 length = content_length(out, body);
    if (length >= 0 && (rh_size)length < body_len) {
        body_len = (rh_size)length;
    }
    memmove(out, out + body_at, body_len);
    out[body_len] = '\0';
    if (out_len != NULL) {
        *out_len = body_len;
    }
    return ROBOHUB_OK;
}
