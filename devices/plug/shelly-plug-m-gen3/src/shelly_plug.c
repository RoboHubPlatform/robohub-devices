/*
 * shelly_plug.c — a Shelly Plug M Gen3 over HTTP RPC or MQTT.
 *
 * Both transports end in the same parse of the status document and the same
 * cached switch state. HTTP is a GET to Shelly's RPC; MQTT is a publish to the
 * device's rpc topic, with the broker delivering the status topics back. The
 * topic prefix and the RPC source come from the configuration.
 */
#include "shelly_plug.h"
#include "robohub/protocols/http_client.h"
#include "robohub/protocols/json.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define SHELLY_DEFAULT_SOURCE "robohub"

/* The topic prefix the plug publishes under: the one it was configured with,
 * or its device id, which is Shelly's default. */
static const char* shelly_prefix(const RoboHubShelly* plug)
{
    return plug->config.topic_prefix != NULL ? plug->config.topic_prefix
                                             : plug->config.device_id;
}

static const char* shelly_source(const RoboHubShelly* plug)
{
    return plug->config.source != NULL ? plug->config.source : SHELLY_DEFAULT_SOURCE;
}

/* A source is written into a JSON string as it is; refuse what would end the
 * string or need escaping. */
static rh_bool source_is_plain(const char* source)
{
    if (source[0] == '\0') {
        return RH_FALSE;
    }
    for (const char* c = source; *c != '\0'; c++) {
        if (*c == '"' || *c == '\\' || (unsigned char)*c < 0x20u) {
            return RH_FALSE;
        }
    }
    return RH_TRUE;
}

/* The end of the JSON value that starts at `p` (an object or an array), just
 * past its closing bracket, or NULL when the document ends first. Strings are
 * skipped whole, so a bracket inside one is not counted. */
static const char* value_end(const char* p)
{
    int depth = 0;
    while (*p != '\0') {
        char c = *p;
        if (c == '"') {
            p++;
            while (*p != '\0' && *p != '"') {
                if (*p == '\\' && p[1] != '\0') {
                    p++;
                }
                p++;
            }
            if (*p == '\0') {
                return NULL;
            }
        } else if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            depth--;
            if (depth == 0) {
                return p + 1;
            }
        }
        p++;
    }
    return NULL;
}

/* The object that is the value of the member `"switch:0"`, or NULL. The member
 * is found where a key stands (after `{` or `,`), so the name inside a string
 * value — an event's `"component":"switch:0"` — is not taken for it. */
static const char* find_switch_object(const char* json)
{
    static const char key[] = "\"switch:0\"";
    const char* p = json;
    char previous = '\0';
    while (*p != '\0') {
        if (*p == '"') {
            if (strncmp(p, key, sizeof(key) - 1u) == 0 &&
                (previous == '{' || previous == ',')) {
                const char* after = p + sizeof(key) - 1u;
                while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') {
                    after++;
                }
                if (*after == ':') {
                    after++;
                    while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') {
                        after++;
                    }
                    return *after == '{' ? after : NULL;
                }
            }
            /* Skip the string whole. */
            p++;
            while (*p != '\0' && *p != '"') {
                if (*p == '\\' && p[1] != '\0') {
                    p++;
                }
                p++;
            }
            if (*p == '\0') {
                return NULL;
            }
            previous = '"';
            p++;
            continue;
        }
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            previous = *p;
        }
        p++;
    }
    return NULL;
}

/* Read the switch status object, the shape `<prefix>/status/switch:0` carries:
 * `output`, `apower`, `voltage`, `current` and `aenergy.total` (Wh). A field
 * that is absent — a NotifyStatus carries only what changed — leaves the cached
 * value as it was. */
static void parse_switch(RoboHubShelly* plug, const char* json)
{
    rh_f64 value = 0;
    rh_bool on = RH_FALSE;
    if (robohub_json_bool(json, "output", &on)) {
        plug->on = on;
    }
    if (robohub_json_number(json, "apower", &value)) {
        plug->power_w = (rh_f32)value;
    }
    if (robohub_json_number(json, "voltage", &value)) {
        plug->voltage_v = (rh_f32)value;
    }
    if (robohub_json_number(json, "current", &value)) {
        plug->current_a = (rh_f32)value;
    }
    if (robohub_json_number(json, "aenergy/total", &value)) {
        plug->energy_total_kwh = (rh_f32)(value / 1000.0);
    }
}

