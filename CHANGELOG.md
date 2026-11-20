# Changelog

All notable changes to this module are recorded here, one section per release.

## [0.8.2] - 2026-11-20

The gateway node's protocol copies know the update messages (robohub-protocol 1.5.0).

## [0.8.1] - 2026-11-20

The gateway node's protocol copies know `HEALTH` (0x0403) and `ResetReason` (robohub-protocol 1.4.0).

## [0.8.0] - 2026-11-20

The device-node runtime (`runtime/device-node`, `robohub-device-node`) moved to the robohub-node collection (`device-node/`), with its requirements, renumbered RNODE-DEV-0001..0003 (formerly DEVS-NODE-0001..0003; robohub ADR-0020). `apps/smart-plug`, its test node and `nodes/gateway-node` name it, and robohub-core, -transport and -telemetry, at their new paths in robohub-node; `nodes/gateway-node` stays here, an application of this collection's drivers. CI checks out robohub-node instead of the three repositories.

## [0.7.0] - 2026-11-20

Zigbee, as data (ADR-0003, DEVS-0011). A new device kind, **`coordinator`**: a
USB radio Mesh places under a board declaring `host` and `usb_host` and drives
through Zigbee2MQTT (robohub ADR-0017). Its manifest says how it appears on USB
(`usb.ids`, `usb.by_id`), what a board must have to host it (`needs`) and which
Zigbee2MQTT adapter drives it (`zigbee.adapter`), and nothing else; no C code
runs it.

- `devices/coordinator/sonoff-zbdongle-e` 0.1.0 — the Zigbee 3.0 USB Dongle Plus
  V2 (EFR32MG21): USB `1a86:55d4` and `10c4:ea60`, the `ember` adapter,
  EmberZNet 7.4.4 or later (DEVS-COORD-0002). The first coordinator to work end
  to end.
- `devices/coordinator/sonoff-zbdongle-p` 0.1.0 — the Zigbee 3.0 USB Dongle Plus
  (CC2652P): USB `10c4:ea60`, the `zstack` adapter (DEVS-COORD-0001).
- `devices/plug/sonoff-s60zbtpf` 0.1.0 — the S60ZBTPF Zigbee plug on its stock
  firmware, a manifest only (DEVS-PLUG-0012): a `zigbee` block (model S60ZBTPF,
  vendor SONOFF) Mesh matches a joined device by, the S60TPF's signals read
  from one status topic `{prefix}`, `energy_total` from Zigbee2MQTT's `energy`,
  the online state read from the member `state` of `{prefix}/availability`
  (`online_path: state`, `online_up: online`), and the switch as
  `{"state":"{ONOFF}"}` on `{prefix}/set`. Reading these needs Mesh's side of the
  same contract (robohub-mesh MESH-0034, MESH-0035).

All three are listed under `modules:` and `devices:` (the coordinators after the
plugs). They are `type: device` with no `build` section and no `package`
section: there is nothing to compile and nothing to link, and Mesh reads them as
data. Until robohub-common's device contract allows a device without `build` and
the kind `coordinator`, `rho module validate` refuses them (no CI step or test
validates the collection's nested modules today).

Each has a page: the E's tells the E and the P apart (label, USB ids, by-id
names), says where to get EmberZNet 7.4.4+ (Sonoff's web flasher, the darkxst
Silicon Labs builds) and how to reach the dongle — on a USB extension cable away
from USB 3 and Wi-Fi, from WSL with usbipd (`bind`, `attach --wsl
--auto-attach`), from a UNO Q through a powered USB-C hub (host mode is off when
the board is powered from 5V/VIN until the JCTL header is set), with the user in
`dialout` (DEVS-COORD-0003). The S60ZBTPF's says how to pair it (a ~5 s press
until the LED blinks while Mesh's joining is open), what it pushes and how often,
that voltage is read once at pairing and power and current read 0 while it is
off, and that it needs no reflash and no eWeLink account (DEVS-PLUG-0014). The
README's field-line grammar documents `online_path` and `{ONOFF}`.

New tests: `tests/test_sonoff_zigbee.c` applies the S60ZBTPF's field lines,
online path and action to Zigbee2MQTT's documented messages — on, off with power
and current at 0, a message without voltage, online and offline availability,
malformed payloads, and `{"state":"ON"}`/`{"state":"OFF"}` from `{ONOFF}` — each
test also a CTest case of its own (DEVS-PLUG-0013). `tests/check_coordinators.py`
(suite `coordinators`) checks every coordinator manifest for its protocol, USB
ids of the form `vvvv:pppp`, by-id patterns, needs and a Zigbee2MQTT adapter, and
every Zigbee device for its model and vendor, after checking itself against
broken manifests (DEVS-COORD-0004).

