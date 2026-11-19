---
id: ADR-0002
title: Sonoff S60TPF is supported reflashed with Tasmota, over MQTT
status: proposed
date: 2026-11-19
requirements: [DEVS-0010, DEVS-PLUG-0005, DEVS-PLUG-0006, DEVS-PLUG-0007, DEVS-PLUG-0008, DEVS-PLUG-0009, DEVS-PLUG-0010, DEVS-PLUG-0011, SYS-0001, SYS-0025, SYS-0033, SYS-0051]
supersedes: []
superseded_by: []
---

# Sonoff S60TPF is supported reflashed with Tasmota, over MQTT

## Context

A person wants a Sonoff **S60TPF** in RoboHub: the Wi-Fi version of Sonoff's
iPlug S60 series, a type F (Schuko) plug with a power meter. SYS-0051 asks that
equipment with its own protocol be read and switched locally and appear like a
board; SYS-0001 forbids a third-party service between the device and the
application; SYS-0025, SYS-0059 and MESH-0022 ask that a switch show the state
the device reported; SYS-0033 asks that every credential stay owner-only.
ADR-0001 makes a device's signals and actions data in its manifest, which Mesh
reads to poll and switch a network device without its C driver.

### The device

| Fact | What is known | Source |
|---|---|---|
| MCU | Espressif **ESP32-C3**, 4 MB flash, on a Coolkit SM-049 module (seen on the UK S60TPG; the same pinout is reported for the EU S60) | [1], [5], [6] |
| Rating | 250 V~ 50/60 Hz, **16 A**, 4000 W max; Wi-Fi 2.4 GHz b/g/n only | [1], [2] |
| Meter | **CSE7759B** (UART, 4800 baud, even parity, read as a CSE7766 by Tasmota and ESPHome) on GPIO7; real-time voltage, current and power; eWeLink keeps daily and monthly energy and 6 months of history | [1], [3], [4] |
| Accuracy | **Not published** for the S60TPF (the newer S61STPF does publish figures); users report needing calibration, and one report reads ~1 W at ~83 W of reverse (PV) flow | [1], [4], [8] |
| Pins | relay (and red LED) GPIO4, button GPIO9 (low when pressed, also the boot strap), status LED GPIO5, meter RX GPIO7; GPIO1 drives an unpopulated transistor (another variant) | [3], [4] |
| Firmware | eWeLink (CoolKit), UIID 190 (the POWR3 protocol family), firmware 1.2.0 confirmed working with LAN control | [7] |
| Matter | **No.** Only through the eWeLink CUBE's software Matter bridge (a separate hub). The Matter-over-Wi-Fi model is the **S61STPF** (iPlug Gen2), a different board (a Beken BK7238) | [1], [2], [9] |
| Variants | S60TPF (EU type E/F), S60TPG (UK), S60ZBTPF/S60ZBTPG (Zigbee: another radio, cloud-only in eWeLink LAN terms, out of scope here) | [1], [6], [7] |
| FCC ID / teardown | No FCC ID is stated (CE/RoHS only, an EU product); teardowns: the Tasmota discussion of the EU version and an Elektroda write-up of the UK one | [1], [4], [5] |

### How a local controller can reach it

**1. eWeLink LAN mode (stock firmware).** The S60TPF supports it: it
advertises `_ewelink._tcp` over mDNS, pushes its state in the TXT records
(`data1..data4`, with `iv`, `encrypt`, `seq`) and accepts commands as an HTTP
**POST** to `http://<ip>:8081/zeroconf/<command>` — for this family
`/zeroconf/switches` with `{"switches":[{"outlet":0,"switch":"on"}],"operSide":1}`
[7], [10], [12]. The payload is encrypted with **AES-128-CBC**, key = MD5 of the
device's `devicekey`, a random IV per message, PKCS#7 padding, base64 [10]. The
`devicekey` is issued when the plug is paired to an eWeLink account: it is read
from the cloud with the person's eWeLink login, or from the plug's setup page
(`http://10.10.7.1/device`, `apikey`) during pairing [11]. Power, voltage and
current arrive locally in hundredths (`power: 1234` = 12.34 W); the day and
month energy locally too, the long history only from the cloud [12]. Sonoff does
not document the encrypted protocol; it is reverse-engineered, its best-known
client calls it "not always stable", and a vendor firmware update may change or
drop it [11].
- relay control: yes; state read back: yes (pushed in mDNS and in the POST's
  answer); power/energy: yes (real-time), with the caveats above;
