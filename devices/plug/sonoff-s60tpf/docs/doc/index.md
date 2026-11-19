# Sonoff S60TPF (Tasmota)

The Sonoff **S60TPF** is the Wi-Fi model of Sonoff's iPlug S60 series: a type F
(Schuko) plug rated 16 A / 4000 W, with a relay and a power meter, built on an
ESP32-C3 with 4 MB of flash. RoboHub supports it **reflashed with Tasmota**
(`tasmota32c3`) and reached **over MQTT**, through the keeper's broker
(ADR-0002). The integration presents `RoboHubPlug` and produces a
`RoboHubPowerReading`; Mesh reads and switches it from this module's manifest.

:::{warning}
**Stock eWeLink firmware is not supported.** A plug that has not been reflashed
cannot be used here: RoboHub does not speak eWeLink's encrypted LAN protocol, and
the eWeLink cloud is never used (SYS-0001). eWeLink LAN mode on stock firmware is
future work, a decision of its own. Reflashing voids the warranty and gives up
the eWeLink app; the vendor can no longer update the plug.
:::

**Variants.** The UK model, the **S60TPG**, is reported flashed the same way with
the same template and is *likely identical, but untested here*; it is not claimed
as covered. The Zigbee models (S60ZBTPF, S60ZBTPG) are another radio and are out
of scope. The S61STPF (iPlug Gen2, Matter) is a different board.

## Before you start: mains safety

:::{warning}
The plug switches mains. **Unplug it from the wall and keep it unplugged for the
whole job** — opening, wiring, flashing and closing. Never connect it to mains
while it is open, and never connect a serial adapter while it is in a socket: in
use, its low-voltage side sits at mains potential. Power the board only from the
serial adapter, as below. Close the case completely before it goes back into a
socket. If you are not comfortable working on a mains device, do not do this.
:::

You need: a USB–serial adapter with a **5 V** output and 3.3 V logic (an FT232,
CP2102 or CH340 board), four wires or a pogo-pin clip, a soldering iron (or a
jig that presses wires onto the pads), a spudger to open the case, and
`esptool` (`pip install esptool`) on the computer.

## 1. Open the case and reach the pads

The case is glued. With the plug unplugged, prise the back off along its seam
with a spudger. The board is held by the L, N and earth bars; to reach the
module's pads, desolder the bars or press a pogo or bent-wire jig onto the pads
from the side that is free. The ESP32-C3 sits on a Coolkit SM-049 module.

The pins that matter (from the Tasmota discussion of the EU plug):

| Pad / GPIO | What it is |
|---|---|
| `5V`, `GND` | power for the board while it is flashed |
| `TX`, `RX` | the ESP32-C3's UART0 |
| GPIO9 | the button, and the boot strap: low at power-up enters the bootloader |
| GPIO4 | relay and red LED |
| GPIO5 | status LED |
| GPIO7 | the meter's serial line (CSE7759B, read as a CSE7766) |

## 2. Wire the serial adapter

| Adapter | Plug |
|---|---|
| 5V | 5V |
| GND | GND |
| TX | RX |
| RX | TX |

Power the board from **5 V**: powering it from 3.3 V was reported to brown out
the Wi-Fi. Hold GPIO9 low (the button pressed, or GPIO9 wired to GND) while you
connect the adapter, then release it: the ESP32-C3 starts in its bootloader.

## 3. Back up the stock firmware

Read the whole 4 MB flash before writing anything; it is the only way back to
eWeLink:

```sh
esptool.py --chip esp32c3 --port /dev/ttyUSB0 read_flash 0x0 0x400000 s60tpf-stock.bin
```

Keep the file. Enter the bootloader again (GPIO9 low at power-up) before each
esptool command if the plug has restarted.

## 4. Write Tasmota

Download `tasmota32c3.factory.bin` from Tasmota's releases and write it at
address 0:

```sh
esptool.py --chip esp32c3 --port /dev/ttyUSB0 erase_flash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash 0x0 tasmota32c3.factory.bin
```

Disconnect the adapter, close the case completely, and only then plug it in.

## 5. Configure Tasmota

On first start the plug opens an access point (`tasmota-XXXXXX`). Join it, open
`http://192.168.4.1`, and give it your Wi-Fi (2.4 GHz only). Then, on its web
page, **Configuration → Configure Other**, paste the template, tick *Activate*
and save:

```json
{"NAME":"S60","GPIO":[1,1,1,1,224,544,1,3104,1,32,1,0,0,0,0,0,0,0,1,1,1,1],"FLAG":0,"BASE":1}
```

In the keeper, open the broker to the network for devices (Clients → *Open it to
the network*) and **add a broker user for the plug** on the Clients tab, allowed
its own topic prefix — the name you give the plug below. The keeper shows the
password **once**. Then, in the plug's **Console**:

```text
Topic s60-desk
FullTopic %topic%/%prefix%/
MqttHost <the keeper's host>
MqttPort 1884
MqttUser <the broker user the keeper issued for this plug>
MqttPassword <its password>
WebPassword <a password of your own for the plug's web page>
TelePeriod 30
```

