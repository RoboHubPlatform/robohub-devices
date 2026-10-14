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

/* Read one of the inverter's total figures. The figures live in the document's
 * `total` object — the channels under `0`, `1`, … are the DC inputs and the
 * total is the inverter — so the total is read, and a document that does not
 * group them falls back to a top-level key. */
static rh_bool hoymiles_figure(const char* body, const char* key, rh_f64* out)
{
    char path[64];
    rh_size at = 0u;
    const char* prefix = "total/";
    for (const char* c = prefix; *c != '\0' && at + 1u < sizeof(path); c++) {
        path[at++] = *c;
    }
    for (const char* c = key; *c != '\0' && at + 3u < sizeof(path); c++) {
        path[at++] = *c;
    }
    path[at++] = '/';
    path[at++] = 'v';
    path[at] = '\0';
    if (robohub_json_number(body, path, out)) {
        return RH_TRUE;
    }
    /* Without a total group, the same key at the top level. */
    char fallback[64];
    at = 0u;
    for (const char* c = key; *c != '\0' && at + 3u < sizeof(fallback); c++) {
        fallback[at++] = *c;
    }
    fallback[at++] = '/';
    fallback[at++] = 'v';
    fallback[at] = '\0';
    return robohub_json_number(body, fallback, out);
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
    if (hoymiles_figure(dev->body, "Power", &value)) {
        out->pv_power_w = (rh_f32)value;
        valid |= ROBOHUB_POWER_PV_POWER;
    }
    if (hoymiles_figure(dev->body, "YieldDay", &value)) {
        out->energy_today_kwh = (rh_f32)value;
        valid |= ROBOHUB_POWER_ENERGY_TODAY;
    }
    if (hoymiles_figure(dev->body, "YieldTotal", &value)) {
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
