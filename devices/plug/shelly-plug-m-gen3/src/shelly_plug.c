/*
 * shelly_plug.c — a Shelly Plug M Gen3 over HTTP RPC or MQTT.
 *
 * Both transports end in the same parse of the status document and the same
 * cached switch state. HTTP is a GET to Shelly's RPC; MQTT is a publish to the
 * device's rpc topic, with the broker delivering the status topic back.
 */
#include "shelly_plug.h"
#include "robohub/protocols/http_client.h"
#include "robohub/protocols/json.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void parse_status(RoboHubShelly* plug, const char* json)
{
    rh_f64 value = 0;
    rh_bool on = RH_FALSE;
    if (robohub_json_bool(json, "switch:0/output", &on)) {
        plug->on = on;
    }
    if (robohub_json_number(json, "switch:0/apower", &value)) {
        plug->power_w = (rh_f32)value;
    }
    if (robohub_json_number(json, "switch:0/voltage", &value)) {
        plug->voltage_v = (rh_f32)value;
    }
    if (robohub_json_number(json, "switch:0/current", &value)) {
        plug->current_a = (rh_f32)value;
    }
    if (robohub_json_number(json, "switch:0/aenergy/total", &value)) {
        plug->energy_total_kwh = (rh_f32)(value / 1000.0);
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
        snprintf(topic, sizeof(topic), "shellyplugmgen3-%s/rpc", plug->config.device_id);
        snprintf(payload, sizeof(payload),
                 "{\"id\":1,\"src\":\"robohub\",\"method\":\"Switch.Set\",\"params\":{\"id\":0,\"on\":%s}}",
                 on ? "true" : "false");
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
    if (config->transport == ROBOHUB_SHELLY_MQTT && config->device_id == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
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
    /* MQTT is push: the broker delivers status, so poll only subscribes once. */
    if (plug->config.mqtt == NULL || plug->config.mqtt->subscribe == NULL) {
        return ROBOHUB_ERR_STATE;
    }
    char topic[160];
    snprintf(topic, sizeof(topic), "shellyplugmgen3-%s/status/switch:0",
             plug->config.device_id);
    return plug->config.mqtt->subscribe(plug->config.mqtt, topic);
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