- `Topic` is the plug's name and the **address** of the device in the plan. Use
  one level without `/`, `#`, `+` or spaces (`s60-desk`); the integration
  refuses a prefix that is empty, longer than 128 bytes, starts with `/`, or
  holds `#`, `+` or a space.
- `FullTopic %topic%/%prefix%/` puts everything the plug says under that one
  name — `s60-desk/stat/…`, `s60-desk/tele/…`, `s60-desk/cmnd/…` — which is the
  prefix the keeper's broker user is allowed.
- `MqttUser` and `MqttPassword` go into the **plug only**. They are never written
  into the plan, a manifest or a project file (DEVS-0010).
- `WebPassword` protects the plug's own web page on the LAN. RoboHub never uses
  that page (its password would travel in clear over HTTP).
- `TelePeriod` **must be between 10 and 60 seconds** for a useful chart and a
  prompt "unreachable" (Tasmota's default, 300 s, is too slow). Tell the device
  node the same period (`tele_period_s`).

## 6. Calibrate the meter

The S60TPF's metering accuracy is not published, and plugs differ. Calibration
is a **manual step**, done once with a known, steady, resistive load (a kettle, a
heater or an incandescent lamp) and, best, a reference meter:

1. Plug the load in through the S60TPF, switch it on, and let it settle.
2. Read the true voltage and power from the reference meter (or the load's
   rating, less precisely).
3. In the Console, with those values:

```text
VoltageSet 230
PowerSet 1985.0
CurrentSet 8630
```

`VoltageSet` is in V, `PowerSet` in W, `CurrentSet` in **mA** (power ÷ voltage ×
1000). Readings near zero, and reverse flow (a balcony PV behind the plug), stay
unreliable on this metering chip.

## What the plug says, and what is read

With the configuration above the plug publishes:

| Topic | Payload | Read as |
|---|---|---|
| `<prefix>/stat/RESULT` | `{"POWER":"ON"}` — the answer to a Power command | switch |
| `<prefix>/stat/POWER` | `ON` / `OFF` — the bare word, also after a button press | switch |
| `<prefix>/tele/STATE` | every TelePeriod, with `"POWER":"ON"` | switch |
| `<prefix>/tele/SENSOR` | every TelePeriod, `ENERGY.{Power,Voltage,Current,Today,Total}` | W, V, A, kWh, kWh |
| `<prefix>/tele/LWT` | `Online`, or `Offline` as the broker's last will | reachable |

It is switched by publishing `true` or `false`, not retained, to
`<prefix>/cmnd/POWER`. Tasmota reports in the units the readings declare, so
nothing is converted.

- **The state is what the plug reports.** A switch request is a publish; the
  cached state changes only when the plug answers on `stat/RESULT`,
  `stat/POWER` or `tele/STATE`. A request is never shown as the state.
- **Unreachable.** After an `Offline` last will, or after three telemetry
  periods without a word from the plug (the configured period, 300 s when none
  is given), it is unreachable: no state and no power reading, until it is heard
  again.
- **Errors.** A payload that is not the JSON or the ON/OFF word its topic
  carries changes nothing and is a protocol error. A broker that refuses the
  platform client's login, or a publish the client refuses, reaches the caller
  as the client's error — never as an off switch.

## Configuration of the C driver

`RoboHubSonoffConfig`, for a device node:

| Field | Meaning |
|---|---|
| `prefix` | The plug's Tasmota `Topic`. |
| `tele_period_s` | The plug's `TelePeriod` (10..3600); 0 means 300. Three periods of silence make it unreachable. |
| `mqtt` | The platform's MQTT client, which holds the broker login. |
| `now_ms` | The clock silence is measured on (`robohub_osal_now_ms` on a board). |
| `id` | The device's area and node. |

The platform hands each delivered message to `robohub_sonoff_ingest(plug, topic,
payload)`; `robohub_sonoff_poll` subscribes to the five topics.

**Credentials.** The integration needs none of its own: the broker login belongs
to the platform's MQTT client and the keeper, and the plug's broker password is
entered on the plug. Nothing is read from an environment value, a command line, a
manifest or the plan.

## In a plan

The manifest describes the plug as data (ADR-0001), so Mesh reads and switches it
without the C driver:

- `telemetry_mqtt.status_topics` lists the four status topics, merged into one
  reading; each message changes only the fields it carries.
- `telemetry_mqtt_fields` maps them: `switch|state||onoff|POWER|1` reads the
  JSON topics, `switch|state||onoff|$|1` the bare word on `stat/POWER`, and the
  meter's five lines read `ENERGY/…` with a scale of 1.
- `telemetry_mqtt.online_topic` is `{prefix}/tele/LWT`, and `online_up: "Online"`
  says which payload means online.
- `actions.switch` publishes `{on}` to `{prefix}/cmnd/POWER`.

The plan names the plug's `Topic` as the device's address, with `transport: mqtt`.
