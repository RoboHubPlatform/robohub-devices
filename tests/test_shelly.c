/*
 * test_shelly.c — the Shelly Plug M Gen3 over MQTT, with a fake broker client.
 *
 * The payloads are the shapes a Gen2+/Gen3 plug publishes (Shelly's Gen2+ API,
 * "MQTT" and "Notifications"):
 *
 *   <prefix>/status/switch:0   the switch component's status, the object alone
 *                              (published when "generic status update over
 *                              MQTT" is on)
 *   <prefix>/events/rpc        a JSON-RPC notification, NotifyStatus carrying
 *                              only what changed under params/switch:0 (when
 *                              "RPC status notifications over MQTT" is on)
 *
 * and, for the HTTP transport, the Shelly.GetStatus document, where the switch
 * sits under `switch:0` beside the other components. The checks are that each
 * of them updates the cached state, that a partial notification leaves the rest
 * alone, that a notification for another component changes nothing, and that
 * the topics and the RPC source come from the configuration.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "robohub/devices/plug/shelly_plug.h"

static int failures;

#define CHECK(cond, what) do { \
    if (cond) { printf("[PASS] %s\n", what); } \
    else { printf("[FAIL] %s (%s:%d)\n", what, __FILE__, __LINE__); failures++; } \
} while (0)

static int near(double a, double b) { return fabs(a - b) < 1e-3; }

typedef struct {
    char topics[4][128];
    int subscribed;
    char published_topic[128];
    char published_payload[256];
} FakeBroker;

static RoboHubError fake_publish(RoboHubMqtt* self, const char* topic,
                                 const char* payload, rh_bool retain)
{
    (void)retain;
    FakeBroker* b = (FakeBroker*)self->context;
    snprintf(b->published_topic, sizeof(b->published_topic), "%s", topic);
    snprintf(b->published_payload, sizeof(b->published_payload), "%s", payload);
    return ROBOHUB_OK;
}

static RoboHubError fake_subscribe(RoboHubMqtt* self, const char* topic)
{
    FakeBroker* b = (FakeBroker*)self->context;
    if (b->subscribed < 4) {
        snprintf(b->topics[b->subscribed], sizeof(b->topics[0]), "%s", topic);
    }
    b->subscribed++;
    return ROBOHUB_OK;
}

/* What a Plug M Gen3 publishes on <prefix>/status/switch:0. */
static const char* STATUS_SWITCH0 =
    "{\"id\":0, \"source\":\"WS_in\", \"output\":true, \"apower\":37.1,"
    " \"voltage\":231.4, \"freq\":50.0, \"current\":0.182, \"pf\":0.88,"
    " \"aenergy\":{\"total\":1534.212,\"by_minute\":[612.004,617.310,610.882],"
    "\"minute_ts\":1763560560},"
    " \"ret_aenergy\":{\"total\":0.000,\"by_minute\":[0.000,0.000,0.000],"
    "\"minute_ts\":1763560560},"
    " \"temperature\":{\"tC\":31.6, \"tF\":88.9}}";

/* What it publishes on <prefix>/events/rpc when the switch is turned off:
 * a NotifyStatus with only the fields that changed. */
static const char* NOTIFY_OFF =
    "{\"src\":\"shellyplugmg3-84fce63e6c5c\",\"dst\":\"shellyplugmg3-84fce63e6c5c/events\","
    "\"method\":\"NotifyStatus\",\"params\":{\"ts\":1763560600.42,"
    "\"switch:0\":{\"id\":0,\"output\":false,\"source\":\"button\"}}}";

/* ... and when the meter moves. */
static const char* NOTIFY_POWER =
    "{\"src\":\"shellyplugmg3-84fce63e6c5c\",\"dst\":\"shellyplugmg3-84fce63e6c5c/events\","
    "\"method\":\"NotifyStatus\",\"params\":{\"ts\":1763560610.03,"
    "\"switch:0\":{\"id\":0,\"apower\":0.0,\"current\":0.000,"
    "\"aenergy\":{\"by_minute\":[0.000,612.004,617.310],\"minute_ts\":1763560620,"
    "\"total\":1540.000}}}}";

/* A notification for another component, whose keys a careless lookup would
 * take for the switch's: it must change nothing. */
static const char* NOTIFY_OTHER =
    "{\"src\":\"shellyplugmg3-84fce63e6c5c\",\"dst\":\"shellyplugmg3-84fce63e6c5c/events\","
    "\"method\":\"NotifyStatus\",\"params\":{\"ts\":1763560620.00,"
    "\"sys\":{\"uptime\":3600,\"output\":true,\"apower\":99.0}}}";

