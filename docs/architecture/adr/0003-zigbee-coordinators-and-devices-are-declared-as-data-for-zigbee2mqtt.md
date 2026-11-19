---
id: ADR-0003
title: Zigbee coordinators and devices are declared as data for Zigbee2MQTT
status: accepted
date: 2026-11-20
requirements: [DEVS-0011, DEVS-PLUG-0012, DEVS-PLUG-0013, DEVS-PLUG-0014, DEVS-COORD-0001, DEVS-COORD-0002, DEVS-COORD-0003, DEVS-COORD-0004, SYS-0069, SYS-0051]
supersedes: []
superseded_by: []
---

# Zigbee coordinators and devices are declared as data for Zigbee2MQTT

## Context

robohub ADR-0017 decides that a Zigbee coordinator — a USB radio — is placed in
a mesh under a board that can host it, that Mesh runs Zigbee2MQTT for it, and
that a Zigbee device is then an MQTT device under the coordinator's base topic.
The first coordinator is the **Sonoff Zigbee 3.0 USB Dongle Plus**, which exists
in two versions, and the first device the **Sonoff S60ZBTPF** smart plug.

ADR-0001 makes a device's signals and actions data in its manifest; the S60TPF
(ADR-0002) added a C driver beside its manifest, because a device node on a board
can read Tasmota over MQTT and frame it to a gateway.

### The coordinators

| | ZBDongle-P | ZBDongle-E |
|---|---|---|
| Radio | TI CC2652P | Silicon Labs EFR32MG21 |
| USB bridge, id | Silicon Labs CP210x, `10c4:ea60` | WCH CH9102F, `1a86:55d4` (some units CP210x, `10c4:ea60`) |
| `/dev/serial/by-id/` | `usb-ITead_Sonoff_Zigbee_3.0_USB_Dongle_Plus_<serial>-if00-port0` (older: `usb-Silicon_Labs_…_0111-if00-port0`) | `usb-ITEAD_SONOFF_Zigbee_3.0_USB_Dongle_Plus_V2_<serial>-if00` (CP210x units: `usb-Itead_…_V2_<serial>-if00-port0`) |
| Zigbee2MQTT adapter | `zstack` | `ember` (EmberZNet 7.4 or later; 7.4.4+ recommended) |

Sources: Zigbee2MQTT's adapter discovery and adapter pages (robohub ADR-0017,
[1]–[3]). Neither the vendor nor Zigbee2MQTT gives a udev rule; the by-id path is
the stable name. Behind usbipd or a VM the by-id name may differ.

### The S60ZBTPF in Zigbee2MQTT

`model` S60ZBTPF, `vendor` SONOFF. One JSON state message on
`<base>/<friendly_name>` with `state` (ON/OFF), `power` (W), `voltage` (V),
`current` (A), `energy` (kWh, total), `energy_today`, `energy_yesterday`,
`energy_month`, and settings (`power_on_behavior`, `overload_protection`,
`inching_control_set`, …). Switched with `{"state":"ON"}` on `…/set`; JSON
booleans are refused. Availability `{"state":"online"}` on `…/availability`.
The plug pushes: the relay on change, power and current at most every 10 s on
change, the day's, yesterday's and month's energy every 1–60 min, the total from
the metering cluster; **voltage is read once at pairing and not reported**; power
and current are forced to 0 while the relay is off (a firmware quirk the
converter works around) [4], [5].

## Decision

1. **A new device kind, `coordinator`**, declared under `devices/coordinator/`,
   with a manifest that says what Mesh needs to place and drive it and nothing
   else:

   ```yaml
   device:
     kind: coordinator
     manufacturer: SONOFF
     model: ZBDongle-P
     protocol: zigbee
     chip: CC2652P
   usb:
     ids: ["10c4:ea60"]
     by_id:
       - "usb-ITead_Sonoff_Zigbee_3.0_USB_Dongle_Plus_*-if00-port0"
       - "usb-Silicon_Labs_Sonoff_Zigbee_3.0_USB_Dongle_Plus_*-if00-port0"
   needs:
     host: true
     usb_host: true
   zigbee:
     adapter: zstack
   ```

   `sonoff-zbdongle-p` (DEVS-COORD-0001) and `sonoff-zbdongle-e`
   (DEVS-COORD-0002, adapter `ember`, `firmware_min: "EmberZNet 7.4.4"`). No C
   code: the coordinator is driven by Zigbee2MQTT, on the host.
2. **The S60ZBTPF is a manifest only** (`devices/plug/sonoff-s60zbtpf`,
   DEVS-PLUG-0012): `transports: [zigbee]`, a `zigbee: {model, vendor}` block
   Mesh matches a joined device by, the ADR-0009 MQTT contract with one status
   topic `{prefix}`, `online_path: state`, `online_up: online`, and the switch as
   `{"state":"{ONOFF}"}` on `{prefix}/set` (the two contract additions are
   robohub-mesh's MESH-0034 and MESH-0035). Signals as the S60TPF's, so a Wi-Fi
   and a Zigbee plug read the same: `energy_total` from Zigbee2MQTT's `energy`.
3. **No C driver for the S60ZBTPF, for now.** A device node on a board cannot
   reach a Zigbee device except through Zigbee2MQTT's broker topics, which Mesh
   already reads; a driver would duplicate the manifest for no reader. When a
   board-side reader is wanted, it is a decision of its own.
4. **Tests hold the manifests** (DEVS-PLUG-0013, DEVS-COORD-0004): the plug's
   field lines applied to Zigbee2MQTT's documented payloads, and every coordinator
   manifest checked for its ids, patterns, needs and a known adapter.

## Consequences

- Mesh reads a new kind and two new blocks (`usb`, `needs`, `zigbee`); a reader
  that does not know them sees a device of an unknown kind and offers nothing.
- The plug's voltage reading only changes when it is re-read or re-paired; it is
  declared so it is shown, and its guide says so (DEVS-PLUG-0014).
- The plug needs no reflash and no eWeLink account: pairing is a long press of
  its button while Mesh has joining open.
- A third coordinator (the Dongle Plus MG24, a ConBee) is one more manifest with
  its adapter; a second Zigbee device is one more manifest with its `zigbee`
  block.

## Alternatives considered

- **A C driver for the S60ZBTPF**, as for the S60TPF — symmetry, but no consumer:
  only Mesh reads Zigbee devices, from the broker. Deferred.
- **Coordinators as a field of a board** rather than a device — a board would
  then carry every radio it might host; a coordinator is a part a person buys and
  plugs in, like a plug. Rejected.
- **Inferring the adapter from the USB id** — `10c4:ea60` is both a P and some
  E units. Rejected: the manifest says.

## Sources

1. Koenkk/zigbee-herdsman, `src/adapter/adapterDiscovery.ts` — https://github.com/Koenkk/zigbee-herdsman/blob/master/src/adapter/adapterDiscovery.ts
2. Zigbee2MQTT, *Z-Stack adapters* — https://www.zigbee2mqtt.io/guide/adapters/zstack.html
3. Zigbee2MQTT, *EmberZNet adapters* — https://www.zigbee2mqtt.io/guide/adapters/emberznet.html
4. Zigbee2MQTT, *SONOFF S60ZBTPF* — https://www.zigbee2mqtt.io/devices/S60ZBTPF.html
5. Koenkk/zigbee-herdsman-converters, `src/devices/sonoff.ts` (exposes, reporting) and issue 28470 (power while off) — https://github.com/Koenkk/zigbee-herdsman-converters/blob/master/src/devices/sonoff.ts , https://github.com/Koenkk/zigbee2mqtt/issues/28470
