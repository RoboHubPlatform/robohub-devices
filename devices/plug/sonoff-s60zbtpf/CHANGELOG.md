# Changelog

All notable changes to this module are recorded here, one section per release.

## [0.1.0] - 2026-11-20

The Sonoff S60ZBTPF Zigbee plug, declared as data for Mesh (ADR-0003,
DEVS-PLUG-0012): `transports: [zigbee]`, `firmware: stock`, a `zigbee` block
(model S60ZBTPF, vendor SONOFF) Mesh matches a joined device by, the signals of
the S60TPF, one status topic `{prefix}` with a field line per signal, the online
state read from `state` of `{prefix}/availability` (`online_path`), and the switch
as `{"state":"{ONOFF}"}` on `{prefix}/set`. No C driver. The page says how to pair
it and what it reports: voltage read once at pairing, power and current 0 while
off, no reflash and no eWeLink account (DEVS-PLUG-0014).
