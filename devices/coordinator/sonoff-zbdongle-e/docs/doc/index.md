# Sonoff ZBDongle-E (Zigbee coordinator)

The **Sonoff Zigbee 3.0 USB Dongle Plus V2**, model **ZBDongle-E**, is a USB
Zigbee coordinator built on a Silicon Labs **EFR32MG21**. In RoboHub it is a node
of a mesh's plan: Mesh places it under a board that can host a USB device, and
runs **Zigbee2MQTT** for it on that board, with Zigbee2MQTT's **`ember`**
adapter (ADR-0003, robohub ADR-0017). The Zigbee devices that join it — a Sonoff
S60ZBTPF plug, say — become MQTT devices on the keeper's broker. No C code runs
the dongle, and nothing of RoboHub is written to it.

## ZBDongle-E or ZBDongle-P?

Sonoff sold two dongles under almost the same name, with different radios and
different Zigbee2MQTT adapters. Choosing the wrong manifest means Zigbee2MQTT
cannot start, so check which one you have:

| | ZBDongle-E (this page) | ZBDongle-P |
|---|---|---|
| Name on the label / box | *Zigbee 3.0 USB Dongle Plus* **V2**, model **ZBDongle-E** | *Zigbee 3.0 USB Dongle Plus*, model **ZBDongle-P** |
| Radio | Silicon Labs EFR32MG21 | TI CC2652P |
| USB bridge and id (`lsusb`) | WCH CH9102F, **`1a86:55d4`** (some units: CP210x, `10c4:ea60`) | Silicon Labs CP210x, **`10c4:ea60`** |
| `/dev/serial/by-id/` | `usb-ITEAD_SONOFF_Zigbee_3.0_USB_Dongle_Plus_V2_<serial>-if00` (CP210x units: `usb-Itead_Sonoff_Zigbee_3.0_USB_Dongle_Plus_V2_<serial>-if00-port0`) | `usb-ITead_Sonoff_Zigbee_3.0_USB_Dongle_Plus_<serial>-if00-port0` |
| Zigbee2MQTT adapter | `ember` | `zstack` |

The **model on the label** is what decides it. The USB id helps — `1a86:55d4` is
always an E — but `10c4:ea60` is both a P and some E units, which is why the
manifest declares the adapter rather than RoboHub guessing it from the id. The
`V2` in the by-id name is an E.

```sh
lsusb | grep -Ei '1a86:55d4|10c4:ea60'
ls -l /dev/serial/by-id/
```

## Firmware: EmberZNet 7.4.4 or later

Zigbee2MQTT's `ember` adapter needs **EZSP v13 or later, which is EmberZNet 7.4**;
the manifest asks for **7.4.4** (`firmware_min`), the oldest release the
Zigbee2MQTT guide recommends. Many dongles left the factory with 6.x or 7.1 to
7.3 firmware. A dongle with older firmware does not start: Zigbee2MQTT stops
with an EZSP protocol-version error in its log, which Mesh shows.

To see what yours has, start it once and read the version in Zigbee2MQTT's log
(`[EZSP] ... version` at start), or read it with Silicon Labs' tools. To flash it:

- **Sonoff's web flasher**, <https://dongle.sonoff.tech>, in Chrome or Edge (it
  uses Web Serial): plug the dongle into a Windows, macOS or Linux machine
  directly (not through WSL), choose *ZBDongle-E*, and flash the latest
  **EmberZNet NCP** (coordinator) image of 7.4.4 or later. Not a *router* image
  and not *Thread/RCP*.
- **The Silicon Labs firmware builds** for this dongle, published by darkxst at
  <https://github.com/darkxst/silabs-firmware-builder>: its releases carry a
  `zbdonglee_zigbee_ncp_<version>_sw_flow_115200.gbl` (8.0.3.0 in the
  2025-06-27 release, newer than 7.4.4 and fine), written with the web flasher
  above or with `universal-silabs-flasher`:

  ```sh
  pipx install universal-silabs-flasher
  universal-silabs-flasher --device /dev/ttyUSB0 flash \
      --firmware zbdonglee_zigbee_ncp_8.0.3.0_sw_flow_115200.gbl
  ```

  `sw_flow` is software flow control at 115200 baud, which is what Zigbee2MQTT
  uses when nothing else is configured.

