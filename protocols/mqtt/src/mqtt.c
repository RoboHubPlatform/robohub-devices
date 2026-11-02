/*
 * mqtt.c — the one call a device integration makes through the MQTT seam.
 */
#include "mqtt.h"

#include <stddef.h>

RoboHubError robohub_mqtt_publish(RoboHubMqtt* mqtt, const char* topic,
                                  const char* payload, rh_bool retain)
{
    if (mqtt == NULL || mqtt->publish == NULL || topic == NULL || payload == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    return mqtt->publish(mqtt, topic, payload, retain);
}
