#!/bin/sh
# Build and run the host tests for robohub-devices.
#
# The runner is the ecosystem's, in the orchestrator; this script says where it
# is and what it is run over. The sibling checkouts it builds against —
# robohub-common for the interfaces and the HAL, robohub-linux for a real
# socket and a real clock — are named by the environment, because a path into
# another repository belongs to the machine and not to this one.
#
#     ROBOHUB_COMMON_DIR=/path/to/robohub-common \
#     ROBOHUB_LINUX_DIR=/path/to/robohub-linux \
#     ROBOHUB_ORCHESTRATOR_DIR=/path/to/robohub \
#         tests/run.sh
set -eu

: "${ROBOHUB_COMMON_DIR:?set ROBOHUB_COMMON_DIR to the robohub-common checkout}"
: "${ROBOHUB_LINUX_DIR:?set ROBOHUB_LINUX_DIR to the robohub-linux checkout}"
: "${ROBOHUB_ORCHESTRATOR_DIR:?set ROBOHUB_ORCHESTRATOR_DIR to the robohub checkout}"

HERE=$(cd "$(dirname "$0")" && pwd)
exec "$ROBOHUB_ORCHESTRATOR_DIR/shared/tests/run-host-tests.sh" "$(dirname "$HERE")"
