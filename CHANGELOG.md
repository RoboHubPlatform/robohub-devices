# Changelog

All notable changes to this module are recorded here, one section per release.

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

The protocol copies know `SESSION_RESET`; the host nodes act on it through the transport.
