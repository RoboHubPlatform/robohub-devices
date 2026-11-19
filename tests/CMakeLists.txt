#
# robohub-devices' tests, registered with CTest so that `rho test` runs them
# (tests/tests.yml declares this directory as a ctest suite).
#
# This is a test project and nothing else: the collection itself is not built
# by CMake, and nothing here compiles the devices. It registers:
#
#   host-tests      tests/run.sh: the host tests in tests/host-tests.plan, built
#                   and run by the ecosystem's runner in robohub
#                   (shared/tests/run-host-tests.sh) against the sibling
#                   checkouts of robohub-common and robohub-linux.
#   test_sonoff_*   the Sonoff S60TPF's tests, one case each, run by name from
#                   the binary host-tests built.
#   e2e-*           the end-to-end scripts, which run a real gateway and a real
#                   node. They need GATEWAY_BIN (another repository's binary)
#                   and this repository's node build, and exit 77 when either is
#                   absent, which CTest reports as skipped (SKIP_RETURN_CODE)
#                   with the reason the script printed, never as a pass.
#
# ROBOHUB_DEVICES_TESTS says which of the two a configuration registers — `host`,
# `e2e`, or `all` (the default) — so tests.yml can declare them as two suites.
#
# The sibling checkouts are found under ROBOHUB_WORKSPACE, the directory the
# manifests name them under, or beside this repository when it is unset.
#
# @project RoboHub Platform
# @license MIT
#

cmake_minimum_required(VERSION 3.16)

project(robohub-devices-tests NONE)

enable_testing()

get_filename_component(ROBOHUB_DEVICES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

set(ROBOHUB_DEVICES_TESTS "all" CACHE STRING "Which tests to register: host, e2e or all")
set_property(CACHE ROBOHUB_DEVICES_TESTS PROPERTY STRINGS host e2e all)
if(NOT ROBOHUB_DEVICES_TESTS MATCHES "^(host|e2e|all)$")
    message(FATAL_ERROR "ROBOHUB_DEVICES_TESTS is host, e2e or all, not '${ROBOHUB_DEVICES_TESTS}'")
endif()

if(DEFINED ENV{ROBOHUB_WORKSPACE} AND NOT "$ENV{ROBOHUB_WORKSPACE}" STREQUAL "")
    set(ROBOHUB_WORKSPACE_DIR "$ENV{ROBOHUB_WORKSPACE}")
else()
    get_filename_component(ROBOHUB_WORKSPACE_DIR "${ROBOHUB_DEVICES_ROOT}" DIRECTORY)
endif()

find_program(ROBOHUB_SH sh REQUIRED)

if(ROBOHUB_DEVICES_TESTS STREQUAL "host" OR ROBOHUB_DEVICES_TESTS STREQUAL "all")
foreach(sibling robohub robohub-common robohub-linux)
    if(NOT EXISTS "${ROBOHUB_WORKSPACE_DIR}/${sibling}")
        message(FATAL_ERROR
            "robohub-devices' host tests build against ${sibling}, which is not at "
            "${ROBOHUB_WORKSPACE_DIR}/${sibling}.\n"
            "  Check it out beside this repository, or set ROBOHUB_WORKSPACE to the "
            "directory the checkouts are in.")
    endif()
endforeach()

# The host tests. Their build directory is this one, so a run leaves nothing in
# the system's temporary directory.
add_test(NAME host-tests COMMAND "${ROBOHUB_SH}" "${ROBOHUB_DEVICES_ROOT}/tests/run.sh")
set_tests_properties(host-tests PROPERTIES ENVIRONMENT
    "ROBOHUB_COMMON_DIR=${ROBOHUB_WORKSPACE_DIR}/robohub-common;ROBOHUB_LINUX_DIR=${ROBOHUB_WORKSPACE_DIR}/robohub-linux;ROBOHUB_ORCHESTRATOR_DIR=${ROBOHUB_WORKSPACE_DIR}/robohub;TMPDIR=${CMAKE_CURRENT_BINARY_DIR}"
    FIXTURES_SETUP robohub_devices_host_build)

# The Sonoff S60TPF's tests, each run again by name from the binary host-tests
# built (the runner builds into $TMPDIR/robohub-<module>-tests), so each is a
# case of its own and a requirement's evidence is the test that verifies it.
# They need host-tests to have built the binary, and are not run without it.
get_filename_component(ROBOHUB_DEVICES_NAME "${ROBOHUB_DEVICES_ROOT}" NAME)
set(ROBOHUB_DEVICES_HOST_OUT "${CMAKE_CURRENT_BINARY_DIR}/robohub-${ROBOHUB_DEVICES_NAME}-tests")
foreach(test
        test_sonoff_topics
        test_sonoff_result_and_power
        test_sonoff_state_and_sensor
        test_sonoff_onoff_any_case
        test_sonoff_requested_state_not_cached
        test_sonoff_publish_refused
        test_sonoff_offline_lwt
        test_sonoff_offline_by_silence
        test_sonoff_malformed
        test_sonoff_bad_prefixes
        test_sonoff_manifest_matches_driver
        test_sonoff_manifest_declares
        test_sonoff_docs_cover_preparation)
    add_test(NAME ${test} COMMAND "${ROBOHUB_DEVICES_HOST_OUT}/test_sonoff" ${test})
    set_tests_properties(${test} PROPERTIES FIXTURES_REQUIRED robohub_devices_host_build)
endforeach()
endif()

# The end-to-end scripts. GATEWAY_BIN, and NODE_BIN when it is set, reach them
# from the environment `rho test` was run in.
if(ROBOHUB_DEVICES_TESTS STREQUAL "e2e" OR ROBOHUB_DEVICES_TESTS STREQUAL "all")
find_program(ROBOHUB_PYTHON3 python3)
foreach(script e2e_power e2e_growatt e2e_smart_plug)
    string(REPLACE "_" "-" name "${script}")
    if(ROBOHUB_PYTHON3)
        add_test(NAME ${name} COMMAND "${ROBOHUB_PYTHON3}" "${ROBOHUB_DEVICES_ROOT}/tests/${script}.py")
    else()
        add_test(NAME ${name} COMMAND "${ROBOHUB_SH}" -c "echo 'skipped: python3 is not installed'; exit 77")
    endif()
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
endforeach()
endif()
