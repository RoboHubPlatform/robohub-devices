#!/usr/bin/env python3
"""End-to-end test of the device path, on the host.

The whole chain with everything except the inverter made of the real code:

    fake OpenDTU (HTTP)  ->  gateway_node (real)  ->  robohub-transport frame
                          ->  gateway uplink (real) -> device listing + REST

The inverter is simulated by a small HTTP server answering the OpenDTU status
document; the node is the built `gateway_node`; the gateway is the built
`robohub-gateway`. The node reads the document, publishes a PowerReading frame to
the gateway's uplink, and the gateway lists the inverter and serves its figures.

    GATEWAY_BIN=/path/to/robohub-gateway tests/e2e_power.py

It runs five times: with the link sealed (the node given a board id and key,
the gateway the project key they derive from) over the gateway's uplink; plain
over the uplink against a gateway that refuses plain frames, and against one that
allows them — the inverter must not appear either way, since the uplink is a
network and never takes a plain frame (gateway 0.6.0 on); and plain over the
gateway's serial link, where ALLOW_PLAIN_FRAMES=1 lets a deployment migrating its
boards hear them, and where without it they are refused. The serial link is two
pseudo-terminals this test joins, one the node's `uart0` and one the gateway's
`SERIAL_PORT`. The gateway's REST API wants a ticket; the test signs one with a
key it makes for itself.

NODE_BIN defaults to this repository's node build. The gateway binary is another
repository's, so it is named by the environment.
"""
import hashlib
import hmac
import http.client as httpclient
import json
import os
import pty
import select
import socket
import subprocess
import shutil
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.dirname(HERE)
NODE_BIN = os.environ.get(
    "NODE_BIN", os.path.join(DEV, "nodes", "gateway-node", "build", "output", "gateway_node"))
GATEWAY_BIN = os.environ.get("GATEWAY_BIN")
# What the test exits with when this machine cannot run it — no gateway binary,
# or no node build — rather than a failure: the automake convention, which ctest
# reports as a skip with the reason printed above it.
CANNOT_RUN_HERE = 77
HOST = "127.0.0.1"
AREA = 7
NODE = 30
OPENDTU_BODY = (
    b'{"inverters":[{"serial":"1161716xxxxx","name":"HMS-800","order":0,"data_age":3,'
    b'"poll_enabled":true,"reachable":true,"producing":true,"limit_relative":100,"limit_absolute":-1}],'
    b'"total":{"Power":{"v":823.5,"u":"W","d":1},"YieldDay":{"v":1750,"u":"Wh","d":0},'
    b'"YieldTotal":{"v":4321.125,"u":"kWh","d":3}},'
    b'"hints":{"time_sync":false,"radio_problem":false,"default_password":false}}'
)


class OpenDTU(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/api/livedata/status"):
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(OPENDTU_BODY)))
            self.end_headers()
            self.wfile.write(OPENDTU_BODY)
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, *args):
        pass


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind((HOST, 0))
    port = s.getsockname()[1]
    s.close()
    return port


TOKEN = ""


def http(port, method, path, body=None):
    conn = httpclient.HTTPConnection(HOST, port, timeout=5)
    headers = {"Content-Type": "application/json"} if body else {}
    if TOKEN:
        headers["Authorization"] = f"Bearer {TOKEN}"
    conn.request(method, path, body=body, headers=headers)
    resp = conn.getresponse()
    data = resp.read().decode()
    conn.close()
    return resp.status, data


