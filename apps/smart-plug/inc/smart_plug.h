/**
 * @file smart_plug.h
 * @brief smart-plug — a temperature sensor and a switch, as one device.
 *
 * The composition, apart from how it is wired: a `RoboHubTemperature` and a
 * `RoboHubSwitch` become one device that reports the temperature and the switch's
 * state and answers a switch command. The wiring — which sensor, which relay,
 * which pins — is the program's, so the same composition runs on a board with a
 * DS18B20 and a relay GPIO and in a host test with a simulated pair.
 *
 * @project RoboHub Platform
 * @author Aurel Miron
 * @version 0.1
 * @date 2026
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SMART_PLUG_H
#define SMART_PLUG_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma once

#include "types.h"
#include "error.h"
#include "robohub/devices/device.h"
#include "robohub/drivers/temperature.h"
#include "robohub/devices/switch.h"
#include "robohub/device-node/device_node.h"

/** The parts a smart plug is made of, and the node it publishes through. */
typedef struct {
    RoboHubTemperature* temperature;
    RoboHubSwitch*      output;
    RoboHubDeviceId     id;
    RoboHubDeviceNode*  node;
} RoboHubSmartPlug;

/** Bind the parts and the node. Both parts must outlive the plug. */
void robohub_smart_plug_init(RoboHubSmartPlug* plug,
                             RoboHubTemperature* temperature,
                             RoboHubSwitch* output,
                             RoboHubDeviceId id,
                             RoboHubDeviceNode* node);

/** A command handler, ready to hand to `robohub_device_node_add_device`. */
RoboHubError robohub_smart_plug_command(RoboHubDeviceId id,
                                        rh_u16 opcode,
                                        rh_u32 argument,
                                        rh_u32* actual,
                                        void* user);

/** The poll, ready to hand to `robohub_device_node_set_poll`. */
RoboHubError robohub_smart_plug_poll(void* user);

#ifdef __cplusplus
}
#endif

#endif /* SMART_PLUG_H */