Close Zigbee2MQTT (stop the coordinator in Mesh) before flashing: only one
program can hold the serial port. Flashing keeps nothing of a previous network's
pairing on the dongle; Zigbee2MQTT's own backup is what restores it.

## Plugging it in

- **Use a USB extension cable** (50 cm to 1 m) and put the dongle away from the
  computer's case, from **USB 3 ports, cables and drives** (USB 3 radiates in the
  2.4 GHz band), and from **Wi-Fi antennas and routers**. Plugged straight into a
  port, most coordinators work badly or not at all.
- Keep it in one place: the devices route through each other, and moving the
  coordinator moves the centre of the network.

### On this machine under WSL

WSL does not see a Windows USB device until it is passed through with
**usbipd-win**. In Windows (PowerShell):

```powershell
winget install --interactive --exact dorssel.usbipd-win
usbipd list                                   # find the dongle's BUSID, e.g. 2-3
usbipd bind --busid <id>                      # once, in an administrator PowerShell
usbipd attach --wsl --busid <id> --auto-attach
```

`bind` needs an administrator PowerShell and is done once per dongle;
`attach` runs as your user. `--auto-attach` keeps attaching it whenever it is
plugged in again or WSL restarts, for as long as the command runs (leave its
window open, or start it at sign-in); without it, attach again after every
reboot. While it is attached to WSL, Windows cannot use the dongle. Then, in
WSL, `lsusb` lists it and `/dev/ttyUSB0` or `/dev/ttyACM0` appears. Behind
usbipd the `/dev/serial/by-id/` name may differ from the one above; Mesh shows
the ports it finds.

### On an Arduino UNO Q

The UNO Q's Linux side has one **USB-C** port, which switches between device and
host. Plug the dongle in through a **powered USB-C hub** (a hub with its own power supply), not
straight into the port. The board's manifest declares `usb_host: true`, so Mesh
offers it as a place for the coordinator.

**Host mode is not enabled when the board is powered from its 5V or VIN pin.**
Powered that way, the USB-C port stays a device port until the JCTL header is set
as Arduino describes in [UNO Q host
mode](https://docs.arduino.cc/tutorials/uno-q/host-mode/).

## The serial device's group

The user that runs Zigbee2MQTT — your user on this machine, `arduino` on a UNO Q
— must be allowed to open the serial device. On Debian and Ubuntu the device
belongs to the group **`dialout`**:

```sh
ls -l /dev/ttyUSB0              # crw-rw---- 1 root dialout ...
sudo usermod -aG dialout "$USER"
```

Sign out and in again (in WSL, `wsl --shutdown` from Windows) for the group to
apply. On Arch and Fedora the group is `uucp`; `ls -l` shows which. A user not in
the group gets *Permission denied* on the port, which Zigbee2MQTT's log names.

## In a plan

The manifest (DEVS-COORD-0002) says what Mesh needs and nothing else:

| Block | Value | What Mesh does with it |
|---|---|---|
| `device` | kind `coordinator`, protocol `zigbee`, chip `EFR32MG21` | offers it as a coordinator node |
| `usb.ids` | `1a86:55d4`, `10c4:ea60` | recognises it among a board's USB devices |
| `usb.by_id` | the two patterns above | picks the stable `/dev/serial/by-id/` path for `serial.port` |
| `needs` | `host: true`, `usb_host: true` | places it only under a board that declares both |
| `zigbee.adapter` | `ember` | writes `serial.adapter: ember` into Zigbee2MQTT's configuration |
| `zigbee.firmware_min` | `EmberZNet 7.4.4` | says which firmware to flash when it does not start |

Zigbee2MQTT's network key, PAN id and extended PAN id are generated on its first
start and kept beside its configuration on the board, not in the plan; the broker
user it connects with is issued by the keeper for this coordinator alone.
