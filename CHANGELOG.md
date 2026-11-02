# Changelog

All notable changes to this module are recorded here, one section per release.

## [0.2.0] - 2026-11-02

A Shelly Plug M Gen3 integration: a switch and a power meter over HTTP JSON-RPC
or MQTT. The MQTT protocol seam is a module of its own, and the device declares
its kind, model and transports so a deployment planner can find it. The JSON
reader gained boolean fields for `"output":true`.

## [0.1.0] - 2026-10-14

The device integrations: Growatt and Hoymiles inverters, a relay, the HTTP and
JSON clients, and the device-node runtime.
