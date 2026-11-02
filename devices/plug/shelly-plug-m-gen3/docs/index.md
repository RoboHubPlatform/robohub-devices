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
