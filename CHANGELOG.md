# Changelog

All notable changes to this module are recorded here, one section per release.

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