- security: a per-device AES key, no authentication beyond it, no replay
  protection beyond a sequence number; the key is a long-lived secret;
- setup: an eWeLink account and the app, at least once, then the key copied out;
- robustness: at the vendor's mercy; updates arrive through the cloud.

**2. eWeLink DIY mode.** The unencrypted variant of the same API, documented by
Sonoff for firmware 3.3.0 and later on a listed set of devices (BASICR3, RFR3,
MINI, later MINIR4 and others) [13], [14]. **No source confirms DIY mode on the
S60TPF**; its specification page and Sonoff's lists do not name it [1], [14].
Not relied on.

**3. Matter over Wi-Fi.** The S60TPF is not a Matter device. Matter would mean
buying an eWeLink CUBE (a bridge, itself a vendor hub) *and* RoboHub becoming a
Matter controller (commissioning, a fabric, PASE/CASE, device attestation) — a
subsystem of its own, and better spent on a plug that speaks it (S61STPF).

**4. Reflash with Tasmota.** Done and reported on the EU and UK S60 with the
stock `tasmota32c3` build and this template [4], [6]:

```json
{"NAME":"S60","GPIO":[1,1,1,1,224,544,1,3104,1,32,1,0,0,0,0,0,0,0,1,1,1,1],"FLAG":0,"BASE":1}
```

There is **no known OTA (tuya-convert-like) path** for these ESP32-C3 plugs:
every report flashes over serial — the glued case opened, the L/N/earth bars
desoldered (or a pogo or bent-wire jig pressed onto the pads), the board powered
from its 5 V pad (3.3 V was reported to brown out the Wi-Fi), GPIO9 held low at
power-up, `esptool` writing `tasmota32c3.factory.bin` after a backup of the 4 MB
flash [4], [5], [6]. Mains must never be connected while the plug is open; in use
the GPIOs sit at mains potential [4]. What a controller then gets [15]:
- **MQTT**: `cmnd/<topic>/POWER` accepts `0|off|false` and `1|on|true`; the
  answer is `stat/<topic>/RESULT` `{"POWER":"ON"}` and `stat/<topic>/POWER`
  `ON`, also after a press of the button; every TelePeriod (default 300 s,
  10–3600) `tele/<topic>/STATE` (with `POWER`) and `tele/<topic>/SENSOR` with
  `ENERGY.{Total,Yesterday,Today,Power,ApparentPower,ReactivePower,Factor,
  Voltage,Current}` in kWh, W, VA, V and A; `tele/<topic>/LWT` `Online` or
  `Offline`. `FullTopic` is configurable, and `%topic%/%prefix%/` puts
  everything under one prefix. The broker user and password are per device
  (`MqttUser`, `MqttPassword`); TLS only in builds that carry it.
- **HTTP**: `GET /cm?cmnd=Power%20On`, `Status 10` for the meter; with a web
  password, the password goes **in the URL, in clear** (`user=…&password=…`).
- relay control: yes; state read back: yes, pushed; power/energy: yes, in the
  units RoboHub declares;
- security: the broker's per-user ACL; the person sets a web admin password;
- setup: the reflash (soldering, mains safety), then a handful of commands;
- robustness: the person's own firmware; no vendor update can undo it.

**5. Reflash with ESPHome.** A device page with a working configuration exists
(ESP32-C3, esp-idf, `cse7766` on GPIO7) [3]; flashing is the same serial work.
Its native API is protobuf over TCP with Noise encryption (a new protocol client
here), its MQTT puts each sensor on its own topic as a bare value (which the
`signal|field|unit|type|json/path|scale` grammar does not read), and its web
server switches with POST. Each plug also needs a firmware compiled for it with
its secrets built in.

**6. Others.** The eWeLink cloud API is excluded by SYS-0001. OpenBeken targets
Beken and Bouffalo chips (it fits the S61STPF, not this ESP32-C3) [9].

### What exists here

The Shelly Plug M Gen3 is read over HTTP (by the gateway and Mesh) or MQTT (by
Mesh through the keeper's broker: the address is the topic prefix, and the keeper
grants a device `readwrite <prefix>/#`). The HTTP client is GET-only
(DEVS-PROTO-0004); the collection has no POST, no AES and no mDNS; Mesh calls an
HTTP device with `curl` arguments, and the gateway's HTTP device path is written
for the Shelly's RPC.

