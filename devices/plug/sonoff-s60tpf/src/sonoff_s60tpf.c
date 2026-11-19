/*
 * sonoff_s60tpf.c — a Sonoff S60TPF running Tasmota, over MQTT.
 *
 * Tasmota splits what it says across topics: the switch answers on stat/RESULT
 * (JSON) and stat/POWER (a bare word) and repeats on tele/STATE, the meter
 * reports on tele/SENSOR, and the broker publishes the last will on tele/LWT.
 * Each is folded into one cached state, changing only what it carries. The
 * switch is driven by a publish to cmnd/POWER, and the cached switch follows
 * only the plug's own report. The broker login is the platform client's; no
 * credential is read or kept here (DEVS-0010).
 *
 * @project RoboHub Platform
 * @license MIT
 */
#include "sonoff_s60tpf.h"
#include "robohub/protocols/json.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* How many telemetry periods of silence make the plug unreachable. */
#define SONOFF_SILENT_PERIODS 3u

static rh_bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* `word`, between `start` and `end`, equals `want` ignoring letter case. */
static rh_bool equal_nocase(const char* start, rh_size len, const char* want)
{
    if (strlen(want) != len) {
        return RH_FALSE;
    }
    for (rh_size i = 0; i < len; i++) {
        if (lower(start[i]) != want[i]) {
            return RH_FALSE;
        }
    }
    return RH_TRUE;
}

static rh_bool equal_exact(const char* start, rh_size len, const char* want)
{
    return strlen(want) == len && strncmp(start, want, len) == 0;
}

// @req DEVS-PLUG-0005
rh_bool robohub_sonoff_parse_onoff(const char* word, rh_bool* on)
{
    if (word == NULL || on == NULL) {
        return RH_FALSE;
    }
    const char* start = word;
    while (is_space(*start)) {
        start++;
    }
    rh_size len = strlen(start);
    while (len > 0u && is_space(start[len - 1u])) {
        len--;
    }
    if (equal_nocase(start, len, "on") || equal_exact(start, len, "true") ||
        equal_exact(start, len, "1")) {
        *on = RH_TRUE;
        return RH_TRUE;
    }
    if (equal_nocase(start, len, "off") || equal_exact(start, len, "false") ||
        equal_exact(start, len, "0")) {
        *on = RH_FALSE;
        return RH_TRUE;
    }
    return RH_FALSE;
}

/* A prefix Tasmota can publish under and a subscription can name: not empty,
 * not too long for the topic buffers, not absolute, and no wildcard or space. */
// @req DEVS-PLUG-0006
static rh_bool prefix_is_valid(const char* prefix)
{
    if (prefix == NULL || prefix[0] == '\0' || prefix[0] == '/' ||
        strlen(prefix) > ROBOHUB_SONOFF_PREFIX_MAX) {
        return RH_FALSE;
    }
    for (const char* c = prefix; *c != '\0'; c++) {
        if (*c == '#' || *c == '+' || *c == ' ' || (unsigned char)*c < 0x20u) {
            return RH_FALSE;
        }
    }
    return RH_TRUE;
}

/* The payload is a JSON object: `{` first and `}` last, around whitespace. */
static rh_bool is_json_object(const char* payload)
{
    const char* p = payload;
    while (is_space(*p)) {
        p++;
    }
    if (*p != '{') {
        return RH_FALSE;
    }
    rh_size len = strlen(p);
    while (len > 0u && is_space(p[len - 1u])) {
        len--;
    }
    return len >= 2u && p[len - 1u] == '}';
}

/* Whether the document names a member `"POWER"` at all, whatever its value. */
static rh_bool has_power_key(const char* json)
{
    const char* p = json;
    while ((p = strstr(p, "\"POWER\"")) != NULL) {
        const char* after = p + 7;
        while (is_space(*after)) {
            after++;
        }
        if (*after == ':') {
            return RH_TRUE;
        }
        p = after;
    }
    return RH_FALSE;
}

typedef enum { POWER_ABSENT, POWER_READ, POWER_BAD } PowerField;

/* The switch in a RESULT or STATE document: `"POWER":"ON"` (also a boolean or
 * 1/0, which a manifest's onoff type accepts too). */
static PowerField json_power(const char* json, rh_bool* on)
{
    char word[16];
    if (robohub_json_string(json, "POWER", word, sizeof(word))) {
        return robohub_sonoff_parse_onoff(word, on) ? POWER_READ : POWER_BAD;
    }
    if (robohub_json_bool(json, "POWER", on)) {
        return POWER_READ;
    }
    rh_f64 number = 0;
    if (robohub_json_number(json, "POWER", &number)) {
        if (number == 1.0 || number == 0.0) {
            *on = number == 1.0 ? RH_TRUE : RH_FALSE;
            return POWER_READ;
        }
        return POWER_BAD;
    }
    return has_power_key(json) ? POWER_BAD : POWER_ABSENT;
}