/* Read whichever status document the plug sent. `Shelly.GetStatus` over HTTP
 * and a `NotifyStatus` on `<prefix>/events/rpc` hold the switch under
 * `switch:0` (the second inside `params`); `<prefix>/status/switch:0` is the
 * switch object itself. The switch object is read on its own, bounded, so a key
 * it lacks is not found in a component after it. A notification without
 * `switch:0` — another component's status, or an event — is not the switch's. */
// @req DEVS-PLUG-0001, DEVS-PLUG-0004
static void parse_status(RoboHubShelly* plug, const char* json)
{
    const char* object = find_switch_object(json);
    if (object == NULL) {
        rh_bool is_switch_status = RH_TRUE;
        char method[32];
        if (robohub_json_string(json, "method", method, sizeof(method))) {
            is_switch_status = RH_FALSE;    /* a notification about something else */
        }
        if (is_switch_status) {
            parse_switch(plug, json);
        }
        return;
    }
    const char* end = value_end(object);
    if (end == NULL) {
        return;                              /* a truncated document */
    }
    rh_size len = (rh_size)(end - object);
    char bounded[768];
    if (len < sizeof(bounded)) {
        memcpy(bounded, object, len);
        bounded[len] = '\0';
        parse_switch(plug, bounded);
    } else {
        /* Larger than a switch status ever is; read it in place. */
        parse_switch(plug, object);
    }
}

static RoboHubError shelly_get(RoboHubPlug* self, rh_bool* on)
{
    RoboHubShelly* plug = (RoboHubShelly*)self->context;
    if (on == NULL || !plug->ready) {
        return ROBOHUB_ERR_STATE;
    }
    *on = plug->on;
    return ROBOHUB_OK;
}

// @req DEVS-PLUG-0002
static RoboHubError shelly_set(RoboHubPlug* self, rh_bool on)
{
    RoboHubShelly* plug = (RoboHubShelly*)self->context;
    if (!plug->ready) {
        return ROBOHUB_ERR_STATE;
    }
    if (plug->config.transport == ROBOHUB_SHELLY_HTTP) {
        char path[128];
        snprintf(path, sizeof(path), "/rpc/Switch.Set?id=0&on=%s",
                 on ? "true" : "false");
        RoboHubError err = robohub_http_get(plug->config.host, plug->config.port,
                                            path, plug->buffer, sizeof(plug->buffer),
                                            NULL, ROBOHUB_HTTP_TIMEOUT_MS);
        if (ROBOHUB_FAILED(err)) {
            return err;
        }
    } else {
        if (plug->config.mqtt == NULL) {
            return ROBOHUB_ERR_STATE;
        }
        char topic[160];
        char payload[192];
        int n = snprintf(topic, sizeof(topic), "%s/rpc", shelly_prefix(plug));
        if (n < 0 || (rh_size)n >= sizeof(topic)) {
            return ROBOHUB_ERR_INVALID_ARG;
        }
        n = snprintf(payload, sizeof(payload),
                     "{\"id\":1,\"src\":\"%s\",\"method\":\"Switch.Set\",\"params\":{\"id\":0,\"on\":%s}}",
                     shelly_source(plug), on ? "true" : "false");
        if (n < 0 || (rh_size)n >= sizeof(payload)) {
            return ROBOHUB_ERR_INVALID_ARG;
        }
        RoboHubError err = robohub_mqtt_publish(plug->config.mqtt, topic, payload, RH_FALSE);
        if (ROBOHUB_FAILED(err)) {
            return err;
        }
    }
    plug->on = on;
    return ROBOHUB_OK;
}

static RoboHubError shelly_read_power(RoboHubPlug* self, RoboHubPowerReading* out)
{
    RoboHubShelly* plug = (RoboHubShelly*)self->context;
    return robohub_shelly_power(plug, out);
}

