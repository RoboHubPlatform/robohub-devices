/**
 * @file json.h
 * @brief A small reader for the fields of a JSON document.
 *
 * OpenDTU answers with JSON, and a device integration needs a handful of numbers
 * out of it — a power, a yield. This is not a parser that builds a tree; it is a
 * lookup for one field at a time, named by its path, which is the whole of what a
 * device needs and small enough to carry on a board.
 *
 * A field is named the way it is nested: `"total/Power/v"` reads the `v` member
 * of `Power` of `total`, which is how an OpenDTU's figures sit. It understands
 * enough of the grammar to not be fooled: a key is a string in a member position
 * (the non-space before it is `{` or `,`), and string values are skipped whole,
 * so a key name inside a string value is not mistaken for the key.
 *
 * @project RoboHub Platform
 * @author Aurel Miron
 * @version 0.1
 * @date 2026
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef JSON_H
#define JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma once

#include "types.h"

/**
 * @brief Read the number at `path`, a slash-separated key path.
 *
 * `"12"` is a top-level key; `"total/Power/v"` is a member of a member of a
 * member. The first matching key at each step wins, which is what a document
 * holding one device's figures wants. Returns `RH_TRUE` and writes `out` when
 * the field is found and its value is a number.
 */
rh_bool robohub_json_number(const char* json, const char* path, rh_f64* out);

/**
 * @brief Copy the string at `path` into `out`, NUL-terminated.
 *
 * Returns `RH_TRUE` when the field is found and its value is a string. A value
 * longer than `cap` is truncated and still succeeds; `cap` of zero is refused.
 */
rh_bool robohub_json_string(const char* json, const char* path, char* out, rh_size cap);

#ifdef __cplusplus
}
#endif

#endif /* JSON_H */
