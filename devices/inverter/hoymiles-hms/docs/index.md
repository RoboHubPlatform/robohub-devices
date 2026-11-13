# Hoymiles microinverters through OpenDTU

Hoymiles HM and HMS microinverters (an HMS-800W and its kind) speak a 2.4 GHz
radio protocol of their own; an [OpenDTU](https://www.opendtu.solar) — an ESP32
with an NRF24 or CMT2300A radio — talks to them and answers over Wi-Fi. RoboHub
reads the OpenDTU, not the inverters.

## What is read

One `GET /api/livedata/status`, the `total` of every inverter the OpenDTU
polls — two 800 W microinverters are one figure:

| Signal | JSON path | Unit sent | Reading |
|---|---|---|---|
| `pv_power` | `total/Power/v` | W | W |
| `energy_today` | `total/YieldDay/v` | Wh | kWh (scale 0.001) |
| `energy_total` | `total/YieldTotal/v` | kWh | kWh |

The C driver reads the unit each figure carries (`"u"`) and scales by it; the
manifest's field map, which Mesh polls, has the scales written in.

## Setting it up

1. Flash OpenDTU (or OpenDTU-OnBattery) to an ESP32 with a CMT2300A radio for
   HMS inverters, join it to the site's Wi-Fi and add each inverter by its
   serial number.
2. Leave read-only web API access on (the default): the live data is then read
   without a password.
3. In Mesh, add the Hoymiles HMS microinverter device with the OpenDTU's
   address; its `pv_power`, `energy_today` and `energy_total` appear as sources
   to bind to energy's microinverter equipment.
