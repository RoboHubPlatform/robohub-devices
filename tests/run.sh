#!/bin/sh
# Build and run the host tests for robohub-devices.
#
# The code under test is in this repository; the sibling checkouts it builds
# against — robohub-common for the interfaces and the HAL, robohub-linux for a
# real socket and a real clock — are named by the environment, because a path
# into another repository belongs to the machine and not to this one.
#
#     ROBOHUB_COMMON_DIR=/path/to/robohub-common \
#     ROBOHUB_LINUX_DIR=/path/to/robohub-linux \
#         tests/run.sh
set -eu

: "${ROBOHUB_COMMON_DIR:?set ROBOHUB_COMMON_DIR to the robohub-common checkout}"
: "${ROBOHUB_LINUX_DIR:?set ROBOHUB_LINUX_DIR to the robohub-linux checkout}"

HERE=$(cd "$(dirname "$0")" && pwd)
DEV=$(dirname "$HERE")
OUT="${TMPDIR:-/tmp}/robohub-devices-tests"
mkdir -p "$OUT"

CC=${CC:-cc}
BASE="-I$ROBOHUB_COMMON_DIR/include -I$ROBOHUB_COMMON_DIR/include/robohub \
      -I$ROBOHUB_COMMON_DIR/include/robohub/hal -I$ROBOHUB_COMMON_DIR/include/robohub/drivers \
      -I$ROBOHUB_COMMON_DIR/include/robohub/devices"

# The generated namespace a build produces for cross-module includes
# (robohub/protocols/*, robohub/devices/inverter/*). A build makes these copies;
# the test does the same so the sources compile as they are written.
GEN="$OUT/include"
mkdir -p "$GEN/robohub/protocols" "$GEN/robohub/devices/inverter"
cp "$DEV/protocols/modbus/inc/modbus.h"          "$GEN/robohub/protocols/modbus.h"
cp "$DEV/protocols/http/inc/http_client.h"       "$GEN/robohub/protocols/http_client.h"
cp "$DEV/protocols/json/inc/json.h"              "$GEN/robohub/protocols/json.h"
cp "$DEV/devices/inverter/growatt-es/inc/growatt.h"  "$GEN/robohub/devices/inverter/growatt.h"
cp "$DEV/devices/inverter/hoymiles-hms/inc/hoymiles.h" "$GEN/robohub/devices/inverter/hoymiles.h"

PROTO="$DEV/nodes/gateway-node/src/protocol"

run() { printf '\n== %s ==\n' "$1"; shift; "$@"; }

$CC -std=c11 -Wall -Wextra -I"$DEV/protocols/modbus/inc" $BASE \
    "$DEV/tests/test_modbus.c" "$DEV/protocols/modbus/src/modbus.c" -o "$OUT/test_modbus"
$CC -std=c11 -Wall -Wextra -I"$GEN" -I"$DEV/protocols/json/inc" $BASE \
    "$DEV/tests/test_json.c" "$DEV/protocols/json/src/json.c" -o "$OUT/test_json"
$CC -std=c11 -Wall -Wextra -I"$GEN" -I"$DEV/protocols/modbus/inc" \
    -I"$DEV/devices/inverter/growatt-es/inc" -I"$PROTO" $BASE \
    "$DEV/tests/test_growatt.c" "$DEV/protocols/modbus/src/modbus.c" \
    "$DEV/devices/inverter/growatt-es/src/growatt.c" "$PROTO/protocol.c" -o "$OUT/test_growatt"
$CC -std=gnu11 -Wall -Wextra -I"$GEN" -I"$DEV/protocols/http/inc" \
    -I"$DEV/protocols/json/inc" -I"$DEV/devices/inverter/hoymiles-hms/inc" $BASE \
    "$DEV/tests/test_hoymiles.c" "$DEV/protocols/http/src/http_client.c" \
    "$DEV/protocols/json/src/json.c" "$DEV/devices/inverter/hoymiles-hms/src/hoymiles.c" \
    "$ROBOHUB_LINUX_DIR/src/linux_net.c" "$ROBOHUB_COMMON_DIR/src/platform/net.c" \
    "$ROBOHUB_COMMON_DIR/src/error.c" -lpthread -o "$OUT/test_hoymiles"

run test_modbus "$OUT/test_modbus"
run test_json "$OUT/test_json"
run test_growatt "$OUT/test_growatt"
run test_hoymiles "$OUT/test_hoymiles"

printf '\nall device tests passed\n'
