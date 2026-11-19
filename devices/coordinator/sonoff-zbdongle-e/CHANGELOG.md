# Changelog

All notable changes to this module are recorded here, one section per release.

## [0.1.0] - 2026-11-20

The Sonoff Zigbee 3.0 USB Dongle Plus V2 (ZBDongle-E, EFR32MG21), declared as a
coordinator for Mesh (ADR-0003, DEVS-COORD-0002): USB ids `1a86:55d4` and
`10c4:ea60`, its two `/dev/serial/by-id/` patterns, `needs` host and USB host,
Zigbee2MQTT's `ember` adapter and EmberZNet 7.4.4 as the oldest firmware. No C
code. The page tells the E from the P, says where to get the firmware and how to
flash it, and how to reach the dongle on an extension cable, from WSL (usbipd)
and from a UNO Q (a powered hub; host mode when powered from 5V/VIN), and with
the serial device's group (DEVS-COORD-0003).
