/**
 * @file http_client.h
 * @brief A small HTTP GET over the network contract.
 *
 * A device reached over IP — an OpenDTU fronting a Hoymiles microinverter — is
 * read by asking it for a document. This is the asking: open a TCP socket
 * through `RoboHubNet`, send one GET, and hand back the body. It is deliberately
 * just enough for that, because a device integration needs one request to one
 * path, not a general client.
 *
 * The request asks for the connection to close, so the body ends when the server
 * is done; when the reply carries a length, that is used instead, so a server
 * that keeps the socket open is not waited on. A timeout bounds every receive,
 * because the caller shares a core with the scheduler.
 *
 * @project RoboHub Platform
 * @author Aurel Miron
 * @version 0.1
 * @date 2026
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma once

#include "types.h"
#include "error.h"
#include "robohub/net.h"

/** How long a single receive waits, in milliseconds, when none is given. */
#define ROBOHUB_HTTP_TIMEOUT_MS 3000u

/**
 * @brief GET `path` from `host:port` and copy the body into `out`.
 *
 * `out` receives the response body, NUL-terminated, and `out_len` its length
 * (either may be NULL for the length). `cap` bounds the whole response — headers
 * and body — so a document larger than the buffer is truncated rather than
 * overrunning it, and the call still succeeds.
 *
 * Returns `ROBOHUB_ERR_NOT_FOUND` when the host does not resolve or answer,
 * `ROBOHUB_ERR_TIMEOUT` when nothing arrives, and `ROBOHUB_ERR_PROTOCOL` when
 * the reply has no header terminator.
 */
RoboHubError robohub_http_get(const char* host,
                              rh_u16 port,
                              const char* path,
                              char* out,
                              rh_size cap,
                              rh_size* out_len,
                              rh_u32 timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_CLIENT_H */
