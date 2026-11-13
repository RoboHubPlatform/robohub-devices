/*
 * hoymiles.c — a Hoymiles microinverter through an OpenDTU.
 *
 * One GET, three fields out of the document. The figures live in the document's
 * `total` object — `"total":{"Power":{"v":…},"YieldDay":{"v":…},"YieldTotal":{…}}`
 * — so the total is read rather than a channel, and a field that is not there is
 * absent from the reading rather than zero.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "hoymiles.h"
#include "robohub/protocols/http_client.h"
#include "robohub/protocols/json.h"

#include <stddef.h>
#include <string.h>

/* `total/<key>/<leaf>`, or `<key>/<leaf>` without the group. */
static void hoymiles_path(char* path, rh_size cap, rh_bool grouped, const char* key, const char* leaf)
{
    rh_size at = 0u;
    const char* parts[3] = { grouped ? "total/" : "", key, leaf };
    for (rh_size p = 0u; p < 3u; p++) {
        if (p == 2u && at + 1u < cap) {
            path[at++] = '/';
        }
        for (const char* c = parts[p]; *c != '\0' && at + 1u < cap; c++) {
            path[at++] = *c;
        }
    }
    path[at] = '\0';
}

/* Read one of the inverter's total figures, in the reading's unit. The figures
 * live in the document's `total` object — the channels under `0`, `1`, … are
 * the DC inputs and the total is the inverter — and a document that does not
 * group them falls back to a top-level key. Each figure carries its unit (`u`):
 * OpenDTU counts the day's yield in Wh and the lifetime yield in kWh, and a
 * figure whose unit is the thousand-times one is scaled to the reading's. With
 * no unit, the one OpenDTU documents for the figure is taken. */
static rh_bool hoymiles_figure(const char* body, const char* key, const char* unit,
                               const char* default_unit, rh_f64* out)
{
    char path[64];
    rh_bool grouped = RH_TRUE;
    hoymiles_path(path, sizeof(path), RH_TRUE, key, "v");
    if (!robohub_json_number(body, path, out)) {
        grouped = RH_FALSE;
        hoymiles_path(path, sizeof(path), RH_FALSE, key, "v");
        if (!robohub_json_number(body, path, out)) {
            return RH_FALSE;
        }
    }
    char given[8];
    hoymiles_path(path, sizeof(path), grouped, key, "u");
    if (!robohub_json_string(body, path, given, sizeof(given))) {
        strncpy(given, default_unit, sizeof(given) - 1u);
        given[sizeof(given) - 1u] = '\0';
    }
    if (strcmp(given, unit) == 0) {
        return RH_TRUE;
    }
    /* W against kW, Wh against kWh: one is the other with a k in front. */
    if (given[0] == 'k' && strcmp(given + 1, unit) == 0) {
        *out *= 1000.0;
        return RH_TRUE;
    }
    if (unit[0] == 'k' && strcmp(unit + 1, given) == 0) {
        *out /= 1000.0;
        return RH_TRUE;
    }
    return RH_FALSE; /* a unit this reading cannot be put in */
}

/* -------------------------------------------------------------------------- */
/*  Interface operations                                                      */
/* -------------------------------------------------------------------------- */

static RoboHubError hoymiles_op_init(RoboHubInverter* self)
{
    RoboHubHoymiles* dev = (RoboHubHoymiles*)self->context;
    if (dev->host == NULL || dev->port == 0u) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    dev->ready = RH_TRUE;
    return ROBOHUB_OK;
}

static RoboHubError hoymiles_op_deinit(RoboHubInverter* self)
{
    ((RoboHubHoymiles*)self->context)->ready = RH_FALSE;
    return ROBOHUB_OK;
}

static RoboHubError hoymiles_op_read(RoboHubInverter* self, RoboHubPowerReading* out)
{
    RoboHubHoymiles* dev = (RoboHubHoymiles*)self->context;
    if (out == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (!dev->ready) {
        return ROBOHUB_ERR_STATE;
    }

    const char* path = dev->path != NULL ? dev->path : ROBOHUB_HOYMILES_PATH;
    rh_size length = 0u;
    RoboHubError err = robohub_http_get(dev->host, dev->port, path,
                                        dev->body, sizeof(dev->body), &length, 0u);
    if (ROBOHUB_FAILED(err)) {
        return err;
    }

    memset(out, 0, sizeof(*out));
    out->device = dev->id;
    out->kind = ROBOHUB_DEVICE_INVERTER;
    out->status = 0u;

    rh_u16 valid = 0u;
    rh_f64 value = 0;
    if (hoymiles_figure(dev->body, "Power", "W", "W", &value)) {
        out->pv_power_w = (rh_f32)value;
        valid |= ROBOHUB_POWER_PV_POWER;
    }
    if (hoymiles_figure(dev->body, "YieldDay", "kWh", "Wh", &value)) {
        out->energy_today_kwh = (rh_f32)value;
        valid |= ROBOHUB_POWER_ENERGY_TODAY;
    }
    if (hoymiles_figure(dev->body, "YieldTotal", "kWh", "kWh", &value)) {
        out->energy_total_kwh = (rh_f32)value;
        valid |= ROBOHUB_POWER_ENERGY_TOTAL;
    }
    if (valid == 0u) {
        /* The document answered but held none of the figures: the wrong path, or
         * a firmware whose keys differ. That is a protocol result, not a zero. */
        return ROBOHUB_ERR_PROTOCOL;
    }
    out->valid = valid;
    return ROBOHUB_OK;
}

/* -------------------------------------------------------------------------- */
/*  Binding                                                                   */
/* -------------------------------------------------------------------------- */

RoboHubError robohub_hoymiles_init(RoboHubHoymiles* dev,
                                   const char* host,
                                   rh_u16 port,
                                   const char* path,
                                   RoboHubDeviceId id)
{
    if (dev == NULL || host == NULL || port == 0u) {
        return ROBOHUB_ERR_INVALID_ARG;
    }

    memset(dev, 0, sizeof(*dev));
    dev->host = host;
    dev->port = port;
    dev->path = path;
    dev->id = id;
    dev->ready = RH_FALSE;

    dev->interface.name = "hoymiles.inverter";
    dev->interface.context = dev;
    dev->interface.init = hoymiles_op_init;
    dev->interface.deinit = hoymiles_op_deinit;
    dev->interface.read = hoymiles_op_read;

    return hoymiles_op_init(&dev->interface);
}
