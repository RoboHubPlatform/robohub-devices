# Growatt inverter (Modbus RTU)

Reads a Growatt inverter over its RS485 port and presents it as a
`RoboHubInverter`: PV, load, grid and battery power, the battery's state of
charge, and the PV energy today and in total, as a `PowerReading`.

## The default map: SPF 3000-6000 ES Plus

From Growatt's off-grid RS485 protocol V0.26, input registers (function 04),
read as one block of registers 0-101:

| Figure | Registers | Unit | Note |
|---|---|---|---|
| PV power | 3-4 + 5-6 | 0.1 W | PV1 + PV2, 32-bit, high register first |
| Load power | 9-10 | 0.1 W | the inverter's AC output |
| Grid power | 36-37 | 0.1 W | signed: + from the grid, − export |
| Battery power | 77-78 | 0.1 W | signed; Growatt counts discharge as +, the reading counts charge as + |
| Battery SoC | 18 | % | |
| PV energy today | 48-49 + 52-53 | 0.1 kWh | |
| PV energy total | 50-51 + 54-55 | 0.1 kWh | |

The map has not yet been read from a real inverter. On site, compare the
node's first readings with the inverter's own display before relying on them;
a figure that is wrong is a change to the map in `src/growatt.c` (or a map
passed to `robohub_growatt_init`), not to the decode.

## Wiring

The inverter's RS485 port (marked RS485, or the BMS/RS485 RJ45 on some units —
check the manual for the pinout) to an RS485 transceiver such as a MAX485 on the
node: A to A, B to B, ground shared. 9600 baud, 8N1, Modbus address 1 unless
holding register 30 was changed. The gateway node reads it with
`RH_DEVICE=growatt`, `RH_RS485_UART`, `RH_RS485_DE_PIN`, `RH_RS485_BAUD` and
`RH_MODBUS_SLAVE`.

Another Growatt family (SPH, MOD, MIN) has its own Modbus document and needs its
own map.
