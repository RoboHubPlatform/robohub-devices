/**
 * @file shelly_plug.h
 * @brief A Shelly Plug M Gen3 as a switch and a power meter.
 *
 * The plug speaks Shelly's JSON-RPC. Over HTTP it answers
 * `/rpc/Shelly.GetStatus` and `/rpc/Switch.Set`; over MQTT it publishes a
 * `Switch.Set` request and the platform delivers the status payload. Both
 * transports produce the same cached state and the same power reading, so a
 * caller does not know which one is in use.
 *
 * @project RoboHub Platform
 * @license MIT
 */
#ifndef ROBOHUB_SHELLY_PLUG_H
#define ROBOHUB_SHELLY_PLUG_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma once

#include "types.h"
#include "error.h"
#include "robohub/devices/device.h"
#include "robohub/devices/plug.h"
#include "robohub/protocols/mqtt.h"

typedef enum {
    ROBOHUB_SHELLY_HTTP = 0,
    ROBOHUB_SHELLY_MQTT = 1
} RoboHubShellyTransport;

typedef struct {
    const char* host;       /* HTTP host, or unused for MQTT */
    rh_u16      port;       /* HTTP port, or unused for MQTT */
    /** The plug's Shelly device id (`shellyplugmg3-<mac>`), which is also the
     *  topic prefix a plug publishes under until it is given another. */
    const char* device_id;
    /** The MQTT topic prefix the plug is configured with (Shelly's
     *  `MQTT.SetConfig` `topic_prefix`); NULL means the device id. */
    const char* topic_prefix;
    /** The `src` of the JSON-RPC requests sent to the plug, the name its
     *  answers come back to (`<source>/rpc`); NULL means "robohub". It may not
     *  hold `"`, `\` or a control character. */
    const char* source;
    RoboHubShellyTransport transport;
    RoboHubMqtt* mqtt;      /* required for the MQTT transport */
    RoboHubDeviceId id;
} RoboHubShellyConfig;

typedef struct {
    RoboHubPlug interface; /* must remain first */
    RoboHubShellyConfig config;
    rh_bool on;
    rh_f32 power_w;
    rh_f32 voltage_v;
    rh_f32 current_a;
    rh_f32 energy_total_kwh;
    rh_bool ready;
    char buffer[2048];
} RoboHubShelly;

RoboHubError robohub_shelly_init(RoboHubShelly* plug, const RoboHubShellyConfig* config);

/** Read the device over its transport and update the cached state and power. */
RoboHubError robohub_shelly_poll(RoboHubShelly* plug);

/** The last power reading, as a `RoboHubPowerReading`. */
RoboHubError robohub_shelly_power(RoboHubShelly* plug, RoboHubPowerReading* out);

/**
 * Feed a status payload the platform received for this plug.
 *
 * Every shape the plug sends is read: the switch status object published on
 * `<prefix>/status/switch:0`, a `NotifyStatus` notification published on
 * `<prefix>/events/rpc` (only the fields it carries change), and the
 * `Shelly.GetStatus` document. A notification for another component, or an
 * event, changes nothing.
 */
RoboHubError robohub_shelly_ingest_status(RoboHubShelly* plug, const char* payload);

#ifdef __cplusplus
}
#endif

#endif /* ROBOHUB_SHELLY_PLUG_H */