## Decision

1. **RoboHub supports the S60TPF reflashed with Tasmota, over MQTT, first and
   for now only.** Stock eWeLink firmware is not supported; a plug that is not
   reflashed is outside this decision.
2. A new module `devices/plug/sonoff-s60tpf`, beside the Shelly, declares in
   its manifest (ADR-0001) `device: {kind: plug, manufacturer: Sonoff, model:
   S60TPF, transports: [mqtt], firmware: tasmota}`, the signals switch, power,
   voltage, current, energy_today and energy_total, the Tasmota status topics
   and field lines, the online topic `{prefix}/tele/LWT`, and the switch action
   `mqtt_topic: "{prefix}/cmnd/POWER"`, `mqtt_payload: "{on}"` (DEVS-PLUG-0009).
   The person sets `FullTopic %topic%/%prefix%/`, so the plan's address is the
   plug's Tasmota `Topic` and the keeper's device ACL fits unchanged.
3. Its C driver presents `RoboHubPlug` for a device node (DEVS-0004), reading
   the same topics (DEVS-PLUG-0005, -0006), switching by a publish and changing
   its state **only from what the plug reports** (DEVS-PLUG-0007 — unlike the
   Shelly driver, which keeps the requested state), and treating an `Offline`
   will, silence for three telemetry periods and a garbled payload as errors
   (DEVS-PLUG-0008). The broker login is the platform client's; the module
   carries **no credential** (DEVS-0010, DEVS-PLUG-0011).
4. Host tests feed the driver and the manifest's field lines the same Tasmota
   documents (DEVS-PLUG-0010).
5. The Tasmota handling stays in this module. When a second Tasmota device
   arrives it moves to a shared place (a `protocols/tasmota`, or a firmware
   profile a manifest names) rather than being copied.
6. eWeLink LAN mode is **deferred**, not rejected: wanted later, it is a
   decision of its own — a device-node driver (POST, AES-128-CBC, mDNS) with the
   `devicekey` read from an owner-only file as DEVS-0010 says.

## Consequences

- **What the person must do:** open the plug (it is glued), reach the serial
  pads, power the board from 5 V *with the plug off mains*, back up the flash and
  write `tasmota32c3.factory.bin`; then, on the plug's own access point and web
  page, join the Wi-Fi, apply the template, set `Topic <name>`,
  `FullTopic %topic%/%prefix%/`, `MqttHost`/`MqttPort` to the keeper's broker,
  `MqttUser`/`MqttPassword` to the broker user the keeper issued for the device,
  a `WebPassword`, a `TelePeriod` (10–60 s for a useful chart), and calibrate
  the meter against a known load (`PowerSet`, `VoltageSet`, `CurrentSet`). It
  voids the warranty and gives up the eWeLink app. The module's documentation
  carries all of it (DEVS-PLUG-0011).
- **Security:** commands reach the plug only through the broker, as the device's
  own user, limited to its prefix (SYS-0028). The plug's web page remains on the
  LAN, behind the web password over plain HTTP; RoboHub never uses it. No
  credential enters a manifest, plan or project file (DEVS-0010).
- **Firmware updates:** the vendor can no longer push one. A Tasmota upgrade
  could rename a JSON key or a topic; the field lines are where that is fixed,
  and the host tests hold the documented shapes.
- **Mesh needs, before the plug appears and is switched there** (robohub-mesh,
  outside this repository): a device whose manifest names **several status
  topics** (RESULT and STATE carry the switch, SENSOR the meter), merged into
  one reading; a state field typed as an **ON/OFF word** (Tasmota's
  `"POWER":"ON"`; Mesh's state words are lower-case and its JSON reader expects
  a boolean); and MESH-0022's confirmation read from whichever topic carries the
  state field. The gateway needs nothing for an MQTT device; its HTTP device
  path stays Shelly-only. The keeper's broker ACL needs nothing.
- **Readings:** voltage, current and energy today are signals Mesh stores; the
  `RoboHubPowerReading` a device node frames carries only the load power and the
  energy counters (as for the Shelly).
- The meter's accuracy is unknown until it is calibrated; readings near zero and
  reverse flow (a balcony PV) are unreliable on this metering chip.

## Alternatives considered

- **eWeLink LAN mode on stock firmware** — no soldering and the app is kept, but
  it needs an eWeLink account at least once to obtain the key, an undocumented
  encrypted protocol RoboHub would implement (POST, AES, mDNS: none exists here,
  and Mesh cannot speak it from a manifest), a long-lived key to keep, and it
  stays at the mercy of vendor updates. Deferred.
