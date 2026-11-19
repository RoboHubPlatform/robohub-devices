#!/usr/bin/env python3
"""
check_coordinators.py — every coordinator manifest says what Mesh needs to place
and drive it, and every Zigbee device names what Zigbee2MQTT reports for it.

Mesh refuses or misplaces a coordinator from these lines alone (ADR-0003), so a
typo must fail here, not on a person's machine. For every module.yml under
devices/ (and every path the collection lists under `devices:`):

  kind coordinator  -> device.protocol; usb.ids, at least one, each vvvv:pppp in
                       lower-case hex; usb.by_id, at least one pattern; needs.host
                       and needs.usb_host true; zigbee.adapter one Zigbee2MQTT
                       names (zstack, ember, deconz, zigate, zboss).
  a zigbee block    -> a coordinator's names its adapter (above); any other
                       manifest's names zigbee.model and zigbee.vendor.

The manifests are read with a small reader for the block shapes they use (a
mapping one level deep, a list as `[a, b]` or `- item` lines), so the check
needs nothing beyond python3. Before the real manifests it checks itself
against broken ones, so a check that stopped catching anything fails too.

Prints a [PASS]/[FAIL] line per check and exits non-zero on any failure.

@project RoboHub Platform
@license MIT
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
USB_ID = re.compile(r"^[0-9a-f]{4}:[0-9a-f]{4}$")
ADAPTERS = ("zstack", "ember", "deconz", "zigate", "zboss")


def unquote(value):
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    return value


def uncomment(line):
    """The line without a comment: `#` at the start or after a space, outside quotes."""
    quote = ""
    for i, ch in enumerate(line):
        if quote:
            if ch == quote:
                quote = ""
        elif ch in "\"'":
            quote = ch
        elif ch == "#" and (i == 0 or line[i - 1] in " \t"):
            return line[:i].rstrip()
    return line.rstrip()


def blocks(text):
    """Top-level key -> {child key -> scalar string or list of strings}."""
    out = {}
    current = None
    child = None
    for raw in text.splitlines():
        line = uncomment(raw)
        if not line.strip():
            continue
        if not line.startswith((" ", "\t")):
            key = line.split(":", 1)[0].strip()
            current = out.setdefault(key, {})
            child = None
            continue
        if current is None:
            continue
        stripped = line.strip()
        indent = len(line) - len(line.lstrip())
        if stripped.startswith("- ") and child is not None and isinstance(current.get(child), list):
            current[child].append(unquote(stripped[2:]))
            continue
        if indent == 2 and ":" in stripped:
            key, value = stripped.split(":", 1)
            key = key.strip()
            value = value.strip()
            child = key
            if value == "":
                current[key] = []
            elif value.startswith("[") and value.endswith("]"):
                current[key] = [unquote(v) for v in value[1:-1].split(",") if v.strip()]
            else:
                current[key] = unquote(value)
    return out


# @verifies DEVS-COORD-0004, DEVS-0011
def problems_of(text):
    """What is wrong with one manifest, as sentences; [] when nothing is."""
    b = blocks(text)
    device = b.get("device", {})
    zigbee = b.get("zigbee")
    found = []
    if device.get("kind") == "coordinator":
        if not device.get("protocol"):
            found.append("device.protocol is not declared")
        usb = b.get("usb", {})
        ids = usb.get("ids")
        if not isinstance(ids, list) or not ids:
            found.append("usb.ids names no USB id")
        else:
            for one in ids:
                if not USB_ID.match(one):
                    found.append("usb id '%s' is not vvvv:pppp in lower-case hex" % one)
        by_id = usb.get("by_id")
        if not isinstance(by_id, list) or not [p for p in by_id if p]:
            found.append("usb.by_id names no /dev/serial/by-id/ pattern")
        needs = b.get("needs", {})
        for need in ("host", "usb_host"):
            if needs.get(need) != "true":
                found.append("needs.%s is not true" % need)
        adapter = (zigbee or {}).get("adapter", "")
        if adapter not in ADAPTERS:
            found.append("zigbee.adapter '%s' is not one Zigbee2MQTT names (%s)" % (adapter, ", ".join(ADAPTERS)))
    elif zigbee is not None:
        for key in ("model", "vendor"):
            if not zigbee.get(key):
                found.append("the zigbee block names no %s" % key)
    return found


GOOD = """device:
  kind: coordinator
  protocol: zigbee
  transports: []
