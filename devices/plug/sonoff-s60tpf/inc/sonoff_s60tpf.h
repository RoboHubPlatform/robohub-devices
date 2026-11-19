/**
 * @file sonoff_s60tpf.h
 * @brief A Sonoff S60TPF reflashed with Tasmota, as a switch and a power meter.
 *
 * The plug runs Tasmota (`tasmota32c3`) with `FullTopic %topic%/%prefix%/`, so
 * everything it says is under one prefix, its Tasmota `Topic`:
 *
 *   <prefix>/stat/RESULT   `{"POWER":"ON"}`, the answer to a Power command
 *   <prefix>/stat/POWER    `ON` or `OFF`, the bare word, after any change
 *   <prefix>/tele/STATE    every TelePeriod, with `"POWER":"ON"`
 *   <prefix>/tele/SENSOR   every TelePeriod, `ENERGY.{Power,Voltage,Current,
 *                          Today,Total}` in W, V, A, kWh and kWh
 *   <prefix>/tele/LWT      `Online`, or `Offline` as the broker's last will
 *
 * and it is switched by publishing `true` or `false` to `<prefix>/cmnd/POWER`.
 * The platform's MQTT client carries both directions (the `robohub-mqtt` seam)
 * and hands each delivered message to `robohub_sonoff_ingest`; the broker login
 * is that client's, so this integration holds no credential of its own.
 *
 * The cached state changes only from what the plug reports: a switch request is
 * a publish, and the state follows when the plug answers on `stat/RESULT`,
 * `stat/POWER` or `tele/STATE`. After an `Offline` will, or three telemetry
 * periods without a word from the plug, it is unreachable: `get` and
 * `read_power` answer `ROBOHUB_ERR_STATE` until it is heard again.
 *
 * @project RoboHub Platform
 * @license MIT
 */
#ifndef ROBOHUB_SONOFF_S60TPF_H
#define ROBOHUB_SONOFF_S60TPF_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma once

#include "types.h"
#include "error.h"
#include "robohub/devices/device.h"
#include "robohub/devices/plug.h"
#include "robohub/protocols/mqtt.h"

/** Tasmota's own TelePeriod default, used when the configuration gives none. */
#define ROBOHUB_SONOFF_DEFAULT_TELE_PERIOD_S 300u

/** The longest prefix accepted; the topics are built in 160-byte buffers. */
#define ROBOHUB_SONOFF_PREFIX_MAX 128u

typedef struct {
    /** The plug's Tasmota `Topic`, the prefix everything it says is under (the
     *  plan's address for the device). Refused when empty, longer than 128
     *  bytes, starting with `/`, or holding `#`, `+` or a space. */
    const char* prefix;
    /** The plug's Tasmota `TelePeriod` in seconds (10..3600); 0 means
     *  Tasmota's default, 300. Three periods of silence make it unreachable. */
    rh_u32 tele_period_s;
    /** The platform's MQTT client; required. */
    RoboHubMqtt* mqtt;
    /** The clock silence is measured on, in milliseconds (a board passes
     *  `robohub_osal_now_ms`); required. It may wrap. */
    rh_u32 (*now_ms)(void);
    RoboHubDeviceId id;
} RoboHubSonoffConfig;

/** Which of the figures the plug has reported. */
typedef enum {
    ROBOHUB_SONOFF_HAVE_SWITCH       = 1u << 0,
    ROBOHUB_SONOFF_HAVE_POWER        = 1u << 1,
    ROBOHUB_SONOFF_HAVE_VOLTAGE      = 1u << 2,
    ROBOHUB_SONOFF_HAVE_CURRENT      = 1u << 3,
    ROBOHUB_SONOFF_HAVE_ENERGY_TODAY = 1u << 4,
    ROBOHUB_SONOFF_HAVE_ENERGY_TOTAL = 1u << 5
} RoboHubSonoffHave;

typedef struct {
    RoboHubPlug interface; /* must remain first */
    RoboHubSonoffConfig config;
    rh_bool on;
    rh_f32 power_w;
    rh_f32 voltage_v;
    rh_f32 current_a;
    rh_f32 energy_today_kwh;
    rh_f32 energy_total_kwh;
    /** RoboHubSonoffHave bits. */
    rh_u16 have;
    /** The last will said Offline, and nothing has been heard since. */
    rh_bool offline;
    /** The plug has been heard since init. */
    rh_bool heard;
    /** When the plug was last heard, or when following it began. */
    rh_u32 heard_ms;
    rh_bool ready;
} RoboHubSonoff;

/** Check the configuration and present the plug as `plug->interface`. */
RoboHubError robohub_sonoff_init(RoboHubSonoff* plug, const RoboHubSonoffConfig* config);

/**
 * Subscribe to the five topics the plug publishes on. A subscription the client
 * refuses — a broker that refused its login among them — is returned as the
 * client's error. Silence is counted from here until the plug is heard.
 */
RoboHubError robohub_sonoff_poll(RoboHubSonoff* plug);

/**
 * Feed a message the platform received for this plug.
 *
 * Each topic changes only the fields it carries. A topic that is not one of the
 * five, or a RESULT answering another command, changes nothing and is
 * `ROBOHUB_OK`; a topic outside the plug's prefix is `ROBOHUB_ERR_INVALID_ARG`;
 * a payload that is not the JSON object or the ON/OFF word its topic carries
 * changes nothing and is `ROBOHUB_ERR_PROTOCOL`.
 */
RoboHubError robohub_sonoff_ingest(RoboHubSonoff* plug, const char* topic, const char* payload);

/** Whether the plug is reachable now: no Offline will, and heard within three
 *  telemetry periods. */
rh_bool robohub_sonoff_reachable(const RoboHubSonoff* plug);

/** The last power reading; `ROBOHUB_ERR_STATE` while unreachable or before the
 *  plug has reported anything. Only the figures it reported are valid. */
RoboHubError robohub_sonoff_power(RoboHubSonoff* plug, RoboHubPowerReading* out);

/**
 * Read a Tasmota switch word: `ON`/`OFF` in any letter case, `true`/`false` and
 * `1`/`0` (what the manifest's `onoff` type accepts). Returns `RH_FALSE` for
 * anything else.
 */
rh_bool robohub_sonoff_parse_onoff(const char* word, rh_bool* on);

#ifdef __cplusplus
}
#endif

#endif /* ROBOHUB_SONOFF_S60TPF_H */
