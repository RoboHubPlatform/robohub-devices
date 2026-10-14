# robohub-devices

Integrations for equipment the ecosystem does not sense itself: inverters,
meters, batteries — machines that speak a protocol of their own (Modbus, HTTP)
rather than being read through a bus driver.

A device here presents a **device interface** from
`robohub-common/include/robohub/devices/` — `RoboHubInverter`, reporting power
and energy — and is handed to everything above as that. A Growatt over Modbus and
a Hoymiles over HTTP become the same kind of thing: a reading the gateway lists by
device, like a board's temperature.

## Not robohub-transport

The word "transport" already belongs to **`robohub-transport`**, the RoboHub wire
framing (COBS, the Fletcher-16 check, the parser) that carries a frame between a
node and a gateway. What is here under `protocols/` is a different layer
altogether: third-party **protocol clients** — Modbus RTU, an HTTP GET, a JSON
field reader — used to *talk to a device*. The device node uses both: the
protocol client to read the inverter, and `robohub-transport` to frame the reading
to the gateway. Nothing here re-implements framing.

## Layout

```
protocols/
    modbus/     Modbus RTU master over the RS485 HAL
    http/       a small HTTP GET over the network contract
    json/       a field reader for a JSON document
devices/
    inverter/
        growatt-es/     a Growatt SPH/ES hybrid over Modbus
        hoymiles-hms/   a Hoymiles microinverter through an OpenDTU
nodes/
    gateway-node/       poll a device, publish a PowerReading to a gateway
tests/                  host tests for the protocols and the devices
```

## The devices

### Growatt 6000 ES Plus (SPH/ES)

Read over its RS485 **Modbus** port, through the `max485-module` carrier (or a
host USB-RS485 adapter with automatic direction). It presents `RoboHubInverter`
with PV, load, grid and battery power, battery state of charge, and the energy
counters.

The register map is **data**: `RoboHubGrowattMap` names the address and scale of
each figure, and `robohub_growatt_default_map()` is a starting point that **must
be confirmed against your model's Modbus document**. Moving a register is a
change to the map, not to the code.

The battery attached to the inverter is read through the inverter (the `battery_*`
fields). A direct BMS integration would be a `BATTERY` device of its own later;
the interface and the `BATTERY` kind already exist for it.

### Hoymiles HMS-800-2WB

Read through an **OpenDTU** on the network: `GET /api/livedata/status`. It
presents `RoboHubInverter` with the photovoltaic power and the energy counters;
the `valid` mask says which figures it has — a microinverter has no battery, no
grid meter and no load, so those are absent rather than zero.

The two DC inputs (the two panels) are inputs to the inverter, not devices of
their own, and the document's `total` object is what is read.

## The node

`nodes/gateway-node` reads the configured device on a timer and publishes the
reading as a `PowerReading` frame — the same frame a board sends — so the
inverter appears in the gateway's device listing and its REST API unchanged.
Which device and where everything is come from the environment:

```
RH_DEVICE        growatt | hoymiles                 (default growatt)
AREA, NODE       the device's identity              (default 1, 10)
INTERVAL_MS      how often to read                  (default 5000)
RH_NODE_LINK     uart | tcp                         (default uart)
RH_NODE_UART     the UART the gateway is wired to   (default uart0)
RH_GATEWAY_HOST  gateway host, for a tcp link
RH_GATEWAY_PORT  gateway port, for a tcp link

Growatt:
RH_RS485_UART    the UART the MAX485 is on          (default uart1)
RH_RS485_DE_GPIO the direction pin's controller; empty means automatic
RH_RS485_DE_PIN  the direction pin                  (default 20)
RH_RS485_BAUD    the bus speed                      (default 9600)
RH_MODBUS_SLAVE  the inverter's unit id             (default 1)

Hoymiles:
RH_OPENDTU_HOST  the OpenDTU's host                 (default opendtu.local)
RH_OPENDTU_PORT  the OpenDTU's HTTP port            (default 80)
```

```sh
# A Growatt over a host USB-RS485 adapter, published to a gateway on uart0:
RH_DEVICE=growatt RH_RS485_UART=uart1 RH_RS485_DE_GPIO= \
AREA=7 NODE=20 rho build -target native . && ./build/output/gateway-node

# A Hoymiles through an OpenDTU:
RH_DEVICE=hoymiles RH_OPENDTU_HOST=192.168.1.50 AREA=7 NODE=21 ./gateway-node
```

## Tests

The protocols and the devices have host tests under `tests/`: the Modbus framing
and its failures, the JSON reader's nesting and its accuracy, the Growatt decode
(with a test map, independent of the default), and the Hoymiles read over a real
loopback socket through the real HTTP client.

```sh
ROBOHUB_COMMON_DIR=/path/to/robohub-common \
ROBOHUB_LINUX_DIR=/path/to/robohub-linux \
    tests/run.sh
```

Two end-to-end tests run the **real node** against the **real gateway** with the
inverter simulated — the whole chain except the hardware:

```sh
# fake OpenDTU (HTTP) -> node -> gateway uplink -> device listing
GATEWAY_BIN=/path/to/robohub-gateway tests/e2e_power.py
# Modbus slave (pty) -> node (RS485 + Modbus + Growatt) -> gateway -> listing
GATEWAY_BIN=/path/to/robohub-gateway tests/e2e_growatt.py
```

For bringing the real hardware up, see [docs/runbook.md](docs/runbook.md).
