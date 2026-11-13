# Shelly Plug M Gen3

A Wi-Fi smart plug with a switch and a power meter, reached through Shelly's
JSON-RPC. The integration presents `RoboHubSwitch` and produces a
`RoboHubPowerReading`; a caller does not know which transport carried it.

## Transports

- **HTTP RPC** — `GET /rpc/Shelly.GetStatus` reads the switch and the power
  figures; `GET /rpc/Switch.Set?id=0&on=true|false` drives the switch.
- **MQTT** — the integration publishes `Switch.Set` to
  `shellyplugmgen3-<id>/rpc` and receives the status document from
  `shellyplugmgen3-<id>/status/switch:0`. The platform supplies the broker
  client through the `robohub-mqtt` seam.

The same cached state and power reading are produced either way, so a gateway,
a relay or the Mesh application can treat the plug like any other switch.

## In a plan

The manifest describes both transports as data, so an application reads and
switches the plug without knowing it is a Shelly:

- **HTTP** — `telemetry.http_path` and `telemetry_http_fields` say what to read;
  `actions.switch.http` how to switch it. The plan names the plug's address.
- **MQTT** — `telemetry_mqtt.status_topic` and `telemetry_mqtt_fields` say what to
  read, `actions.switch.mqtt_topic` and `mqtt_payload` how to switch it. The plan
  names the plug's device id (`shellyplugmgen3-<id>`) as its address, and the plug
  is pointed at the keeper's broker, with a broker user the keeper made for it.
