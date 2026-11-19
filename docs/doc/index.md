# RoboHub Devices

The devices a node is made of: inverters, switches, actuators and what they
report. Each device is a collection of drivers and protocols that implement
the interfaces declared by `robohub-common`.

A device is a module of type `device`. It is not a `driver`: a driver reaches a
part through the HAL, and a device reaches the equipment that part is in, often
over a protocol of the equipment's own. The type is declared by the ecosystem
contract in `robohub-common/contracts/`, and its `device:` block says what the
equipment is and how it is reached.

A device may be an integration (a library a node talks to) or an application
(the firmware a device is built from). `apps/smart-plug` is the second: it has a
`main`, a `build` section and its own `signals:`, and it composes the relay and
DS18B20 drivers rather than reimplementing them.

## The devices

Each device has a page of its own:

| Kind | Device | Page |
|---|---|---|
| Inverter | Growatt SPF ES Plus, over Modbus RTU (RS485) | {ref}`robohub-device-growatt-es/index` |
| Inverter | Hoymiles HM/HMS microinverters, through an OpenDTU | {ref}`robohub-device-hoymiles-hms/index` |
| Plug | Shelly Plug M Gen3, over HTTP RPC or MQTT | {ref}`robohub-device-shelly-plug-m-gen3/index` |
| Plug | Sonoff S60TPF reflashed with Tasmota, over MQTT | {ref}`robohub-device-sonoff-s60tpf/index` |
| Actuator | A relay on a GPIO | {ref}`robohub-device-relay/index` |

Sensors are not devices: a DS18B20, a thermistor, a photoresistor, an encoder or
an ADC is a part a board reaches through the HAL, so it is a driver in
`robohub-drivers` (see {ref}`robohub-drivers/index`). A device composes drivers
— the smart plug is the relay and the DS18B20 driver together.

`protocols/http`, `protocols/json`, `protocols/modbus` and `protocols/mqtt` are
the protocol clients a network device is built on (MQTT:
{ref}`robohub-devices-mqtt/index`). `runtime/device-node` is the runtime a
device's firmware runs on, and `nodes/gateway-node` the node that reads one
inverter and sends its readings to a gateway.

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
rho test .
```

`tests/tests.yml` declares the suites, over the CTest project in
`tests/CMakeLists.txt`. `host` runs `tests/run.sh` — the protocols, the
drivers and the gateway node's key file — against the sibling checkouts of
robohub, robohub-common and robohub-linux, found under `ROBOHUB_WORKSPACE` (or
beside this repository). The `e2e-*` suites are the end-to-end scripts with a real
gateway; they need another repository's binary, so `rho test` reports them
skipped with that reason, and they are run with CTest directly:

```sh
rho build -target native nodes/gateway-node
cmake -S tests -B build/tests/e2e -DROBOHUB_DEVICES_TESTS=e2e
GATEWAY_BIN=/path/to/robohub-gateway ctest --test-dir build/tests/e2e --output-on-failure
```

A script that cannot run there — no `GATEWAY_BIN`, no node build — exits 77 and
CTest reports it skipped with the reason it printed. The host tests alone, as
before:

```sh
ROBOHUB_COMMON_DIR=/path/to/robohub-common \
ROBOHUB_LINUX_DIR=/path/to/robohub-linux \
ROBOHUB_ORCHESTRATOR_DIR=/path/to/robohub \
  tests/run.sh
```

The sibling checkouts are named by the environment because a path into another
repository belongs to the machine, not to this one.

## Bringing up real hardware

The {ref}`integration runbook <robohub-devices/runbook>` takes the two inverters from a host-only
end-to-end test to wired hardware: the gateway in master mode, the Growatt over
RS485, the Hoymiles through its OpenDTU, a custom app with control, the order to
bring them up in, and what to check when something is wrong.
