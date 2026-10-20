/**
 * @file device_node.h
 * @brief The runtime a device node is built on.
 *
 * A device node reads a device and publishes its readings, and it answers a
 * command the gateway sends it. That is the same plumbing for every device, so
 * it lives here once: the link and its framing, the telemetry bridge, the
 * publish of a reading, and the dispatch of a control request to the device it
 * names. An application supplies only what is its own — how it reads its device
 * and what a command does — and the runtime supplies the rest.
 *
 * It is deliberately above `protocols/` and the drivers. An application does not
 * see the wire: it reports a temperature, a power reading or a state through a
 * helper that frames the right message, and it answers a command with an opcode
 * and a value. The message layout stays in the schema the runtime carries, so a
 * custom application is written against interfaces and opcodes and never against
 * a payload.
 *
 * A command arrives as the control family the ecosystem already uses: an opcode
 * and an argument, addressed to a device by its area and node. The runtime
 * routes by that identity, so one node may present several devices and each is
 * commanded separately. Opcodes 1..255 are the ecosystem's — the runtime answers
 * `SET_SAMPLING_RATE` itself — and a device's own begin at
 * `CONTROL_OPCODE_VENDOR_BASE`, so a custom command cannot collide.
 *
 * One node per program: the runtime is the program's runtime, and its
 * application stages carry no user pointer, so a single instance is the model.
 *
 * @project RoboHub Platform
 * @author Aurel Miron
 * @version 0.1
 * @date 2026
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef DEVICE_NODE_H
#define DEVICE_NODE_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma once

/* -------------------------------------------------------------------------- */
/*  Includes                                                                  */
/* -------------------------------------------------------------------------- */
#include "types.h"
#include "error.h"
#include "robohub/transport.h"
#include "robohub/core/scheduler.h"
#include "robohub/devices/device.h"
#include "robohub/devices/inverter.h"

/** How many devices one node may present. */
#define ROBOHUB_DEVICE_NODE_MAX_DEVICES 8u

/** How often the runtime reads the link, in milliseconds. */
#define ROBOHUB_DEVICE_NODE_LINK_MS 5u

/* -------------------------------------------------------------------------- */
/*  The vocabulary an application uses                                        */
/* -------------------------------------------------------------------------- */
/* These are the schema's, mirrored here so an application answers commands and
 * reports state by name and never generates or reads the protocol. */

/** NodeStatus: the reading is good. */
#define ROBOHUB_STATUS_OK 0u
/** NodeStatus: the reading is a fault — the device did not answer. */
#define ROBOHUB_STATUS_FAULT 1u

/** Command opcodes the ecosystem defines. A device's own begin at VENDOR_BASE. */
#define ROBOHUB_OP_SET_SAMPLING_RATE 1u
#define ROBOHUB_OP_SET_SWITCH        2u
#define ROBOHUB_OP_SET_SETPOINT      3u
#define ROBOHUB_OP_VENDOR_BASE       256u

/** StateReading `valid` bits. */
#define ROBOHUB_STATE_SWITCH_STATE 1u
#define ROBOHUB_STATE_SETPOINT     2u

/* -------------------------------------------------------------------------- */
/*  Types                                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief A device's command handler.
 *
 * Called with the opcode and argument from the request and the device the
 * request named. A handler writes the value it actually reached to `actual` and
 * returns `ROBOHUB_OK`; any failure is reported to the gateway as a refusal.
 */
typedef RoboHubError (*RoboHubDeviceCommandFn)(RoboHubDeviceId id,
                                               rh_u16 opcode,
                                               rh_u32 argument,
                                               rh_u32* actual,
                                               void* user);

/** The node's periodic work: read the devices and publish. */
typedef RoboHubError (*RoboHubDevicePollFn)(void* user);

/**
 * @brief Read bytes that have arrived on the link.
 *
 * Called every few milliseconds with a buffer; returns how many bytes were
 * copied, and zero when none have arrived. It is how a command reaches the
 * node: the runtime feeds what it reads to the framing, and a whole frame is
 * published on the event bus, where the control dispatch finds it.
 */
typedef rh_size (*RoboHubDeviceNodeReadFn)(rh_u8* data, rh_size max_len, void* context);

/** What a node needs to run. */
typedef struct {
    /** The link to the gateway, written by the transport. */
    RoboHubTransportPhy phy;
    /** The link read, so the node hears a command; may be NULL to publish only. */
    RoboHubDeviceNodeReadFn read;
    /** Passed to `read`. */
    void* read_context;
    /** How often the poll runs, in milliseconds. Zero registers no poll. */
    rh_u32 poll_interval_ms;
} RoboHubDeviceNodeConfig;

typedef struct {
    RoboHubDeviceId       id;
    RoboHubDeviceCommandFn command; /* may be NULL for a device that is only read */
    void*                 user;
} RoboHubDeviceNodeDevice;

/**
 * @brief One node: its link, its devices and its poll.
 *
 * A value the caller owns and declares once — it holds the transport's receive
 * buffer, so it belongs in static storage. Declare it with the `RoboHubDeviceNode`
 * definition in scope (it is not opaque, because the caller supplies the storage).
 */
typedef struct RoboHubDeviceNode {
    RoboHubTransport          transport;
    RoboHubDeviceNodeConfig   config;
    RoboHubDeviceNodeDevice   devices[ROBOHUB_DEVICE_NODE_MAX_DEVICES];
    rh_u32                    device_count;
    RoboHubDevicePollFn       poll;
    void*                     poll_user;
    robohub_core_task_id      poll_task;
    rh_bool                   ready;
} RoboHubDeviceNode;

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Bring a node up around a link.
 *
 * Initialises the telemetry bridge and the transport, registers the runtime's
 * application, and publishes nothing yet. Call it before `run`.
 */
RoboHubError robohub_device_node_init(RoboHubDeviceNode* node,
                                      const RoboHubDeviceNodeConfig* config);

/** Present a device, and the handler for the commands addressed to it. */
RoboHubError robohub_device_node_add_device(RoboHubDeviceNode* node,
                                            RoboHubDeviceId id,
                                            RoboHubDeviceCommandFn command,
                                            void* user);

/** Set the periodic work — read the devices and publish. */
RoboHubError robohub_device_node_set_poll(RoboHubDeviceNode* node,
                                          RoboHubDevicePollFn poll,
                                          void* user);

/**
 * @brief Run the node: initialise the runtime and block in its loop.
 *
 * Subscribes to control requests, enables the events the node publishes, and
 * runs. Returns only on failure.
 */
RoboHubError robohub_device_node_run(RoboHubDeviceNode* node);

/* -------------------------------------------------------------------------- */
/*  Publishing                                                                */
/* -------------------------------------------------------------------------- */
/* What an application calls from its poll. Each frames the message its reading
 * belongs to, so the application never touches the wire. */

/** Publish a temperature reading (the `Telemetry` message). */
RoboHubError robohub_device_node_publish_temperature(RoboHubDeviceNode* node,
                                                     RoboHubDeviceId id,
                                                     rh_f32 celsius,
                                                     rh_u16 status);

/** Publish a power/energy reading (the `PowerReading` message). */
RoboHubError robohub_device_node_publish_power(RoboHubDeviceNode* node,
                                               const RoboHubPowerReading* reading);

/** Publish a device's state (the `StateReading` message). */
RoboHubError robohub_device_node_publish_state(RoboHubDeviceNode* node,
                                               RoboHubDeviceId id,
                                               RoboHubDeviceKind kind,
                                               rh_u16 valid,
                                               rh_bool on,
                                               rh_f32 setpoint,
                                               rh_u16 status);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_NODE_H */
