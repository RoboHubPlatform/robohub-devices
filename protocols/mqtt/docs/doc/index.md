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

## Who uses it

The Shelly Plug M Gen3 integration publishes its `Switch.Set` command and
receives its status through this seam (see
{ref}`robohub-device-shelly-plug-m-gen3/index`). A plug that is reached over MQTT
is pointed at the broker the keeper runs, with a broker user the keeper makes for
it (see {ref}`robohub-mqtt/index`).

This module is the device side's MQTT interface, in C. It is a different thing
from the `robohub-mqtt` plugin (the keeper's broker) and from the `robohub.bus`
interface applications publish through; on the documentation site its page is
`robohub-devices-mqtt` for that reason.