static rh_u32 silence_limit_ms(const RoboHubSonoff* plug)
{
    rh_u32 period = plug->config.tele_period_s != 0u ? plug->config.tele_period_s
                                                     : ROBOHUB_SONOFF_DEFAULT_TELE_PERIOD_S;
    return period * 1000u * SONOFF_SILENT_PERIODS;
}

static void heard(RoboHubSonoff* plug)
{
    plug->heard = RH_TRUE;
    plug->offline = RH_FALSE;
    plug->heard_ms = plug->config.now_ms();
}

// @req DEVS-PLUG-0008
rh_bool robohub_sonoff_reachable(const RoboHubSonoff* plug)
{
    if (plug == NULL || !plug->ready || plug->offline) {
        return RH_FALSE;
    }
    rh_u32 silent = plug->config.now_ms() - plug->heard_ms;   /* wraps as the clock does */
    return silent <= silence_limit_ms(plug) ? RH_TRUE : RH_FALSE;
}

/* The meter, from tele/SENSOR's ENERGY object, in the units Tasmota reports
 * and the reading declares: W, V, A, kWh. */
// @req DEVS-PLUG-0005
static void parse_sensor(RoboHubSonoff* plug, const char* json)
{
    static const struct {
        const char* path;
        rh_u16 bit;
        size_t offset;
    } fields[] = {
        { "ENERGY/Power",   ROBOHUB_SONOFF_HAVE_POWER,        offsetof(RoboHubSonoff, power_w) },
        { "ENERGY/Voltage", ROBOHUB_SONOFF_HAVE_VOLTAGE,      offsetof(RoboHubSonoff, voltage_v) },
        { "ENERGY/Current", ROBOHUB_SONOFF_HAVE_CURRENT,      offsetof(RoboHubSonoff, current_a) },
        { "ENERGY/Today",   ROBOHUB_SONOFF_HAVE_ENERGY_TODAY, offsetof(RoboHubSonoff, energy_today_kwh) },
        { "ENERGY/Total",   ROBOHUB_SONOFF_HAVE_ENERGY_TOTAL, offsetof(RoboHubSonoff, energy_total_kwh) },
    };
    for (rh_size i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        rh_f64 value = 0;
        if (robohub_json_number(json, fields[i].path, &value)) {
            *(rh_f32*)((char*)plug + fields[i].offset) = (rh_f32)value;
            plug->have |= fields[i].bit;
        }
    }
}

static RoboHubError take_power(RoboHubSonoff* plug, PowerField field, rh_bool on)
{
    if (field == POWER_BAD) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    if (field == POWER_READ) {
        plug->on = on;
        plug->have |= ROBOHUB_SONOFF_HAVE_SWITCH;
    }
    heard(plug);
    return ROBOHUB_OK;
}

// @req DEVS-PLUG-0005, DEVS-PLUG-0006, DEVS-PLUG-0007, DEVS-PLUG-0008
RoboHubError robohub_sonoff_ingest(RoboHubSonoff* plug, const char* topic, const char* payload)
{
    if (plug == NULL || topic == NULL || payload == NULL || !plug->ready) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    rh_size prefix_len = strlen(plug->config.prefix);
    if (strncmp(topic, plug->config.prefix, prefix_len) != 0 || topic[prefix_len] != '/') {
        return ROBOHUB_ERR_INVALID_ARG;          /* not this plug's */
    }
    const char* suffix = topic + prefix_len + 1u;
    rh_bool on = RH_FALSE;

    if (strcmp(suffix, "stat/RESULT") == 0 || strcmp(suffix, "tele/STATE") == 0) {
        if (!is_json_object(payload)) {
            return ROBOHUB_ERR_PROTOCOL;
        }
        /* A RESULT answering another command has no POWER: nothing changes,
         * but the plug was heard. */
        PowerField field = json_power(payload, &on);
        return take_power(plug, field, on);
    }
    if (strcmp(suffix, "stat/POWER") == 0) {
        if (!robohub_sonoff_parse_onoff(payload, &on)) {
            return ROBOHUB_ERR_PROTOCOL;
        }
        return take_power(plug, POWER_READ, on);
    }
    if (strcmp(suffix, "tele/SENSOR") == 0) {
        if (!is_json_object(payload)) {
            return ROBOHUB_ERR_PROTOCOL;
        }
        parse_sensor(plug, payload);
        heard(plug);
        return ROBOHUB_OK;
    }
    if (strcmp(suffix, "tele/LWT") == 0) {
        if (strcmp(payload, "Online") == 0) {
            heard(plug);
            return ROBOHUB_OK;
        }
        if (strcmp(payload, "Offline") == 0) {
            plug->offline = RH_TRUE;
            return ROBOHUB_OK;
        }
        return ROBOHUB_ERR_PROTOCOL;
    }
    return ROBOHUB_OK;                           /* another topic of the plug's */
}

static RoboHubError sonoff_get(RoboHubPlug* self, rh_bool* on)
{
    RoboHubSonoff* plug = (RoboHubSonoff*)self->context;
    if (on == NULL || !robohub_sonoff_reachable(plug) ||
        (plug->have & ROBOHUB_SONOFF_HAVE_SWITCH) == 0u) {
        return ROBOHUB_ERR_STATE;
    }
    *on = plug->on;
    return ROBOHUB_OK;
}