def wait_ready(port, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            status, _ = http(port, "GET", "/healthz")
            if status == 200:
                return True
        except Exception:
            pass
        time.sleep(0.05)
    return False


def mint_token(home):
    """A ticket for the gateway, signed by a key made for this run only."""
    keys = os.path.join(home, ".rho", "auth")
    os.makedirs(keys, exist_ok=True)
    subprocess.run(["rho", "auth", "keygen", "--dir", keys, "--force"], check=True, capture_output=True, text=True)
    return subprocess.run(["rho", "auth", "ticket", "--private", os.path.join(keys, "private.pem"), "--issuer", "keeper",
                           "--audience", "gateway", "--subject", "e2e", "--scopes", "*", "--ttl", "1h"],
                          check=True, capture_output=True, text=True).stdout.strip()


# @verifies DEVS-0004, DEVS-0006, DEVS-NODE-0004, DEVS-NODE-0005, DEVS-INV-0003
def main():
    global TOKEN
    if not GATEWAY_BIN or not os.path.exists(GATEWAY_BIN):
        print("skipped: set GATEWAY_BIN to the robohub-gateway binary", file=sys.stderr)
        return CANNOT_RUN_HERE
    if not os.path.exists(NODE_BIN):
        print(f"skipped: node binary not found at {NODE_BIN}; build it with "
              f"`rho build -target native .`", file=sys.stderr)
        return CANNOT_RUN_HERE
    work = tempfile.mkdtemp(prefix="e2e-power-")
    try:
        TOKEN = mint_token(work)
        k_gw = os.urandom(32)
        key_file = os.path.join(work, "gateway.key")
        with open(key_file, "w") as f:
            f.write(k_gw.hex() + "\n")
        os.chmod(key_file, 0o600)
        # The board's key is derived from the project's, as Mesh derives it.
        uid = os.urandom(8)
        device_key = hmac.new(k_gw, b"rh-dev1" + uid, hashlib.sha256).digest()[:16]
        device_key_file = os.path.join(work, "device.key")
        with open(device_key_file, "w") as f:
            f.write(device_key.hex() + "\n")
        os.chmod(device_key_file, 0o600)
        sealed = {"RH_SECURE_UID": uid.hex(), "RH_SECURE_KEY_FILE": device_key_file}
        failures = []
        failures += run("sealed", work, key_file, sealed, {}, expect_listed=True)
        failures += run("plain on the uplink, refused", work, key_file, {}, {}, expect_listed=False)
        failures += run("plain on the uplink, refused even when allowed", work, key_file, {},
                        {"ALLOW_PLAIN_FRAMES": "1"}, expect_listed=False)
        failures += run("plain on the serial link, refused", work, key_file, {}, {}, expect_listed=False,
                        link="uart")
        failures += run("plain on the serial link, allowed", work, key_file, {}, {"ALLOW_PLAIN_FRAMES": "1"},
                        expect_listed=True, link="uart")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if failures:
        for failure in failures:
            print("FAIL:", failure)
        return 1
    print("e2e power: sealed, refused and allowed (serial only) all as expected")
    return 0


class SerialBridge:
    """Two pseudo-terminals joined end to end: the node opens one slave as its
    uart0, the gateway the other as its SERIAL_PORT, and what either writes the
    other reads."""

    def __init__(self):
        self.a_master, a_slave = pty.openpty()
        self.b_master, b_slave = pty.openpty()
        self.node_path = os.ttyname(a_slave)
        self.gateway_path = os.ttyname(b_slave)
        self.running = True
        self.thread = threading.Thread(target=self.pump, daemon=True)
        self.thread.start()

    def pump(self):
        pairs = {self.a_master: self.b_master, self.b_master: self.a_master}
        while self.running:
            ready, _, _ = select.select(list(pairs), [], [], 0.1)
            for fd in ready:
                try:
                    data = os.read(fd, 4096)
                except OSError:
                    continue
                if data:
                    try:
                        os.write(pairs[fd], data)
                    except OSError:
                        pass

    def close(self):
        self.running = False
        self.thread.join(timeout=2)


def run(label, home, key_file, node_extra, gateway_extra, expect_listed, link="tcp"):
    # The fake OpenDTU.
    opendtu = HTTPServer((HOST, 0), OpenDTU)
    opendtu_port = opendtu.server_address[1]
    threading.Thread(target=opendtu.serve_forever, daemon=True).start()

    rest_port = free_port()
    uplink_port = free_port()

    env = dict(os.environ)
    env.update({
        "REST_PORT": str(rest_port),
        "UPLINK_PORT": str(uplink_port),
        "GATEWAY_MODE": "master",
        "GATEWAY_AREA": str(AREA),
        "HOME": home,
        "GATEWAY_KEY_FILE": key_file,
    })
    env.pop("ALLOW_PLAIN_FRAMES", None)
    env.update(gateway_extra)
    bridge = None
    if link == "uart":
        bridge = SerialBridge()
        env["SERIAL_PORT"] = bridge.gateway_path
    gateway = subprocess.Popen([GATEWAY_BIN], env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)

    failures = []
    if not wait_ready(rest_port):
        gateway.kill()
        opendtu.shutdown()
        if bridge is not None:
            bridge.close()
        return [f"{label}: the gateway did not become ready:\n" + gateway.stdout.read()]

    node_env = dict(os.environ)
    node_env.update({
        "RH_DEVICE": "hoymiles",
        "RH_OPENDTU_HOST": HOST,
        "RH_OPENDTU_PORT": str(opendtu_port),
        "RH_NODE_LINK": link,
        "RH_GATEWAY_HOST": HOST,
        "RH_GATEWAY_PORT": str(uplink_port),
        "AREA": str(AREA),
        "NODE": str(NODE),
        "INTERVAL_MS": "300",
    })
    for name in ("RH_SECURE_UID", "RH_SECURE_KEY_FILE"):
        node_env.pop(name, None)
    if bridge is not None:
        node_env["RHO_UART0"] = bridge.node_path
    node_env.update(node_extra)
    node = subprocess.Popen([NODE_BIN], env=node_env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)

    try:
        # Give the node a moment to read and publish, then drain the uplink. The
        # gateway applies queued uplink frames when it is asked for something.
        found = None
        for _ in range(20):
            time.sleep(0.25)
            http(rest_port, "POST", "/api/v1/uplink/pump")
            status, body = http(rest_port, "GET", "/api/v1/nodes")
            try:
                listed = {(n["area_id"], n["node_id"]): n for n in json.loads(body)}
            except Exception:
                listed = {}
            found = listed.get((AREA, NODE))
            if found:
                break

        if not expect_listed:
            if found:
                failures.append(f"{label}: the gateway listed a node it should have refused")
            else:
                print(f"ok: {label}")
            return failures
        if not found:
            failures.append(f"{label}: the inverter never appeared in the device listing")
        else:
            if abs(found.get("pv_power_w", 0) - 823.5) > 0.01:
                failures.append(f"pv_power_w wrong: {found.get('pv_power_w')}")
            if found.get("kind") != 1:
                failures.append(f"kind should be inverter (1): {found.get('kind')}")
            valid = found.get("valid", 0)
            if not (valid & 1):   # PV power
                failures.append(f"PV power not marked valid: {valid}")
            if valid & 16:        # battery SOC
                failures.append(f"a microinverter should have no battery: {valid}")

        status, body = http(rest_port, "GET", "/api/v1/telemetry/power")
        try:
            power = json.loads(body)
        except Exception:
            power = {}
        if status != 200 or abs(power.get("pv_power_w", 0) - 823.5) > 0.01:
            failures.append(f"{label}: the power endpoint is wrong: {status} {body}")
    finally:
        for proc in (node, gateway):
            proc.kill()
        opendtu.shutdown()
        if bridge is not None:
            bridge.close()

    if failures:
        print(f"\n[{label}] node output:\n", node.stdout.read())
        print(f"\n[{label}] gateway output:\n", gateway.stdout.read()[-3000:])
    else:
        print(f"ok: {label}")
    return failures


if __name__ == "__main__":
    sys.exit(main())
