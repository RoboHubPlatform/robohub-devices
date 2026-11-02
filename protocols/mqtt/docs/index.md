# MQTT

The seam a device integration publishes and subscribes through. The platform's
network stack owns the broker connection; this module owns the interface:

```c
typedef struct RoboHubMqtt {
    void* context;
    RoboHubError (*publish)(RoboHubMqtt*, const char* topic, const char* payload, rh_bool retain);
    RoboHubError (*subscribe)(RoboHubMqtt*, const char* topic);
} RoboHubMqtt;

RoboHubError robohub_mqtt_publish(RoboHubMqtt* mqtt, const char* topic,
                                  const char* payload, rh_bool retain);
```

A device names its topic and payload; the platform delivers what was
subscribed. The capability list of `robohub-transport` includes `mqtt`, so a
deployment that has an MQTT client can select it.
