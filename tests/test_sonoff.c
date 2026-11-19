/*
 * test_sonoff.c — the Sonoff S60TPF running Tasmota, over MQTT, with a fake
 * broker client and a fake clock.
 *
 * The payloads are the shapes Tasmota documents (Commands, MQTT, "Power" and
 * "TelePeriod"; the S60's figures from the Tasmota discussion of the EU plug),
 * with FullTopic %topic%/%prefix%/ and the Topic s60-desk:
 *
 *   s60-desk/stat/RESULT   {"POWER":"ON"}, the answer to a Power command
 *   s60-desk/stat/POWER    ON, the bare word
 *   s60-desk/tele/STATE    the TelePeriod state, with "POWER":"ON"
 *   s60-desk/tele/SENSOR   the TelePeriod sensors, ENERGY in kWh, W, V and A
 *   s60-desk/tele/LWT      Online, or the broker's last will, Offline
 *
 * The driver is fed them, and so is the manifest's field map, applied as the
 * contract between this collection and Mesh says (`$` is the whole payload,
 * `onoff` reads ON/OFF in any case); both must leave the same values behind.
 *
 * Run with no argument, every test runs (the host plan); run with a test's name,
 * only that one (the per-test CTest cases tests/CMakeLists.txt registers).
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "robohub/devices/plug/sonoff_s60tpf.h"
#include "robohub/protocols/json.h"

#ifndef SONOFF_MANIFEST
#error "build with -DSONOFF_MANIFEST=\"<path to devices/plug/sonoff-s60tpf/module.yml>\""
#endif
#ifndef SONOFF_DOCS
#error "build with -DSONOFF_DOCS=\"<path to devices/plug/sonoff-s60tpf/docs/doc/index.md>\""
#endif

static int failures;

#define CHECK(cond, what) do { \
    if (cond) { printf("[PASS] %s\n", what); } \
    else { printf("[FAIL] %s (%s:%d)\n", what, __FILE__, __LINE__); failures++; } \
} while (0)

static int near(double a, double b) { return fabs(a - b) < 1e-3; }

/* ------------------------------------------------------------------------ */
/*  Tasmota's documents                                                     */
/* ------------------------------------------------------------------------ */

#define PREFIX "s60-desk"

static const char* RESULT_ON  = "{\"POWER\":\"ON\"}";
static const char* RESULT_OFF = "{\"POWER\":\"OFF\"}";
/* The answer to another command (TelePeriod 10): no POWER in it. */
static const char* RESULT_OTHER = "{\"TelePeriod\":10}";

static const char* STATE_ON =
    "{\"Time\":\"2026-11-19T12:00:00\",\"Uptime\":\"0T01:00:00\",\"UptimeSec\":3600,"
    "\"Heap\":145,\"SleepMode\":\"Dynamic\",\"Sleep\":50,\"LoadAvg\":19,\"MqttCount\":1,"
    "\"POWER\":\"ON\",\"Wifi\":{\"AP\":1,\"SSId\":\"home\",\"BSSId\":\"AA:BB:CC:DD:EE:FF\","
    "\"Channel\":6,\"Mode\":\"11n\",\"RSSI\":82,\"Signal\":-59,\"LinkCount\":1,"
    "\"Downtime\":\"0T00:00:03\"}}";

static const char* STATE_OFF =
    "{\"Time\":\"2026-11-19T12:00:10\",\"Uptime\":\"0T01:00:10\",\"UptimeSec\":3610,"
    "\"Heap\":145,\"SleepMode\":\"Dynamic\",\"Sleep\":50,\"LoadAvg\":19,\"MqttCount\":1,"
    "\"POWER\":\"OFF\",\"Wifi\":{\"AP\":1,\"SSId\":\"home\",\"BSSId\":\"AA:BB:CC:DD:EE:FF\","
    "\"Channel\":6,\"Mode\":\"11n\",\"RSSI\":82,\"Signal\":-59,\"LinkCount\":1,"
    "\"Downtime\":\"0T00:00:03\"}}";

static const char* SENSOR =
    "{\"Time\":\"2026-11-19T12:00:00\",\"ENERGY\":{\"TotalStartTime\":\"2026-11-01T10:00:00\","
    "\"Total\":12.345,\"Yesterday\":0.456,\"Today\":0.123,\"Period\":2,\"Power\":83,"
    "\"ApparentPower\":95,\"ReactivePower\":46,\"Factor\":0.87,\"Voltage\":231,"
    "\"Current\":0.411}}";

static const char* SENSOR_LATER =
    "{\"Time\":\"2026-11-19T12:00:10\",\"ENERGY\":{\"TotalStartTime\":\"2026-11-01T10:00:00\","
    "\"Total\":12.346,\"Yesterday\":0.456,\"Today\":0.124,\"Period\":0,\"Power\":0,"
    "\"ApparentPower\":0,\"ReactivePower\":0,\"Factor\":0.00,\"Voltage\":229,"
    "\"Current\":0.000}}";

/* ------------------------------------------------------------------------ */
/*  Fakes                                                                   */
/* ------------------------------------------------------------------------ */

static rh_u32 fake_now;
static rh_u32 fake_clock(void) { return fake_now; }

typedef struct {
    char topics[8][160];
    int subscribed;
    int published;
    char published_topic[160];
    char published_payload[64];
    rh_bool published_retain;
    RoboHubError publish_result;
    RoboHubError subscribe_result;
} FakeBroker;

