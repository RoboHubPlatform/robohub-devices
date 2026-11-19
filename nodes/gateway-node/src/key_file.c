/**
 * @file key_file.c
 * @brief The gateway node's device key file: who may read it, and what it holds.
 *
 * See key_file.h. The checks are made on the file that was opened (fstat on its
 * descriptor), not on the path before it is opened, so the file checked is the
 * file read.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#if (defined(__unix__) || defined(__APPLE__)) && !defined(_POSIX_C_SOURCE) && !defined(_GNU_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "key_file.h"

#include <stdio.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#  include <errno.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  define RH_KEY_FILE_POSIX 1
#endif

/* The longest key file there is: a ten-digit epoch, a colon, the hex, a line
 * break; and room to see that a longer file is longer. */
#define RH_KEY_FILE_TEXT 80

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

rh_bool rh_key_hex_bytes(const char* text, rh_u8* out, rh_size n)
{
    for (rh_size i = 0u; i < n; i++) {
        int hi = hex_digit(text[2u * i]);
        int lo = (hi < 0) ? -1 : hex_digit(text[2u * i + 1u]);
        if (lo < 0) {
            return RH_FALSE;
        }
        out[i] = (rh_u8)((hi << 4) | lo);
    }
    const char* rest = text + 2u * n;
    while (*rest == '\n' || *rest == '\r') {
        rest++;
    }
    return *rest == '\0' ? RH_TRUE : RH_FALSE;
}

rh_bool rh_key_file_parse(const char* text, rh_u32* epoch, rh_u8* key, rh_size n)
{
    *epoch = 0u;
    const char* colon = strchr(text, ':');
    if (colon != NULL) {
        if (colon == text) {
            return RH_FALSE;
        }
        rh_u32 value = 0u;
        for (const char* p = text; p < colon; p++) {
            if (*p < '0' || *p > '9' || value > 400000000u) {
                return RH_FALSE;
            }
            value = value * 10u + (rh_u32)(*p - '0');
        }
        *epoch = value;
        text = colon + 1;
    }
    return rh_key_hex_bytes(text, key, n);
}

// @req DEVS-NODE-0005, DEVS-NODE-0007
RhKeyFileStatus rh_key_file_read(const char* path, rh_u32* epoch, rh_u8* key, rh_size key_size,
                                 char* why, rh_size why_size)
{
    char text[RH_KEY_FILE_TEXT + 1];
    size_t got = 0u;
    RhKeyFileStatus status = RH_KEY_FILE_OK;
    *epoch = 0u;
    if (why_size > 0u) {
        why[0] = '\0';
    }
    if (path == NULL || path[0] == '\0') {
        snprintf(why, why_size, "RH_SECURE_KEY_FILE names no key file");
        return RH_KEY_FILE_MISSING;
    }

#if defined(RH_KEY_FILE_POSIX)
    int fd = open(path, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        snprintf(why, why_size, "the key file %s cannot be opened: %s", path, strerror(errno));
        return RH_KEY_FILE_MISSING;
    }
    struct stat info;
    if (fstat(fd, &info) != 0) {
        snprintf(why, why_size, "the key file %s cannot be examined: %s", path, strerror(errno));
        status = RH_KEY_FILE_MISSING;
    } else if (!S_ISREG(info.st_mode)) {
        snprintf(why, why_size, "the key file %s is not a regular file", path);
        status = RH_KEY_FILE_NOT_REGULAR;
    } else if (info.st_uid != geteuid()) {
        snprintf(why, why_size,
                 "the key file %s belongs to uid %lu, not to this node's user (uid %lu); "
                 "give it to that user (chown) and make it owner-only (chmod 600 %s)",
                 path, (unsigned long)info.st_uid, (unsigned long)geteuid(), path);
        status = RH_KEY_FILE_NOT_OWNER;
    } else if ((info.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        snprintf(why, why_size,
                 "the key file %s can be read or written by other users (mode %04lo); "
                 "make it owner-only: chmod 600 %s",
                 path, (unsigned long)(info.st_mode & 07777), path);
        status = RH_KEY_FILE_TOO_OPEN;
    } else {
        ssize_t n;
        while (got < sizeof(text) - 1u &&
               (n = read(fd, text + got, sizeof(text) - 1u - got)) != 0) {
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            got += (size_t)n;
        }
    }
    (void)close(fd);
#else
    FILE* file = fopen(path, "r");
    if (file == NULL) {
        snprintf(why, why_size, "the key file %s cannot be opened", path);
        return RH_KEY_FILE_MISSING;
    }
    got = fread(text, 1u, sizeof(text) - 1u, file);
    fclose(file);
#endif

    text[got] = '\0';
    if (status == RH_KEY_FILE_OK && !rh_key_file_parse(text, epoch, key, key_size)) {
        snprintf(why, why_size,
                 "the key file %s does not hold a device key: it should be <epoch>:<%u hex digits>, "
                 "or the %u hex digits alone",
                 path, (unsigned)(2u * key_size), (unsigned)(2u * key_size));
        status = RH_KEY_FILE_BAD_CONTENT;
    }
    if (status != RH_KEY_FILE_OK) {
        memset(key, 0, key_size);
        *epoch = 0u;
    }
    memset(text, 0, sizeof(text));
    return status;
}
