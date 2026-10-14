#!/usr/bin/env python3
"""End-to-end test of the Growatt path, on the host.

    Modbus slave (pty)  ->  gateway_node (real, RS485 adapter + Modbus + Growatt)
                        ->  robohub-transport frame  ->  gateway (real)  ->  REST

The inverter is simulated by a Modbus RTU slave answering on one end of a pty;
the node opens the other end as its RS485 UART (no direction pin: the bus is
automatic), reads the input registers, publishes a PowerReading frame to the
gateway's uplink, and the gateway lists the inverter.

The slave answers the **default** Growatt map, so this also checks that the
default map and the decode agree with a device that answers those addresses.

    GATEWAY_BIN=/path/to/robohub-gateway tests/e2e_growatt.py
"""
import http.client as httpclient
import json
import os
import pty
import socket
import struct
import subprocess
import sys
import threading
import time
import tty

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.dirname(HERE)
NODE_BIN = os.environ.get(
    "NODE_BIN", os.path.join(DEV, "nodes", "gateway-node", "build", "output", "gateway_node"))
GATEWAY_BIN = os.environ.get("GATEWAY_BIN")
HOST = "127.0.0.1"
AREA = 7
NODE = 31

# The default map: base 0, 48 registers, pv at 3 (100 W/count), load at 7,
# grid at 9 (signed), battery at 11 (signed), soc at 13, energy at 26/28 (0.1).
REG_BLOCK = [0] * 48
REG_BLOCK[3] = 12        # pv 1200 W
REG_BLOCK[7] = 8         # load 800 W
REG_BLOCK[9] = 0xFFFB    # grid -500 W
REG_BLOCK[11] = 5        # battery +500 W
REG_BLOCK[13] = 85       # soc 85 %
REG_BLOCK[26] = 42       # today 4.2 kWh
REG_BLOCK[28] = 12345    # total 1234.5 kWh


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def slave(master_fd, stop):
    buf = b""
    while not stop.is_set():
        try:
            chunk = os.read(master_fd, 256)
        except OSError:
            break
        if not chunk:
            continue
        buf += chunk
        while len(buf) >= 8:
            request, buf = buf[:8], buf[8:]
            if crc16(request[:6]) != (request[6] | (request[7] << 8)):
                continue
            fc = request[1]
            count = (request[4] << 8) | request[5]
            if fc != 4 or count == 0 or count > len(REG_BLOCK):
                continue
            regs = REG_BLOCK[:count]
            body = bytes([request[0], fc, count * 2]) + b"".join(struct.pack(">H", r) for r in regs)
            os.write(master_fd, body + struct.pack("<H", crc16(body)))


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
        print(f"node binary not found at {NODE_BIN}", file=sys.stderr)
        return 2

    master_fd, slave_fd = pty.openpty()
    tty.setraw(master_fd)
    tty.setraw(slave_fd)
    slave_path = os.ttyname(slave_fd)
    stop = threading.Event()
    threading.Thread(target=slave, args=(master_fd, stop), daemon=True).start()

    rest_port = free_port()
    uplink_port = free_port()

    env = dict(os.environ)
    env.update({"REST_PORT": str(rest_port), "UPLINK_PORT": str(uplink_port),
                "GATEWAY_MODE": "master", "GATEWAY_AREA": str(AREA)})
    gateway = subprocess.Popen([GATEWAY_BIN], env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    failures = []
    if not wait_ready(rest_port):
        gateway.kill()
        print("FAIL: gateway did not become ready:\n", gateway.stdout.read())
        return 1

    node_env = dict(os.environ)
    node_env.update({
        "RH_DEVICE": "growatt",
        "RH_RS485_UART": "uart1",
        "RHO_UART1": slave_path,
        "RH_RS485_DE_GPIO": "",       # automatic direction
        "RH_RS485_BAUD": "9600",
        "RH_MODBUS_SLAVE": "1",
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
        found = None
        body = "[]"
        for _ in range(30):
            time.sleep(0.25)
            http(rest_port, "POST", "/api/v1/uplink/pump")
            status, body = http(rest_port, "GET", "/api/v1/nodes")
            try:
                listed = {(n["area_id"], n["node_id"]): n for n in json.loads(body)}
            except Exception:
                listed = {}
            found = listed.get((AREA, NODE))
            if found and found.get("pv_power_w"):
                break

        print("NODES:", body)
        if not found:
            failures.append("the inverter never appeared in the device listing")
        else:
            checks = [
                ("pv_power_w", 1200.0),
                ("load_power_w", 800.0),
                ("grid_power_w", -500.0),
                ("battery_power_w", 500.0),
                ("battery_soc", 85.0),
                ("energy_today_kwh", 4.2),
                ("energy_total_kwh", 1234.5),
            ]
            for key, want in checks:
                if abs(found.get(key, 0) - want) > 0.01:
                    failures.append(f"{key}: got {found.get(key)}, want {want}")
            if found.get("valid") != 127:
                failures.append(f"valid should be 127 (all figures), got {found.get('valid')}")
            if found.get("kind") != 1:
                failures.append(f"kind should be inverter (1), got {found.get('kind')}")
    finally:
        for proc in (node, gateway):
            proc.kill()
        stop.set()
        os.close(master_fd)
        os.close(slave_fd)

    if failures:
        print("\nnode output:\n", node.stdout.read())
        print("\ngateway output:\n", gateway.stdout.read())
        for f in failures:
            print("FAIL:", f)
        return 1
    print("\nOK: Growatt -> node -> gateway end to end")
    return 0


if __name__ == "__main__":
    sys.exit(main())
