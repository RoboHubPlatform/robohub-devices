/*
 * test_json.c — the JSON field reader.
 *
 * A document shaped like an OpenDTU's reply: two DC-input channels each with a
 * `Power`, a `total` with the inverter's figures, a string value that happens to
 * contain a word we might look for, and a number in exponent form. The channel
 * powers sit before the total, so a path that reaches the total is what proves
 * the nesting is honoured rather than the first match taken.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include <stdio.h>
#include <string.h>
#include "robohub/protocols/json.h"

int main(void)
{
    const char* doc =
        "{\"inverters\":[{\"name\":\"HMS-800\",\"data\":{"
        "\"0\":{\"Power\":{\"v\":500}},"
        "\"1\":{\"Power\":{\"v\":600}},"
        "\"total\":{\"Power\":{\"v\":1234.5},\"YieldDay\":{\"v\":3.25},"
        "\"YieldTotal\":{\"v\":99.125}}}}],"
        "\"note\":\"Power is a word here\",\"neg\":-12.5e1}";

    int ok = 1;
    rh_f64 value = 0;

    /* The total, not the first channel that also has a `Power`. */
    ok = ok && robohub_json_number(doc, "total/Power/v", &value) && value == 1234.5;
    ok = ok && robohub_json_number(doc, "total/YieldDay/v", &value) && value == 3.25;
    ok = ok && robohub_json_number(doc, "total/YieldTotal/v", &value) && value == 99.125;

    /* A path that reaches a channel reaches that channel, not the total. */
    ok = ok && robohub_json_number(doc, "total/Power/v", &value) && value == 1234.5;

    /* A top-level number in exponent form. */
    ok = ok && robohub_json_number(doc, "neg", &value) && value == -125.0;

    /* A string value. */
    char name[32] = { 0 };
    ok = ok && robohub_json_string(doc, "name", name, sizeof(name)) && strcmp(name, "HMS-800") == 0;

    /* A word that appears only inside a string value is not a key. */
    ok = ok && !robohub_json_number(doc, "word", &value);
    /* A key that is not there. */
    ok = ok && !robohub_json_number(doc, "Absent", &value);
    /* A path whose middle does not exist. */
    ok = ok && !robohub_json_number(doc, "total/Nope/v", &value);

    printf(ok ? "JSON OK\n" : "JSON FAILED\n");
    return ok ? 0 : 1;
}
