# Changelog

All notable changes to this module are recorded here, one section per release.

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
