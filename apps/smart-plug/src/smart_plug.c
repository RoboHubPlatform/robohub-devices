/*
 * smart_plug.c — a temperature sensor and a switch, as one device.
 *
 * Two faces, one device. The temperature is read and published as a temperature
 * reading; the switch is read and published as a state reading; and a command
 * drives the switch and answers with the state it reached. The parts are
 * interfaces, so this file names neither a sensor nor a relay.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "smart_plug.h"

#include <stddef.h>

void robohub_smart_plug_init(RoboHubSmartPlug* plug,
                             RoboHubTemperature* temperature,
                             RoboHubSwitch* output,
                             RoboHubDeviceId id,
                             RoboHubDeviceNode* node)
{
    plug->temperature = temperature;
    plug->output = output;
    plug->id = id;
    plug->node = node;
}

RoboHubError robohub_smart_plug_command(RoboHubDeviceId id,
                                        rh_u16 opcode,
                                        rh_u32 argument,
                                        rh_u32* actual,
                                        void* user)
{
    (void)id;
    RoboHubSmartPlug* plug = (RoboHubSmartPlug*)user;

    if (opcode != ROBOHUB_OP_SET_SWITCH) {
        return ROBOHUB_ERR_NOT_SUPPORTED;
    }

    RoboHubError err = plug->output->set(plug->output, argument != 0u);
    if (ROBOHUB_FAILED(err)) {
        return err;
    }
    /* The acknowledgement carries the state the switch is in, not the one it was
     * told, so a relay changed by something else does not report a lie. */
    rh_bool reached = argument != 0u;
    if (ROBOHUB_FAILED(plug->output->get(plug->output, &reached))) {
        return ROBOHUB_ERR_IO;
    }
    *actual = reached ? 1u : 0u;
    return ROBOHUB_OK;
}

RoboHubError robohub_smart_plug_poll(void* user)
{
    RoboHubSmartPlug* plug = (RoboHubSmartPlug*)user;

    rh_f32 celsius = 0.0f;
    rh_u16 status = ROBOHUB_STATUS_OK;
    if (ROBOHUB_FAILED(plug->temperature->get_celsius(plug->temperature, &celsius))) {
        celsius = 0.0f;
        status = ROBOHUB_STATUS_FAULT;
    }
    (void)robohub_device_node_publish_temperature(plug->node, plug->id, celsius, status);

    rh_bool on = RH_FALSE;
    (void)plug->output->get(plug->output, &on);
    (void)robohub_device_node_publish_state(plug->node, plug->id, ROBOHUB_DEVICE_SWITCH,
                                            ROBOHUB_STATE_SWITCH_STATE, on, 0.0f,
                                            ROBOHUB_STATUS_OK);
    return ROBOHUB_OK;
}