/* An event, not a status: nothing to read. */
static const char* NOTIFY_EVENT =
    "{\"src\":\"shellyplugmg3-84fce63e6c5c\",\"dst\":\"shellyplugmg3-84fce63e6c5c/events\","
    "\"method\":\"NotifyEvent\",\"params\":{\"ts\":1763560630.00,"
    "\"events\":[{\"component\":\"switch:0\",\"id\":0,\"event\":\"toggle\",\"ts\":1763560630.00}]}}";

/* The HTTP transport's Shelly.GetStatus, trimmed to the components a Plug M
 * Gen3 reports; the switch is under switch:0. */
static const char* GET_STATUS =
    "{\"ble\":{},\"cloud\":{\"connected\":false},\"mqtt\":{\"connected\":true},"
    "\"switch:0\":{\"id\":0,\"source\":\"HTTP_in\",\"output\":true,\"apower\":12.5,"
    "\"voltage\":229.8,\"freq\":50.0,\"current\":0.071,\"pf\":0.77,"
    "\"aenergy\":{\"total\":2000.0,\"by_minute\":[0,0,0],\"minute_ts\":1763560000},"
    "\"temperature\":{\"tC\":30.0,\"tF\":86.0}},"
    "\"sys\":{\"mac\":\"84FCE63E6C5C\",\"uptime\":100}}";

static void init_mqtt(RoboHubShelly* plug, RoboHubMqtt* mqtt, FakeBroker* broker,
                      const char* device_id, const char* prefix, const char* source)
{
    memset(broker, 0, sizeof(*broker));
    memset(mqtt, 0, sizeof(*mqtt));
    mqtt->context = broker;
    mqtt->publish = fake_publish;
    mqtt->subscribe = fake_subscribe;

    RoboHubShellyConfig config;
    memset(&config, 0, sizeof(config));
    config.transport = ROBOHUB_SHELLY_MQTT;
    config.device_id = device_id;
    config.topic_prefix = prefix;
    config.source = source;
    config.mqtt = mqtt;
    config.id.node_id = 7;
    CHECK(robohub_shelly_init(plug, &config) == ROBOHUB_OK, "init over MQTT");
}

// @verifies DEVS-PLUG-0004
static void test_status_topic(void)
{
    RoboHubShelly plug;
    RoboHubMqtt mqtt;
    FakeBroker broker;
    init_mqtt(&plug, &mqtt, &broker, "shellyplugmg3-84fce63e6c5c", NULL, NULL);

    CHECK(robohub_shelly_ingest_status(&plug, STATUS_SWITCH0) == ROBOHUB_OK,
          "status/switch:0 is accepted");
    rh_bool on = RH_FALSE;
    CHECK(plug.interface.get(&plug.interface, &on) == ROBOHUB_OK && on,
          "status/switch:0 output:true reads as on");
    CHECK(near(plug.power_w, 37.1), "status/switch:0 apower");
    CHECK(near(plug.voltage_v, 231.4), "status/switch:0 voltage");
    CHECK(near(plug.current_a, 0.182), "status/switch:0 current");
    CHECK(near(plug.energy_total_kwh, 1.534212), "status/switch:0 aenergy.total in kWh");

    RoboHubPowerReading reading;
    CHECK(robohub_shelly_power(&plug, &reading) == ROBOHUB_OK &&
          near(reading.load_power_w, 37.1) && reading.device.node_id == 7,
          "the power reading carries the parsed figures");
}

// @verifies DEVS-PLUG-0004
static void test_notify_status(void)
{
    RoboHubShelly plug;
    RoboHubMqtt mqtt;
    FakeBroker broker;
    init_mqtt(&plug, &mqtt, &broker, "shellyplugmg3-84fce63e6c5c", NULL, NULL);
    robohub_shelly_ingest_status(&plug, STATUS_SWITCH0);

    CHECK(robohub_shelly_ingest_status(&plug, NOTIFY_OFF) == ROBOHUB_OK,
          "NotifyStatus is accepted");
    CHECK(!plug.on, "NotifyStatus output:false reads as off");
    CHECK(near(plug.power_w, 37.1) && near(plug.voltage_v, 231.4),
          "a partial NotifyStatus leaves the fields it does not carry");

    robohub_shelly_ingest_status(&plug, NOTIFY_POWER);
    CHECK(near(plug.power_w, 0.0) && near(plug.current_a, 0.0),
          "NotifyStatus apower and current");
    CHECK(near(plug.energy_total_kwh, 1.54), "NotifyStatus aenergy.total after by_minute");
    CHECK(!plug.on && near(plug.voltage_v, 231.4),
          "the meter notification leaves the switch and the voltage");

    robohub_shelly_ingest_status(&plug, NOTIFY_OTHER);
    CHECK(!plug.on && near(plug.power_w, 0.0),
          "a NotifyStatus for another component changes nothing");
    robohub_shelly_ingest_status(&plug, NOTIFY_EVENT);
    CHECK(!plug.on && near(plug.power_w, 0.0), "a NotifyEvent changes nothing");
}

