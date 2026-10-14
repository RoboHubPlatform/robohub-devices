/*
 * json.c — a small reader for the fields of a JSON document.
 *
 * A scan, not a tree. It walks the text once, skipping over string contents so a
 * key cannot be found inside a value, and stops at the first member whose name
 * is the one asked for. The number is parsed by hand rather than with the C
 * library's float parser, because `strtod` is a large thing to pull onto a board
 * for a handful of figures.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "json.h"

#include <stddef.h>
#include <string.h>

static rh_bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static const char* skip_ws(const char* p)
{
    while (p != NULL && *p != '\0' && is_space(*p)) {
        p++;
    }
    return p;
}

/* Find the member named `key` and return the pointer just past its `:`. A key is
 * a string whose nearest non-space predecessor is `{` or `,` — the member
 * position — which is what keeps a key name inside a string value from being
 * taken for a key. */
static const char* json_find_key(const char* p, const char* key)
{
    if (p == NULL || key == NULL) {
        return NULL;
    }
    rh_size key_len = strlen(key);
    char previous = '\0';

    while (*p != '\0') {
        char c = *p;
        if (c == '"') {
            const char* start = p + 1;
            const char* end = start;
            while (*end != '\0' && *end != '"') {
                if (*end == '\\' && end[1] != '\0') {
                    end++;
                }
                end++;
            }
            rh_size len = (rh_size)(end - start);
            if (len == key_len && strncmp(start, key, key_len) == 0 &&
                (previous == '{' || previous == ',')) {
                const char* after = (*end != '\0') ? end + 1 : end;
                after = skip_ws(after);
                if (*after == ':') {
                    return skip_ws(after + 1);
                }
            }
            p = (*end != '\0') ? end + 1 : end;
            previous = '"';
            continue;
        }
        if (!is_space(c)) {
            previous = c;
        }
        p++;
    }
    return NULL;
}

/* A number at `p`, parsed without the C library's float parser. Returns false
 * when there is no number there. */
static rh_bool json_parse_number(const char* p, rh_f64* out)
{
    rh_bool negative = RH_FALSE;
    if (*p == '-') {
        negative = RH_TRUE;
        p++;
    } else if (*p == '+') {
        p++;
    }
    if (!((*p >= '0' && *p <= '9') || *p == '.')) {
        return RH_FALSE;
    }

    rh_f64 value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (rh_f64)(*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        /* The fraction is read as whole digits and divided once, not added a
         * digit at a time: 1*0.1 + 2*0.01 + 5*0.001 is not 0.125 in binary, and
         * a figure that comes back a hair wrong is a figure that compares
         * unequal to the number it is. */
        rh_u64 fraction = 0;
        rh_u32 digits = 0;
        while (*p >= '0' && *p <= '9') {
            fraction = fraction * 10u + (rh_u64)(*p - '0');
            digits++;
            p++;
        }
        rh_f64 divisor = 1;
        for (rh_u32 i = 0; i < digits; i++) {
            divisor *= 10;
        }
        if (divisor > 0) {
            value += (rh_f64)fraction / divisor;
        }
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        rh_bool exp_negative = RH_FALSE;
        if (*p == '+') {
            p++;
        } else if (*p == '-') {
            exp_negative = RH_TRUE;
            p++;
        }
        rh_i32 exponent = 0;
        while (*p >= '0' && *p <= '9') {
            exponent = exponent * 10 + (*p - '0');
            p++;
        }
        while (exponent-- > 0) {
            value = exp_negative ? value / 10 : value * 10;
        }
    }
    *out = negative ? -value : value;
    return RH_TRUE;
}

static rh_bool json_parse_string(const char* p, char* out, rh_size cap)
{
    if (p == NULL || out == NULL || cap == 0u || *p != '"') {
        return RH_FALSE;
    }
    p++;
    rh_size n = 0;
    while (*p != '\0' && *p != '"') {
        char c = *p;
        if (c == '\\' && p[1] != '\0') {
            p++;
            c = *p;
        }
        if (n + 1u < cap) {
            out[n++] = c;
        }
        p++;
    }
    if (*p != '"') {
        return RH_FALSE;
    }
    out[n] = '\0';
    return RH_TRUE;
}

/* Walk a slash-separated key path and return the pointer to the value of the
 * last key. Each step reuses the single-key search, so the grammar is understood
 * once. */
static const char* json_find_path(const char* json, const char* path)
{
    if (path == NULL || path[0] == '\0') {
        return json;
    }
    const char* p = json;
    const char* segment = path;
    char key[64];

    while (*segment != '\0') {
        rh_size k = 0u;
        while (*segment != '\0' && *segment != '/' && k + 1u < sizeof(key)) {
            key[k++] = *segment++;
        }
        key[k] = '\0';
        if (*segment == '/') {
            segment++;
        }
        p = json_find_key(p, key);
        if (p == NULL) {
            return NULL;
        }
    }
    return p;
}

rh_bool robohub_json_number(const char* json, const char* path, rh_f64* out)
{
    if (json == NULL || path == NULL || out == NULL) {
        return RH_FALSE;
    }
    const char* p = json_find_path(json, path);
    if (p == NULL) {
        return RH_FALSE;
    }
    return json_parse_number(p, out);
}

rh_bool robohub_json_string(const char* json, const char* path, char* out, rh_size cap)
{
    if (json == NULL || path == NULL || out == NULL) {
        return RH_FALSE;
    }
    const char* p = json_find_path(json, path);
    if (p == NULL) {
        return RH_FALSE;
    }
    return json_parse_string(p, out, cap);
}