/* A request, not a state: the cached switch moves when the plug reports it. */
// @req DEVS-PLUG-0007
static RoboHubError sonoff_set(RoboHubPlug* self, rh_bool on)
{
    RoboHubSonoff* plug = (RoboHubSonoff*)self->context;
    if (!plug->ready || plug->config.mqtt == NULL) {
        return ROBOHUB_ERR_STATE;
    }
    char topic[160];
    int n = snprintf(topic, sizeof(topic), "%s/cmnd/POWER", plug->config.prefix);
    if (n < 0 || (rh_size)n >= sizeof(topic)) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    return robohub_mqtt_publish(plug->config.mqtt, topic, on ? "true" : "false", RH_FALSE);
}

static RoboHubError sonoff_read_power(RoboHubPlug* self, RoboHubPowerReading* out)
{
    return robohub_sonoff_power((RoboHubSonoff*)self->context, out);
}

static RoboHubError sonoff_init(RoboHubPlug* self)
{
    RoboHubSonoff* plug = (RoboHubSonoff*)self->context;
    plug->ready = RH_TRUE;
    plug->heard_ms = plug->config.now_ms();
    return ROBOHUB_OK;
}

static RoboHubError sonoff_deinit(RoboHubPlug* self)
{
    RoboHubSonoff* plug = (RoboHubSonoff*)self->context;
    plug->ready = RH_FALSE;
    return ROBOHUB_OK;
}

// @req DEVS-PLUG-0006, DEVS-0010
RoboHubError robohub_sonoff_init(RoboHubSonoff* plug, const RoboHubSonoffConfig* config)
{
    if (plug == NULL || config == NULL || config->mqtt == NULL || config->now_ms == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (!prefix_is_valid(config->prefix)) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (config->tele_period_s != 0u &&
        (config->tele_period_s < 10u || config->tele_period_s > 3600u)) {
        return ROBOHUB_ERR_INVALID_ARG;          /* Tasmota's own range */
    }
    memset(plug, 0, sizeof(*plug));
    plug->config = *config;
    plug->interface.name = "sonoff-s60tpf";
    plug->interface.context = plug;
    plug->interface.init = sonoff_init;
    plug->interface.deinit = sonoff_deinit;
    plug->interface.get = sonoff_get;
    plug->interface.set = sonoff_set;
    plug->interface.read_power = sonoff_read_power;
    return sonoff_init(&plug->interface);
}

// @req DEVS-PLUG-0006, DEVS-PLUG-0008
RoboHubError robohub_sonoff_poll(RoboHubSonoff* plug)
{
    if (plug == NULL || !plug->ready || plug->config.mqtt == NULL ||
        plug->config.mqtt->subscribe == NULL) {
        return ROBOHUB_ERR_STATE;
    }
    static const char* const suffixes[] = {
        "stat/RESULT", "stat/POWER", "tele/STATE", "tele/SENSOR", "tele/LWT"
    };
    for (rh_size i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        char topic[160];
        int n = snprintf(topic, sizeof(topic), "%s/%s", plug->config.prefix, suffixes[i]);
        if (n < 0 || (rh_size)n >= sizeof(topic)) {
            return ROBOHUB_ERR_INVALID_ARG;
        }
        RoboHubError err = plug->config.mqtt->subscribe(plug->config.mqtt, topic);
        if (ROBOHUB_FAILED(err)) {
            return err;
        }
    }
    if (!plug->heard) {
        plug->heard_ms = plug->config.now_ms();  /* silence counts from here */
    }
    return ROBOHUB_OK;
}

// @req DEVS-PLUG-0005, DEVS-PLUG-0008
RoboHubError robohub_sonoff_power(RoboHubSonoff* plug, RoboHubPowerReading* out)
{
    if (plug == NULL || out == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!robohub_sonoff_reachable(plug) || !plug->heard) {
        return ROBOHUB_ERR_STATE;
    }
    out->device = plug->config.id;
    out->timestamp_ms = plug->heard_ms;
    out->kind = ROBOHUB_DEVICE_SWITCH;
    out->status = 0;
    if ((plug->have & ROBOHUB_SONOFF_HAVE_POWER) != 0u) {
        out->valid |= ROBOHUB_POWER_LOAD_POWER;
        out->load_power_w = plug->power_w;
    }
    if ((plug->have & ROBOHUB_SONOFF_HAVE_ENERGY_TODAY) != 0u) {
        out->valid |= ROBOHUB_POWER_ENERGY_TODAY;
        out->energy_today_kwh = plug->energy_today_kwh;
    }
    if ((plug->have & ROBOHUB_SONOFF_HAVE_ENERGY_TOTAL) != 0u) {
        out->valid |= ROBOHUB_POWER_ENERGY_TOTAL;
        out->energy_total_kwh = plug->energy_total_kwh;
    }
    return ROBOHUB_OK;
}