static RoboHubError fake_publish(RoboHubMqtt* self, const char* topic,
                                 const char* payload, rh_bool retain)
{
    FakeBroker* b = (FakeBroker*)self->context;
    if (b->publish_result != ROBOHUB_OK) {
        return b->publish_result;
    }
    b->published++;
    snprintf(b->published_topic, sizeof(b->published_topic), "%s", topic);
    snprintf(b->published_payload, sizeof(b->published_payload), "%s", payload);
    b->published_retain = retain;
    return ROBOHUB_OK;
}

static RoboHubError fake_subscribe(RoboHubMqtt* self, const char* topic)
{
    FakeBroker* b = (FakeBroker*)self->context;
    if (b->subscribe_result != ROBOHUB_OK) {
        return b->subscribe_result;
    }
    if (b->subscribed < 8) {
        snprintf(b->topics[b->subscribed], sizeof(b->topics[0]), "%s", topic);
    }
    b->subscribed++;
    return ROBOHUB_OK;
}

typedef struct {
    RoboHubSonoff plug;
    RoboHubMqtt mqtt;
    FakeBroker broker;
} Rig;

static RoboHubSonoffConfig rig_config(Rig* rig, const char* prefix, rh_u32 period)
{
    RoboHubSonoffConfig config;
    memset(&config, 0, sizeof(config));
    config.prefix = prefix;
    config.tele_period_s = period;
    config.mqtt = &rig->mqtt;
    config.now_ms = fake_clock;
    config.id.area_id = 3;
    config.id.node_id = 12;
    return config;
}

static void rig_init(Rig* rig, rh_u32 period)
{
    memset(rig, 0, sizeof(*rig));
    rig->mqtt.context = &rig->broker;
    rig->mqtt.publish = fake_publish;
    rig->mqtt.subscribe = fake_subscribe;
    fake_now = 1000u;
    RoboHubSonoffConfig config = rig_config(rig, PREFIX, period);
    CHECK(robohub_sonoff_init(&rig->plug, &config) == ROBOHUB_OK, "init with a valid prefix");
    CHECK(robohub_sonoff_poll(&rig->plug) == ROBOHUB_OK, "poll subscribes");
}

static RoboHubError feed(Rig* rig, const char* suffix, const char* payload)
{
    char topic[192];
    snprintf(topic, sizeof(topic), PREFIX "/%s", suffix);
    return robohub_sonoff_ingest(&rig->plug, topic, payload);
}

static rh_bool get_on(Rig* rig, RoboHubError* err)
{
    rh_bool on = RH_FALSE;
    *err = rig->plug.interface.get(&rig->plug.interface, &on);
    return on;
}

/* ------------------------------------------------------------------------ */
/*  The driver                                                              */
/* ------------------------------------------------------------------------ */

// @verifies DEVS-PLUG-0006
static void test_sonoff_topics(void)
{
    Rig rig;
    rig_init(&rig, 0);
    CHECK(rig.broker.subscribed == 5 &&
          strcmp(rig.broker.topics[0], PREFIX "/stat/RESULT") == 0 &&
          strcmp(rig.broker.topics[1], PREFIX "/stat/POWER") == 0 &&
          strcmp(rig.broker.topics[2], PREFIX "/tele/STATE") == 0 &&
          strcmp(rig.broker.topics[3], PREFIX "/tele/SENSOR") == 0 &&
          strcmp(rig.broker.topics[4], PREFIX "/tele/LWT") == 0,
          "poll subscribes to RESULT, POWER, STATE, SENSOR and LWT under the prefix");
    RoboHubError err;
    get_on(&rig, &err);
    CHECK(err == ROBOHUB_ERR_STATE, "nothing reported yet: no switch state");
    RoboHubPowerReading reading;
    CHECK(robohub_sonoff_power(&rig.plug, &reading) == ROBOHUB_ERR_STATE,
          "nothing reported yet: no power reading");
}

// @verifies DEVS-PLUG-0005, DEVS-PLUG-0006, DEVS-PLUG-0010
static void test_sonoff_result_and_power(void)
{
    Rig rig;
    rig_init(&rig, 0);
    RoboHubError err;

    CHECK(feed(&rig, "stat/RESULT", RESULT_ON) == ROBOHUB_OK, "stat/RESULT is accepted");
    CHECK(get_on(&rig, &err) && err == ROBOHUB_OK, "RESULT POWER:ON reads as on");
    CHECK(feed(&rig, "stat/POWER", "OFF") == ROBOHUB_OK, "stat/POWER is accepted");
    CHECK(!get_on(&rig, &err) && err == ROBOHUB_OK, "stat/POWER OFF reads as off");
    feed(&rig, "stat/RESULT", RESULT_ON);
    CHECK(get_on(&rig, &err), "RESULT POWER:ON again reads as on");
    CHECK(feed(&rig, "stat/RESULT", RESULT_OTHER) == ROBOHUB_OK && get_on(&rig, &err) &&
          err == ROBOHUB_OK, "a RESULT answering another command changes nothing");
    feed(&rig, "stat/RESULT", RESULT_OFF);
    CHECK(!get_on(&rig, &err), "RESULT POWER:OFF reads as off");
    CHECK((rig.plug.have & ROBOHUB_SONOFF_HAVE_POWER) == 0u,
          "the switch topics leave the meter unreported");
}