static RoboHubError shelly_init(RoboHubPlug* self)
{
    RoboHubShelly* plug = (RoboHubShelly*)self->context;
    plug->ready = RH_TRUE;
    return ROBOHUB_OK;
}

static RoboHubError shelly_deinit(RoboHubPlug* self)
{
    RoboHubShelly* plug = (RoboHubShelly*)self->context;
    plug->ready = RH_FALSE;
    return ROBOHUB_OK;
}

RoboHubError robohub_shelly_init(RoboHubShelly* plug, const RoboHubShellyConfig* config)
{
    if (plug == NULL || config == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (config->transport == ROBOHUB_SHELLY_HTTP && config->host == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (config->transport == ROBOHUB_SHELLY_MQTT) {
        const char* prefix = config->topic_prefix != NULL ? config->topic_prefix
                                                          : config->device_id;
        /* The longest topic is `<prefix>/status/switch:0`, in a 160-byte buffer. */
        if (prefix == NULL || prefix[0] == '\0' || strlen(prefix) > 128u) {
            return ROBOHUB_ERR_INVALID_ARG;
        }
        if (config->source != NULL &&
            (!source_is_plain(config->source) || strlen(config->source) > 64u)) {
            return ROBOHUB_ERR_INVALID_ARG;
        }
    }
    memset(plug, 0, sizeof(*plug));
    plug->config = *config;
    plug->interface.name = "shelly-plug-m-gen3";
    plug->interface.context = plug;
    plug->interface.init = shelly_init;
    plug->interface.deinit = shelly_deinit;
    plug->interface.get = shelly_get;
    plug->interface.set = shelly_set;
    plug->interface.read_power = shelly_read_power;
    return shelly_init(&plug->interface);
}

RoboHubError robohub_shelly_poll(RoboHubShelly* plug)
{
    if (plug == NULL || !plug->ready) {
        return ROBOHUB_ERR_STATE;
    }
    if (plug->config.transport == ROBOHUB_SHELLY_HTTP) {
        RoboHubError err = robohub_http_get(plug->config.host, plug->config.port,
                                            "/rpc/Shelly.GetStatus", plug->buffer,
                                            sizeof(plug->buffer), NULL,
                                            ROBOHUB_HTTP_TIMEOUT_MS);
        if (ROBOHUB_FAILED(err)) {
            return err;
        }
        parse_status(plug, plug->buffer);
        return ROBOHUB_OK;
    }
    /* MQTT is push: the broker delivers status, so poll only subscribes. The
     * plug publishes its switch status on `status/switch:0` and NotifyStatus on
     * `events/rpc`, each when its option is on; both are asked for. */
    if (plug->config.mqtt == NULL || plug->config.mqtt->subscribe == NULL) {
        return ROBOHUB_ERR_STATE;
    }
    static const char* const suffixes[] = { "status/switch:0", "events/rpc" };
    for (rh_size i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        char topic[160];
        int n = snprintf(topic, sizeof(topic), "%s/%s", shelly_prefix(plug), suffixes[i]);
        if (n < 0 || (rh_size)n >= sizeof(topic)) {
            return ROBOHUB_ERR_INVALID_ARG;
        }
        RoboHubError err = plug->config.mqtt->subscribe(plug->config.mqtt, topic);
        if (ROBOHUB_FAILED(err)) {
            return err;
        }
    }
    return ROBOHUB_OK;
}

RoboHubError robohub_shelly_ingest_status(RoboHubShelly* plug, const char* payload)
{
    if (plug == NULL || payload == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    parse_status(plug, payload);
    return ROBOHUB_OK;
}

RoboHubError robohub_shelly_power(RoboHubShelly* plug, RoboHubPowerReading* out)
{
    if (plug == NULL || out == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->device = plug->config.id;
    out->kind = ROBOHUB_DEVICE_SWITCH;
    out->status = 0;
    out->valid = ROBOHUB_POWER_LOAD_POWER | ROBOHUB_POWER_ENERGY_TOTAL;
    out->load_power_w = plug->power_w;
    out->energy_total_kwh = plug->energy_total_kwh;
    return ROBOHUB_OK;
}
