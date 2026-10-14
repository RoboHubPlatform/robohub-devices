# Integration runbook

Everything needed to bring the two inverters into a RoboHub gateway for real.
The code is in place and the whole path has been exercised on the host with the
inverters simulated; what remains is the hardware.

## What runs where

```
   Growatt (RS485) ──┐
                     ├─ robohub-device-node ──PowerReading frame──► robohub-gateway
   Hoymiles (OpenDTU)┘        (host or board)                        (host)
```

- **robohub-gateway** runs on the host (robohub-linux), reads a serial link and
  the uplink, keeps device state, serves REST and streams to PlotJuggler.
- **robohub-device-node** (`robohub-devices/nodes/gateway-node`) reads one
  inverter and publishes PowerReading frames. One instance per inverter.
- The **inverters** are reached over Modbus RTU (Growatt) or HTTP (Hoymiles,
  through an OpenDTU).

## Do this first, before the hardware

Prove the pipeline with the inverters simulated — the same C node and the same
gateway, no wiring:

```sh
# Unit tests: Modbus framing, the JSON reader, the Growatt decode, HTTP+Hoymiles.
ROBOHUB_COMMON_DIR=<...>/robohub-common ROBOHUB_LINUX_DIR=<...>/robohub-linux \
    <...>/robohub-devices/tests/run.sh

# Build the node and the gateway
cd <...>/robohub-devices/nodes/gateway-node && rho build -target native .
cd <...>/robohub-gateway && rho build .

# End to end, host only
cd <...>/robohub-devices
GATEWAY_BIN=<...>/robohub-gateway/build/robohub-gateway python3 tests/e2e_power.py    # Hoymiles
GATEWAY_BIN=<...>/robohub-gateway/build/robohub-gateway python3 tests/e2e_growatt.py  # Growatt
```

Both print `OK: ... end to end` and leave a device in `/api/v1/nodes`. If these
pass, the software path is good; only the hardware and its addressing remain.

## The gateway

Master mode, reading a serial link for boards and listening for an uplink:

```sh
cd <...>/robohub-gateway
SERIAL_PORT=/dev/ttyUSB0 SERIAL_BAUD=115200 \
REST_PORT=8080 UPLINK_PORT=8081 \
GATEWAY_MODE=master GATEWAY_AREA=7 PLOTJUGGLER_ADDR=127.0.0.1:9870 \
    ./build/robohub-gateway
```

- `GET /api/v1/nodes` — every device, with power/energy for the inverters.
- `GET /api/v1/telemetry/power` — the newest power reading.
- `GET /api/v1/telemetry/temperature` — the newest temperature reading.
- `GET /api/v1/diagnostics` — the gateway's own state.

A device node usually publishes over the gateway's **serial** link (the L2 path).
For the inverter node that link is the node's own UART (`RH_NODE_UART`), wired to
the gateway host. Where the node runs on the same host as the gateway, its TCP
link to the gateway's uplink works too and is what the e2e tests use.

## Growatt 6000 ES Plus

### Wiring

The inverter's RS485 port is A/B, half-duplex, 9600 baud by default.

| Growatt RS485 | MAX485 carrier | Board |
|---------------|----------------|-------|
| A | A | — |
| B | B | — |
| — | VCC / GND | 5 V (3.3 V part for a 3.3 V board) |
| — | DI | the UART's TX |
| — | RO | the UART's RX |
| — | DE + RE | one GPIO (tied together) |

A host can use a **USB-RS485 adapter** instead: it is a tty with automatic
direction, so no DE pin is used.

### Build and run the node

```sh
cd <...>/robohub-devices/nodes/gateway-node && rho build -target native .

# On a host with a USB-RS485 adapter (/dev/ttyUSB1), automatic direction:
RH_DEVICE=growatt \
RH_RS485_UART=uart1 RHO_UART1=/dev/ttyUSB1 RH_RS485_DE_GPIO= \
RH_RS485_BAUD=9600 RH_MODBUS_SLAVE=1 \
AREA=7 NODE=20 INTERVAL_MS=5000 \
RH_NODE_LINK=tcp RH_GATEWAY_HOST=127.0.0.1 RH_GATEWAY_PORT=8081 \
    ./build/output/gateway_node
```

On a board with the MAX485 carrier, drop the `RHO_UART1=` and name the direction
pin instead: `RH_RS485_DE_GPIO=gpio0 RH_RS485_DE_PIN=20`, with `RH_NODE_UART` the
UART wired to the gateway.

`RH_MODBUS_SLAVE` is the unit id the inverter answers to; set it from the
inverter's own menu. `9600` is the usual speed.

### Confirm the register map first

The default map in `robohub-devices/devices/inverter/growatt-es/src/growatt.c`
(`robohub_growatt_default_map`) is a **starting point** and must be confirmed
against your model's Modbus document. If the readings are wrong, correct the
addresses and scales there — moving a register is a one-line change and nothing
else in the driver changes. Read the raw block once to check:

```sh
# A quick read of the first 48 input registers of unit 1, as JSON:
RH_DEVICE=growatt ... INTERVAL_MS=2000 ./gateway_node
# then watch the gateway, or a Modbus tool that can read input registers
# (function 04) at 9600 8N1.
```

A figure that is exactly tenfold off is a scale of 100 where it should be 10 (or
the reverse); a figure that is nonsense is an address.

## Hoymiles HMS-800-2WB

The inverter is reached through an **OpenDTU** (or equivalent DTU) on the LAN.
Confirm it answers first:

```sh
curl http://<opendtu-ip>/api/livedata/status
```

The fields used are `total/Power/v` (W), `total/YieldDay/v` and
`total/YieldTotal/v` (kWh). If a firmware names them differently, that is the one
place to adjust (`hoymiles_figure` in `hoymiles.c`).

```sh
RH_DEVICE=hoymiles RH_OPENDTU_HOST=<opendtu-ip> RH_OPENDTU_PORT=80 \
AREA=7 NODE=21 INTERVAL_MS=5000 \
RH_NODE_LINK=tcp RH_GATEWAY_HOST=127.0.0.1 RH_GATEWAY_PORT=8081 \
    ./build/output/gateway_node
```

A microinverter has no battery, grid meter or load, so those fields are absent
(`valid` says which figures are present); the battery attached to the Growatt is
read through the Growatt.

## Bring-up order

1. Gateway first, so the link is up before any node publishes.
2. `curl /api/v1/nodes` — empty is correct before a node runs.
3. One node. Watch its stdout: it prints the device and `read pv=… W` per cycle,
   or `the device did not answer`.
4. `curl /api/v1/nodes` — the inverter appears by `(area, node)`. If the node
   prints a reading but the gateway does not list it, the link is the problem
   (the serial port, or the uplink port).
5. `curl /api/v1/telemetry/power` — the same figures.
6. Point PlotJuggler at the UDP address for a live plot.

## If something is wrong

| Symptom | Look at |
|---------|---------|
| Node prints `the device did not answer` (Growatt) | A/B swapped, baud, unit id, the DE pin or the drive level, termination |
| Modbus read accepted but values wrong | the register map and scales (see above) |
| Node prints `the device could not be opened` | a named peripheral is not registered: `RH_RS485_UART` / `RHO_UART1` for the UART, `RH_RS485_DE_GPIO` for the pin |
| Node reads (Hoymiles) but no figures | the OpenDTU document's keys, or the wrong path |
| Gateway does not list the device | the node's link: `RH_NODE_LINK`/`RH_GATEWAY_*`, and that the gateway's port is the one the node dials |
| A device listed but power is 0 | the `valid` mask — the figure may be absent, not zero |