## [0.6.0] - 2026-11-19

A new device: the **Sonoff S60TPF**, reflashed with Tasmota, over MQTT
(`devices/plug/sonoff-s60tpf` 0.1.0, ADR-0002, DEVS-0010, DEVS-PLUG-0005..0011).
The module is named for the hardware and its manifest says `firmware: tasmota`.
Its driver presents `RoboHubPlug` for a device node: it subscribes to
`<prefix>/stat/RESULT`, `stat/POWER`, `tele/STATE`, `tele/SENSOR` and `tele/LWT`
(Tasmota with `FullTopic %topic%/%prefix%/`), folds each into one cached state,
and switches by publishing `true`/`false`, not retained, to `<prefix>/cmnd/POWER`.
The state changes only when the plug reports it, never from the request. After
an `Offline` last will, or three telemetry periods without a word, the plug is
unreachable (no state, no reading) until it is heard again; a payload that is not
the JSON or the ON/OFF word its topic carries changes nothing and is a protocol
error; a refused login or publish is the client's error, not an off switch. The
prefix is refused when empty, over 128 bytes, absolute, or holding `#`, `+` or a
space. The module carries no credential: the broker login is the platform
client's.

The manifest declares the signals switch, power, voltage, current, energy today
and energy total, several status topics (`status_topics`), the field lines with
the state typed `onoff` and the bare `stat/POWER` read through the path `$`,
`online_up: "Online"` for the LWT, and the switch action. The field-line grammar
(README, DEVS-0009) now documents `status_topics`, `$`, `onoff` and `online_up`;
the Shelly manifest is unchanged and still valid. Reading these needs Mesh's side
of the same contract.

The module's page gives the reflash with mains safety first (unplugged
throughout), the serial wiring, a backup of the 4 MB flash, `tasmota32c3` with
esptool, the template, the Tasmota commands (`Topic`, `FullTopic`, the keeper's
broker user, `WebPassword`, `TelePeriod` 10-60 s) and the calibration, a manual
step. Stock eWeLink firmware is not supported (eWeLink LAN mode is future work);
the UK S60TPG is likely identical but untested.

`tests/test_sonoff.c` feeds the driver, and the manifest's field lines, the
Tasmota documents (RESULT, POWER, STATE, SENSOR, LWT Online/Offline) and checks
that both read the same values, plus silence, malformed payloads, ON/OFF in any
case, bad prefixes, a refused publish or login, and that a request never becomes
the cached state. Each of its tests is also a CTest case of its own, so a
requirement's evidence is the test that verifies it. DEVS-PLUG-0009 and
DEVS-PLUG-0011 say the maintainer's review decisions (two state lines, `online_up`;
eWeLink LAN mode as future work, the S60TPG, TelePeriod 10-60 s, calibration as a
manual step) and were approved again.

## [0.5.2] - 2026-11-19