usb:
  ids: ["10c4:ea60"]
  by_id:
    - "usb-x_*-if00-port0"
needs:
  host: true
  usb_host: true
zigbee:
  adapter: zstack
"""

BROKEN = [
    ("no protocol", GOOD.replace("  protocol: zigbee\n", "")),
    ("an id in upper case", GOOD.replace("10c4:ea60", "10C4:EA60")),
    ("an id that is not vvvv:pppp", GOOD.replace("10c4:ea60", "10c4-ea60")),
    ("no usb ids", GOOD.replace('["10c4:ea60"]', "[]")),
    ("no by-id pattern", GOOD.replace('  by_id:\n    - "usb-x_*-if00-port0"\n', "  by_id: []\n")),
    ("needs.usb_host false", GOOD.replace("usb_host: true", "usb_host: false")),
    ("needs.host missing", GOOD.replace("  host: true\n", "")),
    ("an adapter Zigbee2MQTT does not name", GOOD.replace("adapter: zstack", "adapter: ezsp2")),
    ("no zigbee block", GOOD.replace("zigbee:\n  adapter: zstack\n", "")),
    ("a Zigbee device without a vendor",
     "device:\n  kind: plug\n  transports: [zigbee]\nzigbee:\n  model: S60ZBTPF\n"),
]


def listed_devices():
    """The paths the collection lists under `devices:`, a top-level list, which
    blocks() does not keep."""
    paths = []
    with open(os.path.join(ROOT, "module.yml"), encoding="utf-8") as f:
        inside = False
        for raw in f:
            line = uncomment(raw.rstrip("\n"))
            if not line.strip():
                continue
            if not line.startswith((" ", "\t", "-")):
                inside = line.strip() == "devices:"
                continue
            if inside and line.strip().startswith("- "):
                paths.append(unquote(line.strip()[2:]))
    return paths


def manifests():
    """Every module.yml under devices/, and every listed device's."""
    found = set()
    for base, dirs, files in os.walk(os.path.join(ROOT, "devices")):
        dirs[:] = [d for d in dirs if d not in ("build", ".git")]
        if "module.yml" in files:
            found.add(os.path.join(base, "module.yml"))
    for path in listed_devices():
        manifest = os.path.join(ROOT, path, "module.yml")
        if os.path.exists(manifest):
            found.add(manifest)
    return sorted(found)


def main():
    failures = 0

    def check(ok, what):
        nonlocal failures
        print("[PASS] %s" % what if ok else "[FAIL] %s" % what)
        if not ok:
            failures += 1

    check(problems_of(GOOD) == [], "a coordinator manifest that says everything passes")
    for what, text in BROKEN:
        check(problems_of(text) != [], "the check refuses a manifest with %s" % what)

    coordinators = 0
    zigbee_devices = 0
    for manifest in manifests():
        rel = os.path.relpath(manifest, ROOT)
        with open(manifest, encoding="utf-8") as f:
            text = f.read()
        b = blocks(text)
        if b.get("device", {}).get("kind") == "coordinator":
            coordinators += 1
        elif "zigbee" in b:
            zigbee_devices += 1
        else:
            continue
        found = problems_of(text)
        for problem in found:
            print("  %s: %s" % (rel, problem))
        check(found == [], "%s says what Mesh needs" % rel)

    check(coordinators >= 2, "the coordinators are checked (%d found)" % coordinators)
    check(zigbee_devices >= 1, "the Zigbee devices are checked (%d found)" % zigbee_devices)
    listed = listed_devices()
    for path in ("devices/coordinator/sonoff-zbdongle-e", "devices/coordinator/sonoff-zbdongle-p",
                 "devices/plug/sonoff-s60zbtpf"):
        check(path in listed, "%s is listed under devices:" % path)
    if failures:
        print("%d check(s) failed" % failures)
        return 1
    print("every coordinator and Zigbee device manifest says what Mesh needs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
