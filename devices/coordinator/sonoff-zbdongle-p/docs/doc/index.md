# Sonoff ZBDongle-P (Zigbee coordinator)

The **Sonoff Zigbee 3.0 USB Dongle Plus**, model **ZBDongle-P**, is a USB Zigbee
coordinator built on a TI **CC2652P**. In RoboHub it is a node of a mesh's plan:
Mesh places it under a board that can host a USB device and runs **Zigbee2MQTT**
for it on that board, with Zigbee2MQTT's **`zstack`** adapter (ADR-0003, robohub
ADR-0017). No C code runs the dongle, and nothing of RoboHub is written to it.

Most of what a coordinator needs — the extension cable, WSL, the UNO Q and the
serial group — is the same for both Sonoff dongles and is said in full on the
{ref}`ZBDongle-E page <robohub-device-sonoff-zbdongle-e/index>`; the summary
below is what differs, and enough to get the P running.

## ZBDongle-P or ZBDongle-E?

| | ZBDongle-P (this page) | ZBDongle-E |
|---|---|---|
| Name on the label / box | *Zigbee 3.0 USB Dongle Plus*, model **ZBDongle-P** | *Zigbee 3.0 USB Dongle Plus* **V2**, model **ZBDongle-E** |
| Radio | TI CC2652P | Silicon Labs EFR32MG21 |
| USB bridge and id (`lsusb`) | Silicon Labs CP210x, **`10c4:ea60`** | WCH CH9102F, **`1a86:55d4`** (some units: CP210x, `10c4:ea60`) |
| `/dev/serial/by-id/` | `usb-ITead_Sonoff_Zigbee_3.0_USB_Dongle_Plus_<serial>-if00-port0` (older units: `usb-Silicon_Labs_Sonoff_Zigbee_3.0_USB_Dongle_Plus_<serial>-if00-port0`) | `usb-ITEAD_SONOFF_Zigbee_3.0_USB_Dongle_Plus_V2_<serial>-if00` |
| Zigbee2MQTT adapter | `zstack` | `ember` |

The **model on the label** decides it: `10c4:ea60` is both a P and some E units,
so the manifest declares the adapter rather than RoboHub guessing it from the id.
A by-id name with `V2` in it is an E.

## Firmware

The P ships with a Z-Stack coordinator firmware Zigbee2MQTT's `zstack` adapter
drives, and it works as delivered. Zigbee2MQTT recommends the newest Z-Stack 3.x
coordinator firmware for the CC2652P (`CC1352P2_CC2652P_launchpad_coordinator_<date>.zip`,
from Koenkk's Z-Stack-firmware repository,
<https://github.com/Koenkk/Z-Stack-firmware>); it is flashed with Sonoff's web
flasher, <https://dongle.sonoff.tech> (choose *ZBDongle-P*), from a machine the
dongle is plugged into directly, with Zigbee2MQTT stopped.

## Plugging it in

- **A USB extension cable**, away from USB 3 ports and drives and from Wi-Fi
  antennas.
- **Under WSL**, pass it through with usbipd-win, in Windows:

  ```powershell
  winget install --interactive --exact dorssel.usbipd-win
  usbipd list
  usbipd bind --busid <id>                      # administrator PowerShell, once
  usbipd attach --wsl --busid <id> --auto-attach
  ```

  Behind usbipd the by-id name may differ; Mesh shows the ports it finds.
- **On an Arduino UNO Q**, through a **powered USB-C hub** on the board's USB-C
  port. **Host mode is not enabled when the board is powered from its 5V or VIN
  pin** until the JCTL header is set as Arduino describes in [UNO Q host
  mode](https://docs.arduino.cc/tutorials/uno-q/host-mode/).
- **The user that runs Zigbee2MQTT must be in the group that owns the serial
  device** — `dialout` on Debian and Ubuntu (`sudo usermod -aG dialout "$USER"`,
  then sign in again), `uucp` on Arch and Fedora; `ls -l /dev/ttyUSB0` shows
  which.

## In a plan

The manifest (DEVS-COORD-0001) says what Mesh needs and nothing else: kind
`coordinator`, protocol `zigbee`, chip `CC2652P`; `usb.ids` `10c4:ea60` and the
two by-id patterns above, from which Mesh picks the stable serial path; `needs`
`host: true` and `usb_host: true`, so Mesh places it only under a board that
declares both; and `zigbee.adapter: zstack`, which Mesh writes into Zigbee2MQTT's
configuration as `serial.adapter`.