Security: the gateway node refuses a key file other users can reach. It read the
device key from RH_SECURE_KEY_FILE whatever the file's owner and mode, so a key
file left group- or world-readable gave every user of the machine the board's key
(and with it the board's sessions with its gateway), and one writable by others
let them choose it. The node now checks the file it opened and refuses to start,
with one line naming the file and the fix (`chmod 600 <file>`, or `chown` to the
node's user), when it is not a regular file, belongs to another user, or grants
its group or others any permission; a missing file or one that is not
`<epoch>:<32 hex digits>` stops it as before, now saying which (DEVS-NODE-0007;
gateway-node 0.1.2, the reading moved to `src/key_file.c`). `tests/test_key_file.c`
covers a good file, a missing one, each too-open mode, another user's file, a
directory, and keys of the wrong length or content. No other reader of a key file
is in this collection.

CI runs this collection's tests: `tests/tests.yml` declares the host tests
(`tests/run.sh`, through a CTest project in `tests/CMakeLists.txt`) as a ctest
suite, so `rho test .` runs them. The end-to-end scripts are suites of their own,
declared skipped with the reason, because they need another repository's gateway
binary; each script now exits 77 (skipped) rather than 2 when GATEWAY_BIN or the
node build is missing, which CTest reports as skipped with the script's reason. Of the
three, only e2e_power passes against gateway 0.6 on: e2e_growatt and
e2e_smart_plug still send plain frames on the uplink and no API ticket.

The Shelly Plug M Gen3 over MQTT reads the status a real plug sends
(shelly-plug-m-gen3 0.2.1, DEVS-PLUG-0004). It looked up `switch:0/output`,
`switch:0/apower` and so on in every payload, which is the shape of
`Shelly.GetStatus` over HTTP; the plug publishes the switch object alone on
`<prefix>/status/switch:0`, so no MQTT status was ever parsed and the plug read
as off with no power, whatever it was doing. The integration now reads the
switch object wherever it is — alone on `status/switch:0`, under `params/switch:0`
in a `NotifyStatus` on `<prefix>/events/rpc` (only the fields it carries change),
or under `switch:0` in `Shelly.GetStatus` — bounded to that object, and ignores a
notification for another component or an event. `poll` subscribes to both topics.
The topics were also fixed to `shellyplugmgen3-<device_id>/...` and the request's
`src` to `robohub`: the config now carries `topic_prefix` (default: the device id,
which is the prefix a Shelly uses until it is given another — so `device_id` is now
the whole id, `shellyplugmg3-<mac>`, not the part after the dash) and `source`
(default `robohub`; a source holding `"`, `\` or a control character is refused,
since it is written into the request's JSON). `tests/test_shelly.c` covers the
three payload shapes, partial and foreign notifications, and the topics and source
from the config.

## [0.4.3] - 2026-11-14

The device nodes' links can be sealed (robohub-transport's secure.h), as the
boards Mesh builds already are. The node runtime takes a board id and key in its
config (device-node 0.1.1) and seals its transport with them, refusing a sealed
config that cannot read the gateway's session answer. The gateway node takes them
from the robohub_secret.h Mesh writes into a build, or on a host from
RH_SECURE_UID and RH_SECURE_KEY_FILE, reads its link between readings so the
session completes, and stops rather than falling back to plain when a key is
named but cannot be read (gateway-node 0.1.1). The end-to-end test runs the node
sealed, plain against a gateway that refuses plain frames, and plain against one
that allows them, and carries the ticket the gateway's API now wants.

## [0.4.2] - 2026-11-14

The Growatt driver's default map is the SPF 3000-6000 ES Plus, from Growatt's
off-grid RS485 protocol V0.26 — the inverter this installation has — where it was
a placeholder: 32-bit powers in 0.1 W, two PV strings added, the grid's sign as
sent and the battery's turned to the reading's (+ charge), read as one block of
registers 0-101. A field now says its width and sign, so a map is the whole of a
model. It has not yet been read from a real inverter; the docs say how to check
it on site. growatt-es 0.2.0.

The Hoymiles day's yield is in kWh: OpenDTU sends it in Wh, and both the C driver
(which now reads each figure's unit) and the manifest Mesh polls (scale 0.001)
took it as kWh, a thousand times too much. The tests use the document OpenDTU
documents. hoymiles-hms 0.1.1.

## [0.4.1] - 2026-11-14

The node runtime's and the gateway node's protocol schemas are copies of robohub-protocol 1.0.0, written by its tools/sync.py, and their C regenerated from them: they gain `SensorReading` and the `PLUG` device kind, which the gateway already spoke, and the gateway node's generated code gains `Hello` and `Telemetry`, which its schema already named.

## [0.4.0] - 2026-11-13

A device says what it can be told to do as data, for every transport it speaks
(`actions:` — an HTTP path, an MQTT topic and payload), and how it is read over
MQTT (`telemetry_mqtt`, `telemetry_mqtt_fields`, the same shape as the HTTP ones).
The Shelly Plug M Gen3 carries both, so an application switches it without knowing
it is a Shelly.

## [0.3.0] - 2026-11-05

Every device integration now declares its telemetry: an interface kind, and for
the HTTP devices the path to read and one line per signal with its JSON path and
scale. The Shelly plug gained voltage, current and total-energy signals; the
Hoymiles inverter gained the photovoltaic power and energy signals it already
read. Mesh polls HTTP devices directly and stores the readings under the device
node's plan identity; a Modbus or GPIO device publishes under the same AREA and
NODE through its device node.

## [0.2.1] - 2026-11-02

The collection's `devices:` list names every device integration, and each one
declares a `device` block. A consumer reads that list instead of scanning the
tree, so adding a device updates the picker on the next render.

## [0.2.0] - 2026-11-02

A Shelly Plug M Gen3 integration: a switch and a power meter over HTTP JSON-RPC
or MQTT. The MQTT protocol seam is a module of its own, and the device declares
its kind, model and transports so a deployment planner can find it. The JSON
reader gained boolean fields for `"output":true`.

## [0.1.0] - 2026-10-14

The device integrations: Growatt and Hoymiles inverters, a relay, the HTTP and
JSON clients, and the device-node runtime.

## [0.4.4] - 2026-11-14

The protocol copies know `RELAYED`.

## [0.5.0] - 2026-11-14

The host node keeps its key as `<epoch>:<hex>` and rewrites it when the gateway rotates it. The end-to-end power test matches what the gateway allows: plain frames only on a serial link that allows them.

## [0.5.1] - 2026-11-14

The protocol copies know `SESSION_RESET`; the host nodes act on it through the transport.
