# Shelly Plug M Gen3

A Wi-Fi smart plug with a switch and a power meter, reached through Shelly's
JSON-RPC. The integration presents `RoboHubSwitch` and produces a
`RoboHubPowerReading`; a caller does not know which transport carried it.

## Transports

- **HTTP RPC** — `GET /rpc/Shelly.GetStatus` reads the switch and the power
  figures; `GET /rpc/Switch.Set?id=0&on=true|false` drives the switch.
- **MQTT** — the integration publishes `Switch.Set` to `<prefix>/rpc` and
  subscribes to the two topics a Gen2+/Gen3 plug reports its switch on:
  `<prefix>/status/switch:0`, the switch object alone (the plug's "generic status
  update over MQTT" option), and `<prefix>/events/rpc`, where `NotifyStatus`
  carries only what changed under `params/switch:0` ("RPC status notifications
  over MQTT"). Either one, or both, keeps the state current; a notification for
  another component, or an event, changes nothing. The platform supplies the
  broker client through the `robohub-mqtt` seam and hands each payload to
  `robohub_shelly_ingest_status`.

## Configuration

`RoboHubShellyConfig` says how the plug is reached:

| Field | Transport | Meaning |
|---|---|---|
| `host`, `port` | HTTP | Where the plug answers RPC. |
| `device_id` | MQTT | The plug's Shelly id, `shellyplugmg3-<mac>`. |
| `topic_prefix` | MQTT | The prefix set in the plug's MQTT settings (`MQTT.SetConfig` `topic_prefix`); NULL means the device id, which is Shelly's default. One of the two is required. |
| `source` | MQTT | The `src` of the requests sent to the plug, and so where it answers (`<source>/rpc`); NULL means `robohub`. A source holding `"`, `\` or a control character is refused. |
| `mqtt` | MQTT | The platform's client. |

The same cached state and power reading are produced either way, so a gateway,
a relay or the Mesh application can treat the plug like any other switch.

## In a plan

The manifest describes both transports as data, so an application reads and
switches the plug without knowing it is a Shelly:

- **HTTP** — `telemetry.http_path` and `telemetry_http_fields` say what to read;
  `actions.switch.http` how to switch it. The plan names the plug's address.
- **MQTT** — `telemetry_mqtt.status_topic` and `telemetry_mqtt_fields` say what to
  read, `actions.switch.mqtt_topic` and `mqtt_payload` how to switch it. The plan
  names the plug's topic prefix (its device id, `shellyplugmg3-<mac>`, unless it was
  given another) as its address, and the plug
  is pointed at the keeper's broker, with a broker user the keeper made for it.