// @verifies DEVS-PLUG-0001
static void test_get_status_document(void)
{
    RoboHubShelly plug;
    RoboHubMqtt mqtt;
    FakeBroker broker;
    init_mqtt(&plug, &mqtt, &broker, "shellyplugmg3-84fce63e6c5c", NULL, NULL);
    robohub_shelly_ingest_status(&plug, GET_STATUS);
    CHECK(plug.on && near(plug.power_w, 12.5) && near(plug.voltage_v, 229.8) &&
          near(plug.current_a, 0.071) && near(plug.energy_total_kwh, 2.0),
          "the Shelly.GetStatus document is read from switch:0");
}

// @verifies DEVS-PLUG-0002, DEVS-PLUG-0004
static void test_topics_and_source(void)
{
    RoboHubShelly plug;
    RoboHubMqtt mqtt;
    FakeBroker broker;

    /* Defaults: the device id is the topic prefix, the source is robohub. */
    init_mqtt(&plug, &mqtt, &broker, "shellyplugmg3-84fce63e6c5c", NULL, NULL);
    CHECK(robohub_shelly_poll(&plug) == ROBOHUB_OK, "poll subscribes");
    CHECK(broker.subscribed == 2 &&
          strcmp(broker.topics[0], "shellyplugmg3-84fce63e6c5c/status/switch:0") == 0 &&
          strcmp(broker.topics[1], "shellyplugmg3-84fce63e6c5c/events/rpc") == 0,
          "the default prefix is the device id, for both status topics");
    CHECK(plug.interface.set(&plug.interface, RH_TRUE) == ROBOHUB_OK, "set over MQTT");
    CHECK(strcmp(broker.published_topic, "shellyplugmg3-84fce63e6c5c/rpc") == 0,
          "Switch.Set goes to <device id>/rpc");
    CHECK(strstr(broker.published_payload, "\"src\":\"robohub\"") != NULL &&
          strstr(broker.published_payload, "\"method\":\"Switch.Set\"") != NULL &&
          strstr(broker.published_payload, "\"on\":true") != NULL,
          "the default source is robohub");

    /* A prefix the plug was configured with, and a source of our own. */
    init_mqtt(&plug, &mqtt, &broker, "shellyplugmg3-84fce63e6c5c", "home/kitchen/plug",
              "robohub-mesh-1");
    robohub_shelly_poll(&plug);
    CHECK(strcmp(broker.topics[0], "home/kitchen/plug/status/switch:0") == 0 &&
          strcmp(broker.topics[1], "home/kitchen/plug/events/rpc") == 0,
          "a configured prefix names the status topics");
    plug.interface.set(&plug.interface, RH_FALSE);
    CHECK(strcmp(broker.published_topic, "home/kitchen/plug/rpc") == 0,
          "a configured prefix names the rpc topic");
    CHECK(strstr(broker.published_payload, "\"src\":\"robohub-mesh-1\"") != NULL &&
          strstr(broker.published_payload, "\"on\":false") != NULL,
          "a configured source is the request's src");

    /* A prefix alone is enough to reach the plug. */
    init_mqtt(&plug, &mqtt, &broker, NULL, "home/kitchen/plug", NULL);
    plug.interface.set(&plug.interface, RH_TRUE);
    CHECK(strcmp(broker.published_topic, "home/kitchen/plug/rpc") == 0,
          "a prefix without a device id is accepted");

    /* A source that would break the JSON it is written into is refused. */
    RoboHubShellyConfig config;
    memset(&config, 0, sizeof(config));
    config.transport = ROBOHUB_SHELLY_MQTT;
    config.device_id = "shellyplugmg3-84fce63e6c5c";
    config.source = "a\"b";
    config.mqtt = &mqtt;
    CHECK(robohub_shelly_init(&plug, &config) == ROBOHUB_ERR_INVALID_ARG,
          "a source with a quote is refused");
    config.source = NULL;
    config.device_id = NULL;
    CHECK(robohub_shelly_init(&plug, &config) == ROBOHUB_ERR_INVALID_ARG,
          "MQTT without a device id or a prefix is refused");
}

int main(void)
{
    test_status_topic();
    test_notify_status();
    test_get_status_document();
    test_topics_and_source();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all shelly checks passed\n");
    return 0;
}
