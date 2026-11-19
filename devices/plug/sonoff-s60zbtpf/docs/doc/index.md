# Sonoff S60ZBTPF (Zigbee)

The Sonoff **S60ZBTPF** is the Zigbee model of Sonoff's iPlug S60 series: a
type F (Schuko) plug rated 16 A / 4000 W, with a relay and a power meter, and a
Zigbee 3.0 router. RoboHub reads and switches it **on its stock firmware**,
through a Zigbee coordinator that RoboHub Mesh runs **Zigbee2MQTT** for
(ADR-0003, robohub ADR-0017). Zigbee2MQTT makes the plug an MQTT device on the
keeper's broker, and Mesh reads it from this module's manifest, as it reads a
Tasmota or a Shelly plug.

**It needs no reflash and no eWeLink account.** Nothing is opened, nothing is
written to the plug, and the eWeLink app and cloud are never used (SYS-0001):
the plug talks to your coordinator and to nothing else.

**Variants.** The UK model, the **S60ZBTPG**, has another socket and another
model name; Mesh matches a joined device by the model Zigbee2MQTT reports, so it
is not matched to this manifest, and it is untested here. The Wi-Fi model, the S60TPF, is a different radio: see
{ref}`robohub-device-sonoff-s60tpf/index`.

## Before you start

You need a mesh with a **Zigbee coordinator** placed under one of its boards — a
ZBDongle-E or ZBDongle-P plugged into this machine or into a host board — and
Zigbee2MQTT running for it (Mesh starts it). See
{ref}`robohub-device-sonoff-zbdongle-e/index` and
{ref}`robohub-device-sonoff-zbdongle-p/index` for the dongle.

## Pairing

1. In Mesh, open the coordinator and **open joining**. Joining stays open for at
   most 254 seconds, Zigbee2MQTT's limit; open it again if it runs out.
2. Plug the S60ZBTPF into a socket near the coordinator (or near another mains
   Zigbee device, which routes for it).
3. **Press and hold the plug's button for about 5 seconds, until its LED
   blinks**, then let go. The plug leaves any network it was on and looks for
   one that is open.
4. Within a minute it appears among the coordinator's discovered devices as
   *SONOFF S60ZBTPF*. Zigbee2MQTT interviews it first; wait until the interview
   has succeeded, then **adopt** it into the plan. Mesh gives it the address you
   choose and renames it to that address in Zigbee2MQTT.
5. Close joining (it closes by itself when its time runs out).

A plug that does not appear: hold the button again until the LED blinks, check
that joining is still open, and bring the plug closer to the coordinator for the
first pairing. A plug that was paired to another network (an eWeLink hub, say)
must be reset this way before it can join.

## What it reports, and how often

Zigbee2MQTT publishes the plug's whole state as one JSON message on
`zigbee/<mesh>/<coordinator>/<address>`, for example:

```json
{"state":"ON","power":12.5,"voltage":231,"current":0.06,"energy":3.42,
 "energy_today":0.12,"energy_yesterday":0.4,"energy_month":2.1,"linkquality":120}
```

| Member | Read as | Unit | When the plug pushes it |
|---|---|---|---|
| `state` | switch | ON/OFF | on every change, from the app, a button press or a command |
| `power` | power | W | **at most every 10 s, on change** |
| `current` | current | A | **at most every 10 s, on change** |
| `voltage` | voltage | V | **read when the plug is paired, not reported afterwards** |
| `energy_today` | energy today | kWh | **every 1 to 60 minutes** |
| `energy` | energy total | kWh | with the metering cluster's total |

`energy_yesterday`, `energy_month`, `linkquality` and the plug's settings are in
the message too; nothing reads them.

- **Voltage does not change.** The plug reports its voltage once, when it is
  paired (or when its value is read again from Zigbee2MQTT); after that the
  shown voltage stays at that reading. That is the plug, not a fault. A message
  without `voltage` leaves the last value.
- **Power and current read 0 while the plug is off.** The plug's firmware forces
  them to 0 when the relay is open, and Zigbee2MQTT passes that on. A load that
  draws standby power behind an off plug shows 0, as it should.
- **Unreachable.** Zigbee2MQTT publishes `{"state":"online"}` or
  `{"state":"offline"}` on `<prefix>/availability`; a plug it has not heard from
  for 10 minutes is offline, and Mesh shows it unreachable until it is online
  again.

It is switched by publishing `{"state":"ON"}` or `{"state":"OFF"}` to
`<prefix>/set`. Zigbee2MQTT refuses a JSON `true`/`false`, so the manifest's
action carries the word.

## In a plan

The manifest describes the plug as data (ADR-0001); no C driver runs it, and the
collection's tests hold its lines to Zigbee2MQTT's messages (DEVS-PLUG-0013):

- `zigbee.model` and `zigbee.vendor` are `S60ZBTPF` and `SONOFF`, what
  Zigbee2MQTT reports for the plug: Mesh matches a device that joined to this
  manifest by them.
- `telemetry_mqtt.status_topics` is `{prefix}` itself, where Zigbee2MQTT
  publishes the state; `{prefix}` is `zigbee/<mesh>/<coordinator>/<address>`.
- `telemetry_mqtt_fields` reads `state` as `onoff`, `power`, `voltage` and
  `current` as numbers, `energy_today` and `energy` (as `energy_total`), each at
  a scale of 1.
- `telemetry_mqtt.online_topic` is `{prefix}/availability`, with
  `online_path: state` and `online_up: "online"`: the online state is the member
  `state` of the JSON payload, compared letter for letter.
- `actions.switch` publishes `{"state":"{ONOFF}"}` to `{prefix}/set`, `{ONOFF}`
  being `ON` or `OFF` for the requested state.

**Credentials.** None are in the manifest or the plan. The broker user
Zigbee2MQTT connects with is issued by the keeper to Mesh for that coordinator,
and the Zigbee network key is generated by Zigbee2MQTT and kept beside its
configuration.