// @verifies DEVS-PLUG-0005, DEVS-PLUG-0006, DEVS-PLUG-0010
static void test_sonoff_state_and_sensor(void)
{
    Rig rig;
    rig_init(&rig, 0);
    RoboHubError err;
    RoboHubPowerReading reading;

    CHECK(feed(&rig, "tele/STATE", STATE_ON) == ROBOHUB_OK, "tele/STATE is accepted");
    CHECK(get_on(&rig, &err) && err == ROBOHUB_OK, "STATE POWER:ON reads as on");
    CHECK(robohub_sonoff_power(&rig.plug, &reading) == ROBOHUB_OK && reading.valid == 0u,
          "before SENSOR the reading has no figure marked valid");

    CHECK(feed(&rig, "tele/SENSOR", SENSOR) == ROBOHUB_OK, "tele/SENSOR is accepted");
    CHECK(near(rig.plug.power_w, 83.0) && near(rig.plug.voltage_v, 231.0) &&
          near(rig.plug.current_a, 0.411) && near(rig.plug.energy_today_kwh, 0.123) &&
          near(rig.plug.energy_total_kwh, 12.345),
          "SENSOR ENERGY Power, Voltage, Current, Today and Total, unconverted");
    CHECK(get_on(&rig, &err), "SENSOR leaves the switch");
    CHECK(robohub_sonoff_power(&rig.plug, &reading) == ROBOHUB_OK &&
          reading.valid == (ROBOHUB_POWER_LOAD_POWER | ROBOHUB_POWER_ENERGY_TODAY |
                            ROBOHUB_POWER_ENERGY_TOTAL) &&
          near(reading.load_power_w, 83.0) && near(reading.energy_today_kwh, 0.123) &&
          near(reading.energy_total_kwh, 12.345) &&
          reading.device.area_id == 3 && reading.device.node_id == 12 && reading.status == 0,
          "the power reading carries load power, energy today and total, for the device");

    feed(&rig, "tele/STATE", STATE_OFF);
    CHECK(!get_on(&rig, &err) && near(rig.plug.power_w, 83.0),
          "STATE POWER:OFF reads as off and leaves the meter");
    feed(&rig, "tele/SENSOR", SENSOR_LATER);
    CHECK(near(rig.plug.power_w, 0.0) && near(rig.plug.voltage_v, 229.0) &&
          near(rig.plug.energy_total_kwh, 12.346), "a later SENSOR replaces the figures");
}

// @verifies DEVS-PLUG-0005, DEVS-PLUG-0008
static void test_sonoff_onoff_any_case(void)
{
    Rig rig;
    rig_init(&rig, 0);
    RoboHubError err;
    static const struct { const char* word; rh_bool on; } words[] = {
        { "ON", RH_TRUE }, { "on", RH_TRUE }, { "On", RH_TRUE }, { "oN", RH_TRUE },
        { "OFF", RH_FALSE }, { "off", RH_FALSE }, { "Off", RH_FALSE }, { "oFf", RH_FALSE },
        { "true", RH_TRUE }, { "false", RH_FALSE }, { "1", RH_TRUE }, { "0", RH_FALSE },
        { "ON\n", RH_TRUE },
    };
    int good = 1;
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        feed(&rig, "stat/POWER", words[i].on ? "OFF" : "ON");
        if (feed(&rig, "stat/POWER", words[i].word) != ROBOHUB_OK ||
            get_on(&rig, &err) != words[i].on) {
            printf("  stat/POWER %s misread\n", words[i].word);
            good = 0;
        }
    }
    CHECK(good, "stat/POWER reads ON/OFF in any letter case, true/false and 1/0");
    feed(&rig, "stat/RESULT", "{\"POWER\":\"on\"}");
    CHECK(get_on(&rig, &err), "RESULT POWER:on in lower case reads as on");
    feed(&rig, "tele/STATE", "{\"POWER\":\"Off\"}");
    CHECK(!get_on(&rig, &err), "STATE POWER:Off in mixed case reads as off");
}

// @verifies DEVS-PLUG-0007
static void test_sonoff_requested_state_not_cached(void)
{
    Rig rig;
    rig_init(&rig, 0);
    RoboHubError err;

    feed(&rig, "stat/POWER", "OFF");
    CHECK(rig.plug.interface.set(&rig.plug.interface, RH_TRUE) == ROBOHUB_OK, "set on publishes");
    CHECK(strcmp(rig.broker.published_topic, PREFIX "/cmnd/POWER") == 0 &&
          strcmp(rig.broker.published_payload, "true") == 0 && !rig.broker.published_retain,
          "switching on publishes true, not retained, to <prefix>/cmnd/POWER");
    CHECK(!get_on(&rig, &err) && err == ROBOHUB_OK,
          "the requested state is not the cached state until the plug reports it");
    feed(&rig, "stat/RESULT", RESULT_ON);
    CHECK(get_on(&rig, &err), "the plug's RESULT makes it on");

    rig.plug.interface.set(&rig.plug.interface, RH_FALSE);
    CHECK(strcmp(rig.broker.published_payload, "false") == 0 && !rig.broker.published_retain,
          "switching off publishes false, not retained");
    CHECK(get_on(&rig, &err), "still on until reported");
    feed(&rig, "tele/STATE", STATE_ON);
    CHECK(get_on(&rig, &err), "a STATE still saying ON keeps it on: the plug did not switch");
    feed(&rig, "stat/POWER", "OFF");
    CHECK(!get_on(&rig, &err), "the plug's stat/POWER OFF makes it off");

    /* With nothing reported at all, a request does not invent a state. */
    Rig fresh;
    rig_init(&fresh, 0);
    fresh.plug.interface.set(&fresh.plug.interface, RH_TRUE);
    get_on(&fresh, &err);
    CHECK(err == ROBOHUB_ERR_STATE, "a request before any report leaves no state");
}