- **eWeLink DIY mode** — unencrypted and documented, but not confirmed on the
  S60TPF. Rejected for want of evidence; revisit if Sonoff lists it.
- **Matter** — not available on this model; a Matter controller is a large new
  subsystem. Rejected here; a decision of its own if the S61STPF is wanted.
- **ESPHome** — equally capable firmware, but its MQTT shape does not fit the
  field-line grammar, its native API needs a new client, and each plug needs a
  compiled firmware. Not supported for now; a person may still run it.
- **Tasmota over HTTP** — fits the GET client, but the password travels in the
  URL (and on Mesh's `curl` command line, against SYS-0033), and the gateway's
  HTTP path would have to stop being Shelly-only. Rejected; MQTT also gives
  pushed state and the keeper's per-device ACL.
- **The eWeLink cloud API** — excluded by SYS-0001.

## Sources

1. Sonoff, *S60TPF* help centre page (specifications, LAN control, Matter through the CUBE) — https://help.sonoff.tech/docs/s60tpf
2. Sonoff, *iPlug Wi-Fi Smart Plug S60 series* — https://sonoff.tech/products/sonoff-iplug-wi-fi-smart-plug-s60-series ; CNX Software, *SONOFF iPlug S60* (2023-12-22) — https://www.cnx-software.com/2023/12/22/sonoff-iplug-s60-compact-wifi-smart-socket-built-in-energy-monitoring-function/
3. ESPHome devices, *Sonoff S60TPF* — https://devices.esphome.io/devices/sonoff-s60tpf/
4. Tasmota discussion #21255, *Sonoff S60 version Europe: tasmotized* (pinout, template, flashing, calibration) — https://github.com/arendst/Tasmota/discussions/21255
5. Elektroda, S60TPG teardown (Coolkit SM-049, ESP32-C3, 4 MB) — https://www.elektroda.com/rtvforum/topic4122886.html
6. Tasmota discussion #22554, *Sonoff S60TPG (UK Version) Successfully Flashed* — https://github.com/arendst/Tasmota/discussions/22554
7. AlexxIT/SonoffLAN, DEVICES.md (S60TPF, UIID 190, firmware 1.2.0, local type `plug`) and issue #1514 — https://github.com/AlexxIT/SonoffLAN/blob/master/DEVICES.md , https://github.com/AlexxIT/SonoffLAN/issues/1514
8. Sonoff, *iPlug Gen2 Matter over Wi-Fi Smart Plug S61STPF* — https://sonoff.tech/products/sonoff-iplug-gen2-matter-over-wifi-smart-plug-s61stpf
9. Elektroda, S61STPF teardown (BK7238, OpenBeken) — https://www.elektroda.com/news/news4157852.html
10. AlexxIT/SonoffLAN, `core/ewelink/local.py` (mDNS, POST to `/zeroconf/<command>` on 8081, AES-128-CBC keyed by MD5(devicekey)) — https://github.com/AlexxIT/SonoffLAN/blob/master/custom_components/sonoff/core/ewelink/local.py
11. AlexxIT/SonoffLAN, README (the devicekey from the cloud or `http://10.10.7.1/device`; the local protocol's stability) — https://github.com/AlexxIT/SonoffLAN
12. AlexxIT/SonoffLAN, `core/devices.py` and `switch.py` (UIID 190: power, current, voltage ×0.01, `dayKwh`, the `switches` command) — https://github.com/AlexxIT/SonoffLAN/blob/master/custom_components/sonoff/core/devices.py
13. Sonoff, *DIY Mode API Protocol* — https://help.sonoff.tech/docs/DIY-MODE-API-PROTOCOL ; itead, *SONOFF DIY MODE Protocol Doc v1.4* — https://github.com/itead/Sonoff_Devices_DIY_Tools/blob/master/SONOFF%20DIY%20MODE%20Protocol%20Doc%20v1.4.md
14. Sonoff, *NSPanel Pro supported devices* (the DIY switch list; the S60 as a plug) — https://sonoff.tech/blogs/news/nspanel-pro-supported-devices-list
15. Tasmota, *Commands* (Power values, FullTopic, Status, TelePeriod, `/cm` with the password in the URL, MqttUser/MqttPassword) — https://tasmota.github.io/docs/Commands/
