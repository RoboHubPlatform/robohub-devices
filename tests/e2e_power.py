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

NODE_BIN defaults to this repository's node build. The gateway binary is another
repository's, so it is named by the environment.
"""
import http.client as httpclient
import json
import os
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.dirname(HERE)
NODE_BIN = os.environ.get(
    "NODE_BIN", os.path.join(DEV, "nodes", "gateway-node", "build", "output", "gateway_node"))
GATEWAY_BIN = os.environ.get("GATEWAY_BIN")
HOST = "127.0.0.1"
AREA = 7
NODE = 30
OPENDTU_BODY = (
    b'{"inverters":[{"name":"HMS-800","data":{'
    b'"0":{"Power":{"v":411.0}},'
    b'"total":{"Power":{"v":823.5},"YieldDay":{"v":1.75},"YieldTotal":{"v":4321.125}}}}]}'
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


def http(port, method, path, body=None):
    conn = httpclient.HTTPConnection(HOST, port, timeout=5)
    headers = {"Content-Type": "application/json"} if body else {}
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


def main():
    if not GATEWAY_BIN or not os.path.exists(GATEWAY_BIN):
        print("set GATEWAY_BIN to the robohub-gateway binary", file=sys.stderr)
        return 2
    if not os.path.exists(NODE_BIN):
        print(f"node binary not found at {NODE_BIN}; build it with "
              f"`rho build -target native .`", file=sys.stderr)
        return 2

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
    })
    gateway = subprocess.Popen([GATEWAY_BIN], env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)

    failures = []
    if not wait_ready(rest_port):
        gateway.kill()
        print("FAIL: gateway did not become ready:\n", gateway.stdout.read())
        return 1

    node_env = dict(os.environ)
    node_env.update({
        "RH_DEVICE": "hoymiles",
        "RH_OPENDTU_HOST": HOST,
        "RH_OPENDTU_PORT": str(opendtu_port),
        "RH_NODE_LINK": "tcp",
        "RH_GATEWAY_HOST": HOST,
        "RH_GATEWAY_PORT": str(uplink_port),
        "AREA": str(AREA),
        "NODE": str(NODE),
        "INTERVAL_MS": "300",
    })
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

        print("NODES:", body)
        if not found:
            failures.append("the inverter never appeared in the device listing")
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
        print("POWER:", status, body)
        try:
            power = json.loads(body)
        except Exception:
            power = {}
        if status != 200 or abs(power.get("pv_power_w", 0) - 823.5) > 0.01:
            failures.append(f"the power endpoint is wrong: {status} {body}")
    finally:
        for proc in (node, gateway):
            proc.kill()
        opendtu.shutdown()

    if failures:
        print("\nnode output:\n", node.stdout.read())
        print("\ngateway output:\n", gateway.stdout.read())
        for f in failures:
            print("FAIL:", f)
        return 1
    print("\nOK: device -> node -> gateway end to end")
    return 0


if __name__ == "__main__":
    sys.exit(main())
