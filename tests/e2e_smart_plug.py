#!/usr/bin/env python3
"""End-to-end test of the smart plug, on the host.

The whole composition and the whole control path, with only the sensor and the
relay simulated:

    fake 1-Wire (DS18B20) + fake GPIO  ->  smart_plug (real)  ->  device-node
    runtime (real)  ->  robohub-transport frame  ->  gateway (real, over a tty)

The node program is `tests/smart-plug-node`, built native: the real composition,
relay and DS18B20 driver over fakes. It publishes a temperature and the switch's
state to the gateway, and answers a switch command from the gateway — which is
the point of the test: a command goes down the serial link, the runtime feeds it,
the composition drives the relay, and the state comes back.

Two ptys are bridged in this process, one end for the gateway and one for the
node, because a host has no other way to give two programs a serial cable.

    GATEWAY_BIN=/path/to/robohub-gateway tests/e2e_smart_plug.py
"""
import http.client as httpclient
import json
import os
import pty
import select
import socket
import subprocess
import sys
import threading
import time
import tty

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.dirname(HERE)
NODE_BIN = os.environ.get(
    "NODE_BIN", os.path.join(DEV, "tests", "smart-plug-node", "build", "output", "smart_plug_node"))
GATEWAY_BIN = os.environ.get("GATEWAY_BIN")
HOST = "127.0.0.1"
AREA = 7
NODE = 20


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
            if http(port, "GET", "/healthz")[0] == 200:
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
              f"`rho build -target native .` in tests/smart-plug-node", file=sys.stderr)
        return 2

    # Two pty pairs, bridged: one end for each program, so they have a cable.
    m_gw, s_gw = pty.openpty()
    m_node, s_node = pty.openpty()
    for fd in (m_gw, s_gw, m_node, s_node):
        tty.setraw(fd)
    gw_path = os.ttyname(s_gw)
    node_path = os.ttyname(s_node)

    stop = threading.Event()

    def bridge():
        fds = [m_gw, m_node]
        while not stop.is_set():
            try:
                ready, _, _ = select.select(fds, [], [], 0.1)
            except OSError:
                return
            for fd in ready:
                try:
                    data = os.read(fd, 4096)
                except OSError:
                    continue
                if data:
                    os.write(m_node if fd == m_gw else m_gw, data)

    threading.Thread(target=bridge, daemon=True).start()

    rest_port = free_port()
    env = dict(os.environ)
    env.update({
        "SERIAL_PORT": gw_path,
        "SERIAL_BAUD": "115200",
        "REST_PORT": str(rest_port),
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
        "RHO_UART0": node_path,
        "RH_NODE_UART": "uart0",
        "AREA": str(AREA),
        "NODE": str(NODE),
    })
    node = subprocess.Popen([NODE_BIN], env=node_env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)

    try:
        # 1. The device appears, reporting its temperature (the simulated 25.0 C)
        # and the switch's state (off).
        found = None
        body = "[]"
        for _ in range(40):
            time.sleep(0.25)
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
            failures.append("the smart plug never appeared in the device listing")
        else:
            if abs(found.get("temperature_c", 0) - 25.0) > 0.01:
                failures.append(f"temperature wrong: {found.get('temperature_c')}")
            if found.get("switch_state", -1) != 0:
                failures.append(f"switch should start off: {found.get('switch_state')}")
            if found.get("kind") != 6:  # SWITCH
                failures.append(f"kind should be switch (6): {found.get('kind')}")

        # 2. A command from the gateway goes down the serial link, the runtime
        # feeds it, the composition drives the relay, and the state comes back.
        status, body = http(rest_port, "POST", f"/api/v1/nodes/{AREA}/{NODE}/switch",
                            json.dumps({"on": True}))
        print("SWITCH:", status, body)
        try:
            switch = json.loads(body)
        except Exception:
            switch = {}
        if status != 200 or switch.get("actual") != 1:
            failures.append(f"switch command failed: {status} {body}")

        # 3. The next poll reports the new state, so the listing shows it on.
        state = None
        for _ in range(20):
            time.sleep(0.3)
            status, body = http(rest_port, "GET", "/api/v1/nodes")
            try:
                listed = {(n["area_id"], n["node_id"]): n for n in json.loads(body)}
            except Exception:
                listed = {}
            state = listed.get((AREA, NODE))
            if state and state.get("switch_state") == 1:
                break
        print("NODES after switch:", body)
        if not state or state.get("switch_state") != 1:
            failures.append("the switch state did not become on")
    finally:
        for proc in (node, gateway):
            proc.kill()
        stop.set()
        for fd in (m_gw, s_gw, m_node, s_node):
            try:
                os.close(fd)
            except OSError:
                pass

    if failures:
        print("\nnode output:\n", node.stdout.read())
        print("\ngateway output:\n", gateway.stdout.read())
        for f in failures:
            print("FAIL:", f)
        return 1
    print("\nOK: smart plug -> node -> gateway, command and state end to end")
    return 0


if __name__ == "__main__":
    sys.exit(main())
