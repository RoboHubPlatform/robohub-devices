/**
 * @file key_file.h
 * @brief The gateway node's device key file: who may read it, and what it holds.
 *
 * A host node takes its device key from the file RH_SECURE_KEY_FILE names, as
 * `<epoch>:<32 hex digits>` or the 32 hex digits alone (epoch 0). The key opens
 * the board's sessions with its gateway, so a file another user of the machine
 * can read is a key that user has, and one another user can write is a key that
 * user chooses. The node refuses either before it reads a byte of it: the file
 * must be a regular file, owned by the user the node runs as, with no permission
 * for its group or for others — what `chmod 600` leaves. A rotated key is written
 * back the same way (main.c's key_file_store), so the file stays so.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#ifndef ROBOHUB_GATEWAY_NODE_KEY_FILE_H
#define ROBOHUB_GATEWAY_NODE_KEY_FILE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What reading a key file found. */
typedef enum {
    RH_KEY_FILE_OK = 0,
    /** There is no file at the path, or it could not be opened. */
    RH_KEY_FILE_MISSING,
    /** The path is not a regular file (a directory, a device, a pipe). */
    RH_KEY_FILE_NOT_REGULAR,
    /** The file belongs to another user than the one the node runs as. */
    RH_KEY_FILE_NOT_OWNER,
    /** The file's group or others have a permission on it. */
    RH_KEY_FILE_TOO_OPEN,
    /** The file is not `<epoch>:<hex>` or `<hex>` of the key's length. */
    RH_KEY_FILE_BAD_CONTENT,
} RhKeyFileStatus;

/**
 * Read the key file at `path` into `key` (`key_size` bytes) and `epoch`, after
 * checking who may read it. On anything but RH_KEY_FILE_OK, `why` holds one line
 * saying what is wrong and how to put it right (for a file that is too open,
 * `chmod 600 <path>`), and `key` holds nothing of the file. The file's text is
 * cleared from memory before this returns.
 */
RhKeyFileStatus rh_key_file_read(const char* path, rh_u32* epoch, rh_u8* key, rh_size key_size,
                                 char* why, rh_size why_size);

/**
 * Exactly `n` bytes of hex, ignoring a trailing line break; anything else is
 * refused, so a file that is short or has a stray character is no key.
 */
rh_bool rh_key_hex_bytes(const char* text, rh_u8* out, rh_size n);

/** A key file's text: `<epoch>:<hex>`, or the hex alone, which is epoch 0. */
rh_bool rh_key_file_parse(const char* text, rh_u32* epoch, rh_u8* key, rh_size n);

#ifdef __cplusplus
}
#endif

#endif