// @verifies DEVS-PLUG-0007, DEVS-PLUG-0008
static void test_sonoff_publish_refused(void)
{
    Rig rig;
    rig_init(&rig, 0);
    RoboHubError err;
    feed(&rig, "stat/POWER", "ON");

    rig.broker.publish_result = ROBOHUB_ERR_IO;
    CHECK(rig.plug.interface.set(&rig.plug.interface, RH_FALSE) == ROBOHUB_ERR_IO,
          "a publish the client refuses is returned as its error");
    CHECK(get_on(&rig, &err) && err == ROBOHUB_OK, "a refused publish leaves the state");

    /* A broker that refused the client's login: the subscription fails with the
     * client's error, and the switch is not shown as off. */
    Rig refused;
    memset(&refused, 0, sizeof(refused));
    refused.mqtt.context = &refused.broker;
    refused.mqtt.publish = fake_publish;
    refused.mqtt.subscribe = fake_subscribe;
    refused.broker.subscribe_result = ROBOHUB_ERR_IO;
    RoboHubSonoffConfig config = rig_config(&refused, PREFIX, 0);
    robohub_sonoff_init(&refused.plug, &config);
    CHECK(robohub_sonoff_poll(&refused.plug) == ROBOHUB_ERR_IO,
          "a refused login reaches the caller as the client's error");
    rh_bool on = RH_TRUE;
    CHECK(refused.plug.interface.get(&refused.plug.interface, &on) == ROBOHUB_ERR_STATE,
          "a refused login is not an off switch");
}

// @verifies DEVS-PLUG-0008
static void test_sonoff_offline_lwt(void)
{
    Rig rig;
    rig_init(&rig, 0);
    RoboHubError err;
    RoboHubPowerReading reading;

    CHECK(feed(&rig, "tele/LWT", "Online") == ROBOHUB_OK, "LWT Online is accepted");
    feed(&rig, "tele/STATE", STATE_ON);
    feed(&rig, "tele/SENSOR", SENSOR);
    CHECK(robohub_sonoff_reachable(&rig.plug), "online and heard: reachable");

    CHECK(feed(&rig, "tele/LWT", "Offline") == ROBOHUB_OK, "LWT Offline is accepted");
    CHECK(!robohub_sonoff_reachable(&rig.plug), "after LWT Offline: unreachable");
    get_on(&rig, &err);
    CHECK(err == ROBOHUB_ERR_STATE, "after LWT Offline get is a state error");
    CHECK(robohub_sonoff_power(&rig.plug, &reading) == ROBOHUB_ERR_STATE,
          "after LWT Offline there is no power reading");

    feed(&rig, "tele/LWT", "Online");
    CHECK(robohub_sonoff_reachable(&rig.plug) && get_on(&rig, &err) && err == ROBOHUB_OK,
          "LWT Online again: reachable, with the state it last reported");
    feed(&rig, "tele/LWT", "Offline");
    feed(&rig, "stat/POWER", "OFF");
    CHECK(robohub_sonoff_reachable(&rig.plug) && !get_on(&rig, &err),
          "a report after Offline makes it reachable again");
}

// @verifies DEVS-PLUG-0008
static void test_sonoff_offline_by_silence(void)
{
    Rig rig;
    RoboHubError err;
    RoboHubPowerReading reading;

    rig_init(&rig, 10);                       /* TelePeriod 10 s */
    feed(&rig, "tele/STATE", STATE_ON);
    feed(&rig, "tele/SENSOR", SENSOR);
    fake_now += 30000u;
    CHECK(robohub_sonoff_reachable(&rig.plug) && get_on(&rig, &err) && err == ROBOHUB_OK,
          "three periods of silence, exactly: still reachable");
    fake_now += 1u;
    CHECK(!robohub_sonoff_reachable(&rig.plug), "past three periods: unreachable");
    get_on(&rig, &err);
    CHECK(err == ROBOHUB_ERR_STATE, "silent: get is a state error");
    CHECK(robohub_sonoff_power(&rig.plug, &reading) == ROBOHUB_ERR_STATE,
          "silent: no power reading");
    feed(&rig, "tele/SENSOR", SENSOR_LATER);
    CHECK(robohub_sonoff_reachable(&rig.plug) &&
          robohub_sonoff_power(&rig.plug, &reading) == ROBOHUB_OK &&
          near(reading.load_power_w, 0.0), "heard again: reachable, with the new figures");

    /* No period configured: Tasmota's default, 300 s, so 900 s of silence. */
    rig_init(&rig, 0);
    feed(&rig, "tele/STATE", STATE_ON);
    fake_now += 900000u;
    CHECK(robohub_sonoff_reachable(&rig.plug), "default period: 900 s is still reachable");
    fake_now += 1u;
    CHECK(!robohub_sonoff_reachable(&rig.plug), "default period: past 900 s unreachable");

    /* The clock wraps; silence is a difference, so it still counts. */
    rig_init(&rig, 10);
    fake_now = 0xFFFFF000u;
    feed(&rig, "tele/STATE", STATE_ON);
    fake_now += 20000u;                       /* past the wrap */
    CHECK(robohub_sonoff_reachable(&rig.plug), "silence across the clock's wrap is measured");

    /* Never heard at all: silence counts from the subscription. */
    rig_init(&rig, 10);
    fake_now += 30001u;
    CHECK(!robohub_sonoff_reachable(&rig.plug), "a plug never heard is unreachable after three periods");
}

