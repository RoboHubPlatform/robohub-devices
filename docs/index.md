# RoboHub Devices

The devices a node is made of: inverters, switches, actuators and what they
report. Each device is a collection of drivers and protocols that implement
the interfaces declared by `robohub-common`.

## The devices

- **Inverters**: Growatt, Hoymiles HMS
- **Plugs**: Shelly Plug M Gen3 over HTTP RPC or MQTT
- **Actuators**: relay
- **Sensors**: photoresistor, thermistor, analog ADC, encoder, pulse ADC,
  infrared receiver

`protocols/http`, `protocols/json` and `protocols/mqtt` are the protocol
clients a network device is built on.

## Signals

A device declares the signals a dashboard may bind to in a top-level `signals:`
list. Each item names an `id`, a `label`, a `direction` (input or output), a
`unit`, the gateway `field` it reads, and the widget `kinds` it can feed. A plug
is both: its switch is an output for a button or a toggle, and its power is an
input for a number, chart or table:

```yaml
signals:
  - id: switch
    label: Switch
    direction: output
    field: state
    kinds: "toggle button"
  - id: power
    label: Power
    direction: input
    unit: W
    field: power_w
    kinds: "numeric chart table"
```

A device without the block falls back to the signals of its `device.kind`.

## Building

```sh
cd nodes/gateway-node
rho build .
```

## Testing

```sh
ROBOHUB_COMMON_DIR=/path/to/robohub-common \\
ROBOHUB_LINUX_DIR=/path/to/robohub-linux \\
ROBOHUB_ORCHESTRATOR_DIR=/path/to/robohub \\
  tests/run.sh
```
