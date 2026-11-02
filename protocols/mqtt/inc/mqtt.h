/**
 * @file mqtt.h
 * @brief The MQTT side of a device integration, as an interface.
 *
 * A board's MQTT client belongs to its network stack; a device integration
 * needs to publish a JSON-RPC request and to be handed the status payload a
 * broker delivers. This interface is that seam: the platform supplies publish
 * and subscribe, and the device formats the topic and the payload.
 *
 * @project RoboHub Platform
 * @license MIT
 */
#ifndef ROBOHUB_MQTT_H
#define ROBOHUB_MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma once

#include "types.h"
#include "error.h"

typedef struct RoboHubMqtt RoboHubMqtt;

struct RoboHubMqtt {
    void* context;
    /** Publish a payload to a topic. `retain` asks the broker to keep it. */
    RoboHubError (*publish)(RoboHubMqtt* self, const char* topic,
                            const char* payload, rh_bool retain);
    /** Ask the broker for a topic; the platform routes delivered payloads back. */
    RoboHubError (*subscribe)(RoboHubMqtt* self, const char* topic);
};

/** Publish through the client the platform supplied. */
RoboHubError robohub_mqtt_publish(RoboHubMqtt* mqtt, const char* topic,
                                  const char* payload, rh_bool retain);

#ifdef __cplusplus
}
#endif

#endif /* ROBOHUB_MQTT_H */