// @verifies DEVS-PLUG-0008, DEVS-PLUG-0010
static void test_sonoff_malformed(void)
{
    Rig rig;
    rig_init(&rig, 0);
    RoboHubError err;
    feed(&rig, "tele/STATE", STATE_ON);
    feed(&rig, "tele/SENSOR", SENSOR);
    rh_u32 heard = rig.plug.heard_ms;
    fake_now += 5000u;

    CHECK(feed(&rig, "stat/RESULT", "POWER ON") == ROBOHUB_ERR_PROTOCOL,
          "a RESULT that is not JSON is a protocol error");
    CHECK(feed(&rig, "stat/RESULT", "{\"POWER\":\"MAYBE\"}") == ROBOHUB_ERR_PROTOCOL,
          "a RESULT whose POWER is not ON/OFF is a protocol error");
    CHECK(feed(&rig, "stat/RESULT", "{\"POWER\":2}") == ROBOHUB_ERR_PROTOCOL,
          "a RESULT whose POWER is a number other than 1/0 is a protocol error");
    CHECK(feed(&rig, "tele/STATE", "{\"POWER\":\"OFF\"") == ROBOHUB_ERR_PROTOCOL,
          "a truncated STATE is a protocol error");
    CHECK(feed(&rig, "tele/SENSOR", "garbage") == ROBOHUB_ERR_PROTOCOL,
          "a SENSOR that is not JSON is a protocol error");
    CHECK(feed(&rig, "stat/POWER", "{\"POWER\":\"OFF\"}") == ROBOHUB_ERR_PROTOCOL,
          "a stat/POWER that is not the bare word is a protocol error");
    CHECK(feed(&rig, "stat/POWER", "OFFF") == ROBOHUB_ERR_PROTOCOL,
          "a stat/POWER word that is not ON/OFF is a protocol error");
    CHECK(feed(&rig, "stat/POWER", "") == ROBOHUB_ERR_PROTOCOL,
          "an empty stat/POWER is a protocol error");
    CHECK(feed(&rig, "tele/LWT", "online") == ROBOHUB_ERR_PROTOCOL,
          "an LWT other than Online/Offline, letter for letter, is a protocol error");
    CHECK(get_on(&rig, &err) && err == ROBOHUB_OK && near(rig.plug.power_w, 83.0) &&
          near(rig.plug.energy_total_kwh, 12.345),
          "the malformed messages changed nothing");
    CHECK(rig.plug.heard_ms == heard, "a malformed message does not count as hearing the plug");

    CHECK(feed(&rig, "tele/SENSOR", "{\"ENERGY\":{\"Power\":\"n/a\",\"Voltage\":232}}") == ROBOHUB_OK &&
          near(rig.plug.power_w, 83.0) && near(rig.plug.voltage_v, 232.0),
          "a SENSOR figure that is not a number leaves that figure alone");
    CHECK(feed(&rig, "tele/INFO1", "{\"Info1\":{}}") == ROBOHUB_OK,
          "another topic of the plug's is ignored");
}

// @verifies DEVS-PLUG-0006, DEVS-PLUG-0010
static void test_sonoff_bad_prefixes(void)
{
    Rig rig;
    memset(&rig, 0, sizeof(rig));
    rig.mqtt.context = &rig.broker;
    rig.mqtt.publish = fake_publish;
    rig.mqtt.subscribe = fake_subscribe;

    char longest[ROBOHUB_SONOFF_PREFIX_MAX + 2];
    memset(longest, 'p', sizeof(longest));
    longest[ROBOHUB_SONOFF_PREFIX_MAX] = '\0';
    char too_long[ROBOHUB_SONOFF_PREFIX_MAX + 2];
    memset(too_long, 'p', sizeof(too_long));
    too_long[ROBOHUB_SONOFF_PREFIX_MAX + 1] = '\0';

    static const char* const refused[] = { "", "/s60-desk", "s60/#", "s60+desk", "s60 desk", "home/+/plug" };
    int all = 1;
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        RoboHubSonoffConfig config = rig_config(&rig, refused[i], 0);
        if (robohub_sonoff_init(&rig.plug, &config) != ROBOHUB_ERR_INVALID_ARG) {
            printf("  prefix '%s' was accepted\n", refused[i]);
            all = 0;
        }
    }
    CHECK(all, "an empty prefix, one starting with /, or one holding #, + or a space is refused");
    RoboHubSonoffConfig config = rig_config(&rig, NULL, 0);
    CHECK(robohub_sonoff_init(&rig.plug, &config) == ROBOHUB_ERR_INVALID_ARG, "no prefix is refused");
    config = rig_config(&rig, too_long, 0);
    CHECK(robohub_sonoff_init(&rig.plug, &config) == ROBOHUB_ERR_INVALID_ARG,
          "a prefix longer than 128 bytes is refused");
    config = rig_config(&rig, longest, 0);
    CHECK(robohub_sonoff_init(&rig.plug, &config) == ROBOHUB_OK &&
          robohub_sonoff_poll(&rig.plug) == ROBOHUB_OK, "a 128-byte prefix is accepted");
    config = rig_config(&rig, "home/kitchen/s60", 0);
    CHECK(robohub_sonoff_init(&rig.plug, &config) == ROBOHUB_OK, "a prefix with levels is accepted");

    config = rig_config(&rig, PREFIX, 5);
    CHECK(robohub_sonoff_init(&rig.plug, &config) == ROBOHUB_ERR_INVALID_ARG,
          "a TelePeriod below Tasmota's 10 s is refused");
    config = rig_config(&rig, PREFIX, 0);
    config.mqtt = NULL;
    CHECK(robohub_sonoff_init(&rig.plug, &config) == ROBOHUB_ERR_INVALID_ARG,
          "no MQTT client is refused");
    config = rig_config(&rig, PREFIX, 0);
    config.now_ms = NULL;
    CHECK(robohub_sonoff_init(&rig.plug, &config) == ROBOHUB_ERR_INVALID_ARG,
          "no clock is refused");

    Rig ok;
    rig_init(&ok, 0);
    CHECK(robohub_sonoff_ingest(&ok.plug, "other-plug/stat/POWER", "ON") == ROBOHUB_ERR_INVALID_ARG &&
          robohub_sonoff_ingest(&ok.plug, PREFIX "x/stat/POWER", "ON") == ROBOHUB_ERR_INVALID_ARG,
          "a message under another prefix is refused");
}

