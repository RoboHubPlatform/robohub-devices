/*
 * test_json.c — the small reader's fields, including a boolean.
 */
#include "robohub/protocols/json.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    const char* doc = "{\"switch:0\":{\"output\":true,\"apower\":12.5,\"aenergy\":{\"total\":1234.5}}}";
    rh_bool on = RH_FALSE;
    assert(robohub_json_bool(doc, "switch:0/output", &on));
    assert(on == RH_TRUE);

    on = RH_TRUE;
    assert(robohub_json_bool("{\"output\":false}", "output", &on));
    assert(on == RH_FALSE);

    rh_f64 power = 0;
    assert(robohub_json_number(doc, "switch:0/apower", &power));
    assert(power > 12.49 && power < 12.51);

    rh_f64 energy = 0;
    assert(robohub_json_number(doc, "switch:0/aenergy/total", &energy));
    assert(energy > 1234.49 && energy < 1234.51);

    assert(!robohub_json_bool("{\"output\":\"true\"}", "output", &on));
    return 0;
}
