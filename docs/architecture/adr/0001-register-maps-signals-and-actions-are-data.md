---
id: ADR-0001
title: A device's register map, signals and actions are data
status: proposed
date: 2026-11-19
requirements: [DEVS-0001, DEVS-0009, DEVS-INV-0001, DEVS-INV-0002]
supersedes: []
superseded_by: []
---

# A device's register map, signals and actions are data

## Context

The collection integrates equipment it does not make: a Growatt whose Modbus
register map differs between models and firmware, a Hoymiles behind an OpenDTU,
a Shelly plug reachable over HTTP or MQTT. Before 0.4.2 the Growatt map was a
placeholder in code; before 0.4.0 an application that wanted to switch a plug had
to know it was a Shelly. Mesh polls HTTP devices itself (robohub:ADR-0008) and
applications reach devices only through named interfaces (robohub:ADR-0007), so
neither can read the C driver to learn what a device offers.

## Decision

- A Modbus device's registers are a map value (`RoboHubGrowattMap`): a block
  base and count, and per figure its register, width, sign and scale. The
  default map is a starting point for one model; moving a register is a change
  to the map, not to the decode.
- Each device integration declares in its `module.yml` what can be read from it
  and what it can be told to do: a `device` block (kind, manufacturer, model,
  transports), `telemetry.interface`, and for HTTP and MQTT devices one line per
  signal, `signal|field|unit|type|json/path|scale`, plus `actions:` per
  transport.
- The collection lists its device integrations under `devices:`, in the order a
  picker offers them; a consumer reads that list instead of scanning the tree.

## Consequences

- A new inverter model is a new map, and a new HTTP device can be read by Mesh
  from its manifest alone.
- The C driver and the manifest describe the same device twice; they can drift
  (the Hoymiles day's yield was wrong in both until 0.4.2), so a unit or scale
  change must be made in both and tested against the device's documented
  document.
- The default Growatt map has not yet been read from a real inverter; the
  documentation says how to confirm it on site.