/* ------------------------------------------------------------------------ */
/*  The manifest's field map, applied as Mesh applies it                    */
/* ------------------------------------------------------------------------ */

#define MAX_LINES 16

typedef struct {
    char text[MAX_LINES][128];
    int count;
} Lines;

typedef struct {
    rh_bool on;
    rh_bool have_on;
    double number[8];
    rh_bool have[8];
    char names[8][32];
    int fields;
} Fields;

static char* read_text(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    static char text[32768];
    size_t n = fread(text, 1, sizeof(text) - 1u, f);
    fclose(f);
    text[n] = '\0';
    return text;
}

static char* read_manifest(void)
{
    return read_text(SONOFF_MANIFEST);
}

/* The quoted items of a top-level list or of a list nested one level, starting
 * at the line `key:`. */
static void list_items(const char* manifest, const char* key, Lines* out)
{
    out->count = 0;
    char header[64];
    snprintf(header, sizeof(header), "\n%s:\n", key);
    const char* p = strstr(manifest, header);
    if (p == NULL) {
        return;
    }
    p += strlen(header);
    while (*p != '\0') {
        const char* end = strchr(p, '\n');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        const char* item = p;
        while (*item == ' ') {
            item++;
        }
        if (strncmp(item, "- \"", 3) != 0) {
            break;
        }
        item += 3;
        const char* close = memchr(item, '"', len - (size_t)(item - p));
        if (close != NULL && out->count < MAX_LINES) {
            snprintf(out->text[out->count], sizeof(out->text[0]), "%.*s",
                     (int)(close - item), item);
            out->count++;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
}

/* The scalar value of `key:` (indented as given), quotes removed. */
static rh_bool scalar(const char* manifest, const char* key, char* out, size_t cap)
{
    char header[64];
    snprintf(header, sizeof(header), "\n%s: ", key);
    const char* p = strstr(manifest, header);
    if (p == NULL) {
        return RH_FALSE;
    }
    p += strlen(header);
    const char* end = strchr(p, '\n');
    size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
    if (len >= 2u && (p[0] == '"' || p[0] == '\'') && p[len - 1u] == p[0]) {
        p++;
        len -= 2u;
    }
    snprintf(out, cap, "%.*s", (int)len, p);
    return RH_TRUE;
}

static int split(char* line, char* parts[6])
{
    int n = 0;
    parts[n++] = line;
    for (char* c = line; *c != '\0' && n < 6; c++) {
        if (*c == '|') {
            *c = '\0';
            parts[n++] = c + 1;
        }
    }
    return n;
}

/* The contract's onoff type, written from the contract, not from the driver. */
static rh_bool contract_onoff(const char* word, rh_bool* on)
{
    char w[16];
    size_t n = 0;
    while (*word == ' ' || *word == '\n' || *word == '\r' || *word == '\t') {
        word++;
    }
    for (; word[n] != '\0' && n + 1u < sizeof(w); n++) {
        w[n] = word[n];
    }
    while (n > 0u && (w[n - 1u] == ' ' || w[n - 1u] == '\n' || w[n - 1u] == '\r')) {
        n--;
    }
    w[n] = '\0';
    if (strcmp(w, "true") == 0 || strcmp(w, "1") == 0 ||
        ((w[0] == 'o' || w[0] == 'O') && (w[1] == 'n' || w[1] == 'N') && w[2] == '\0')) {
        *on = RH_TRUE;
        return RH_TRUE;
    }
    if (strcmp(w, "false") == 0 || strcmp(w, "0") == 0 ||
        ((w[0] == 'o' || w[0] == 'O') && (w[1] == 'f' || w[1] == 'F') &&
         (w[2] == 'f' || w[2] == 'F') && w[3] == '\0')) {
        *on = RH_FALSE;
        return RH_TRUE;
    }
    return RH_FALSE;
}

static int field_slot(Fields* f, const char* name)
{
    for (int i = 0; i < f->fields; i++) {
        if (strcmp(f->names[i], name) == 0) {
            return i;
        }
    }
    snprintf(f->names[f->fields], sizeof(f->names[0]), "%s", name);
    return f->fields++;
}

/* Apply every field line to one message: a field whose path the message does
 * not carry keeps its value; `$` is the whole payload, for a payload that is
 * not JSON; a JSON path reads only a JSON payload. */
static void apply_fields(const Lines* lines, const char* payload, Fields* f)
{
    rh_bool json = payload[0] == '{';
    for (int i = 0; i < lines->count; i++) {
        char line[128];
        snprintf(line, sizeof(line), "%s", lines->text[i]);
        char* parts[6];
        if (split(line, parts) != 6) {
            continue;
        }
        const char* name = parts[1];
        const char* type = parts[3];
        const char* path = parts[4];
        double scale = atof(parts[5]);
        rh_bool whole = strcmp(path, "$") == 0;
        if (whole == json) {
            continue;                         /* not carried by this message */
        }
        if (strcmp(type, "onoff") == 0) {
            char word[32];
            rh_bool on = RH_FALSE;
            if (whole) {
                snprintf(word, sizeof(word), "%s", payload);
            } else if (!robohub_json_string(payload, path, word, sizeof(word))) {
                continue;
            }
            if (contract_onoff(word, &on)) {
                f->on = on;
                f->have_on = RH_TRUE;
            }
            continue;
        }
        rh_f64 value = 0;
        if (!whole && strcmp(type, "f32") == 0 && robohub_json_number(payload, path, &value)) {
            int slot = field_slot(f, name);
            f->number[slot] = value * scale;
            f->have[slot] = RH_TRUE;
        }
    }
}

static double field_value(Fields* f, const char* name, rh_bool* have)
{
    int slot = field_slot(f, name);
    *have = f->have[slot];
    return f->number[slot];
}

// @verifies DEVS-PLUG-0010, DEVS-PLUG-0009
static void test_sonoff_manifest_matches_driver(void)
{
    const char* manifest = read_manifest();
    CHECK(manifest != NULL, "the module's manifest is read");
    if (manifest == NULL) {
        return;
    }
    Lines lines;
    list_items(manifest, "telemetry_mqtt_fields", &lines);
    CHECK(lines.count == 7, "the manifest declares seven MQTT field lines");

    /* The same messages, in order, to both. */
    static const struct { const char* suffix; const char** payload; } sequence[] = {
        { "stat/RESULT", &RESULT_ON }, { "tele/SENSOR", &SENSOR }, { "stat/POWER", NULL },
        { "tele/STATE", &STATE_ON }, { "stat/RESULT", &RESULT_OTHER },
        { "tele/SENSOR", &SENSOR_LATER }, { "tele/STATE", &STATE_OFF },
        { "stat/RESULT", &RESULT_ON },
    };
    Rig rig;
    rig_init(&rig, 0);
    Fields fields;
    memset(&fields, 0, sizeof(fields));
    int agree = 1;
    for (size_t i = 0; i < sizeof(sequence) / sizeof(sequence[0]); i++) {
        const char* payload = sequence[i].payload != NULL ? *sequence[i].payload : "OFF";
        feed(&rig, sequence[i].suffix, payload);
        apply_fields(&lines, payload, &fields);

        rh_bool have = RH_FALSE;
        RoboHubError err;
        rh_bool on = get_on(&rig, &err);
        if (!fields.have_on || err != ROBOHUB_OK || fields.on != on) {
            printf("  after %s: switch differs (manifest %d, driver %d)\n",
                   sequence[i].suffix, (int)fields.on, (int)on);
            agree = 0;
        }
        static const struct { const char* name; rh_u16 bit; size_t offset; } meter[] = {
            { "power_w", ROBOHUB_SONOFF_HAVE_POWER, offsetof(RoboHubSonoff, power_w) },
            { "voltage_v", ROBOHUB_SONOFF_HAVE_VOLTAGE, offsetof(RoboHubSonoff, voltage_v) },
            { "current_a", ROBOHUB_SONOFF_HAVE_CURRENT, offsetof(RoboHubSonoff, current_a) },
            { "energy_today_kwh", ROBOHUB_SONOFF_HAVE_ENERGY_TODAY, offsetof(RoboHubSonoff, energy_today_kwh) },
            { "energy_total_kwh", ROBOHUB_SONOFF_HAVE_ENERGY_TOTAL, offsetof(RoboHubSonoff, energy_total_kwh) },
        };
        for (size_t m = 0; m < sizeof(meter) / sizeof(meter[0]); m++) {
            double value = field_value(&fields, meter[m].name, &have);
            rh_bool driver_has = (rig.plug.have & meter[m].bit) != 0u;
            double driver = *(const rh_f32*)((const char*)&rig.plug + meter[m].offset);
            if (have != driver_has || (have && !near(value, driver))) {
                printf("  after %s: %s differs (manifest %g, driver %g)\n",
                       sequence[i].suffix, meter[m].name, value, driver);
                agree = 0;
            }
        }
    }
    CHECK(agree, "the manifest's field lines and the driver read the same values from the same messages");
    CHECK(!fields.have[field_slot(&fields, "state")],
          "no field line reads the switch as a number");
}

// @verifies DEVS-PLUG-0009, DEVS-0010
static void test_sonoff_manifest_declares(void)
{
    const char* manifest = read_manifest();
    if (manifest == NULL) {
        CHECK(0, "the module's manifest is read");
        return;
    }
    char value[128];
    Lines topics;
    list_items(manifest, "  status_topics", &topics);
    CHECK(topics.count == 4 && strcmp(topics.text[0], "{prefix}/stat/RESULT") == 0 &&
          strcmp(topics.text[1], "{prefix}/stat/POWER") == 0 &&
          strcmp(topics.text[2], "{prefix}/tele/STATE") == 0 &&
          strcmp(topics.text[3], "{prefix}/tele/SENSOR") == 0,
          "status_topics are RESULT, POWER, STATE and SENSOR under {prefix}");
    CHECK(strstr(manifest, "\n  status_topic:") == NULL, "status_topic and status_topics are not both given");
    CHECK(scalar(manifest, "  online_topic", value, sizeof(value)) &&
          strcmp(value, "{prefix}/tele/LWT") == 0, "the online topic is {prefix}/tele/LWT");
    CHECK(scalar(manifest, "  online_up", value, sizeof(value)) && strcmp(value, "Online") == 0,
          "online_up is Online");
    CHECK(scalar(manifest, "    mqtt_topic", value, sizeof(value)) &&
          strcmp(value, "{prefix}/cmnd/POWER") == 0 &&
          scalar(manifest, "    mqtt_payload", value, sizeof(value)) && strcmp(value, "{on}") == 0,
          "the switch action publishes {on} to {prefix}/cmnd/POWER");
    CHECK(scalar(manifest, "  firmware", value, sizeof(value)) && strcmp(value, "tasmota") == 0 &&
          scalar(manifest, "  model", value, sizeof(value)) && strcmp(value, "S60TPF") == 0 &&
          scalar(manifest, "  transports", value, sizeof(value)) && strcmp(value, "[mqtt]") == 0,
          "the device block names the S60TPF, over MQTT, running Tasmota");
    CHECK(strstr(manifest, "password") == NULL && strstr(manifest, "Password") == NULL &&
          strstr(manifest, "mqtt_user") == NULL && strstr(manifest, "secret") == NULL,
          "the manifest carries no credential");
}

/* What a person preparing the plug must be told; the page is checked for it,
 * so a step cannot be dropped from it unnoticed. */
// @verifies DEVS-PLUG-0011, DEVS-0010
static void test_sonoff_docs_cover_preparation(void)
{
    const char* page = read_text(SONOFF_DOCS);
    CHECK(page != NULL, "the module's documentation is read");
    if (page == NULL) {
        return;
    }
    static const struct { const char* text; const char* what; } musts[] = {
        { "Stock eWeLink firmware is not supported", "stock eWeLink firmware is unsupported" },
        { "future work", "eWeLink LAN mode is future work" },
        { "tasmota32c3.factory.bin", "the tasmota32c3 image" },
        { "keep it unplugged for the\nwhole job", "unplugged from mains for the whole job" },
        { "read_flash 0x0 0x400000", "a backup of the 4 MB flash" },
        { "write_flash 0x0", "writing Tasmota with esptool" },
        { "\"GPIO\":[1,1,1,1,224,544,1,3104,1,32,1,0,0,0,0,0,0,0,1,1,1,1]", "the template" },
        { "Topic s60-desk", "the Topic command" },
        { "FullTopic %topic%/%prefix%/", "the FullTopic command" },
        { "MqttHost", "the broker host" },
        { "MqttUser <the broker user the keeper issued", "the device's own broker user from the keeper" },
        { "MqttPassword", "the broker password" },
        { "WebPassword", "a web admin password" },
        { "between 10 and 60 seconds", "a TelePeriod of 10 to 60 s" },
        { "PowerSet", "the meter's calibration" },
        { "manual step", "calibration as a manual step" },
        { "never written\n  into the plan", "no credential in the plan" },
        { "S60TPG", "the UK S60TPG" },
        { "untested", "the S60TPG as untested" },
    };
    int all = 1;
    for (size_t i = 0; i < sizeof(musts) / sizeof(musts[0]); i++) {
        if (strstr(page, musts[i].text) == NULL) {
            printf("  the page does not say: %s\n", musts[i].what);
            all = 0;
        }
    }
    CHECK(all, "the page covers the reflash, the commands, calibration and the unsupported firmware");
}

/* ------------------------------------------------------------------------ */

static const struct {
    const char* name;
    void (*run)(void);
} tests[] = {
    { "test_sonoff_topics", test_sonoff_topics },
    { "test_sonoff_result_and_power", test_sonoff_result_and_power },
    { "test_sonoff_state_and_sensor", test_sonoff_state_and_sensor },
    { "test_sonoff_onoff_any_case", test_sonoff_onoff_any_case },
    { "test_sonoff_requested_state_not_cached", test_sonoff_requested_state_not_cached },
    { "test_sonoff_publish_refused", test_sonoff_publish_refused },
    { "test_sonoff_offline_lwt", test_sonoff_offline_lwt },
    { "test_sonoff_offline_by_silence", test_sonoff_offline_by_silence },
    { "test_sonoff_malformed", test_sonoff_malformed },
    { "test_sonoff_bad_prefixes", test_sonoff_bad_prefixes },
    { "test_sonoff_manifest_matches_driver", test_sonoff_manifest_matches_driver },
    { "test_sonoff_manifest_declares", test_sonoff_manifest_declares },
    { "test_sonoff_docs_cover_preparation", test_sonoff_docs_cover_preparation },
};

int main(int argc, char** argv)
{
    int ran = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (argc > 1 && strcmp(argv[1], tests[i].name) != 0) {
            continue;
        }
        tests[i].run();
        ran++;
    }
    if (ran == 0) {
        printf("no test named %s\n", argc > 1 ? argv[1] : "(none)");
        return 2;
    }
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all sonoff checks passed\n");
    return 0;
}
