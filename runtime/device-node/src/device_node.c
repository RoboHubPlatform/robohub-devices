/*
 * device_node.c — the runtime a device node is built on.
 *
 * The plumbing every device node repeats: bring up the telemetry bridge and the
 * transport, register the runtime's application and its poll task, answer a
 * control request by routing it to the device it names, and publish a reading
 * through a helper that frames the right message. An application above this
 * supplies only its poll and its command handlers.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "device_node.h"
#include "protocol.h"

#include <stddef.h>
#include <string.h>

#include "robohub/telemetry.h"
#include "robohub/core/app.h"
#include "robohub/core/core.h"
#include "robohub/core/event.h"

/* One node per program: the runtime's application stages carry no user pointer,
 * so the node the application belongs to is held here. */
static RoboHubDeviceNode* g_node;

/* -------------------------------------------------------------------------- */
/*  The application                                                           */
/* -------------------------------------------------------------------------- */

static RoboHubError node_poll_task(robohub_core_task_id id, void* user)
{
    (void)id;
    RoboHubDeviceNode* node = (RoboHubDeviceNode*)user;
    if (node->poll == NULL) {
        return ROBOHUB_OK;
    }
    return node->poll(node->poll_user);
}

/* Read what has arrived on the link and offer it to the framing. This is how a
 * command reaches the node: the parser publishes a whole frame on the event bus,
 * and the dispatch below finds it. */
static RoboHubError node_link_task(robohub_core_task_id id, void* user)
{
    (void)id;
    RoboHubDeviceNode* node = (RoboHubDeviceNode*)user;
    if (node->config.read == NULL) {
        return ROBOHUB_OK;
    }
    rh_u8 buffer[64];
    rh_size got = node->config.read(buffer, sizeof(buffer), node->config.read_context);
    if (got > 0u) {
        robohub_transport_feed(&node->transport, buffer, got);
    }
    return ROBOHUB_OK;
}

static RoboHubError node_app_start(void)
{
    RoboHubDeviceNode* node = g_node;
    if (node == NULL) {
        return ROBOHUB_OK;
    }
    if (node->config.read != NULL) {
        robohub_core_task_id link_task = RH_CORE_TASK_INVALID;
        (void)robohub_core_task_add("link", node_link_task, node,
                                    ROBOHUB_DEVICE_NODE_LINK_MS, &link_task);
    }
    if (node->poll != NULL && node->config.poll_interval_ms > 0u) {
        return robohub_core_task_add("poll", node_poll_task, node,
                                     node->config.poll_interval_ms, &node->poll_task);
    }
    return ROBOHUB_OK;
}

static const robohub_core_app node_app = {
    .name = "device-node",
    .init = NULL,
    .start = node_app_start,
    .step = NULL,
    .stop = NULL,
};

/* -------------------------------------------------------------------------- */
/*  Command dispatch                                                          */
/* -------------------------------------------------------------------------- */

/* Route one control request to the device it names and answer it. The runtime
 * answers the sampling rate itself, so a device gets a pace control without
 * writing one; everything else goes to the device's own handler. */
static RoboHubError node_on_control(robohub_event_id id, const void* payload, rh_size size, void* user)
{
    (void)id;
    (void)user;
    RoboHubDeviceNode* node = g_node;

    ControlRequest request;
    if (!ControlRequest_deserialize((const rh_u8*)payload, (size_t)size, &request)) {
        return ROBOHUB_ERR_PROTOCOL;
    }

    ControlResponse response = {
        .device = request.device,
        .opcode = request.opcode,
        .result = 2u, /* no such command */
        .argument = request.argument,
    };
    RoboHubDeviceId dev = { request.device.area_id, request.device.node_id };

    if (request.opcode == (rh_u16)CONTROL_OPCODE_SET_SAMPLING_RATE) {
        if (node != NULL && node->poll_task != RH_CORE_TASK_INVALID) {
            rh_u32 interval = request.argument;
            if (interval < 100u) {
                interval = 100u;
            }
            if (ROBOHUB_SUCCEEDED(robohub_core_task_set_period(node->poll_task, interval))) {
                node->config.poll_interval_ms = interval;
                response.result = 0u;
                response.argument = interval;
            } else {
                response.result = 1u;
            }
        }
    } else if (node != NULL) {
        for (rh_u32 i = 0; i < node->device_count; i++) {
            if (node->devices[i].id.area_id != dev.area_id ||
                node->devices[i].id.node_id != dev.node_id) {
                continue;
            }
            if (node->devices[i].command != NULL) {
                rh_u32 actual = request.argument;
                RoboHubError err = node->devices[i].command(dev, request.opcode,
                                                            request.argument, &actual,
                                                            node->devices[i].user);
                response.result = ROBOHUB_SUCCEEDED(err) ? 0u : 1u;
                response.argument = actual;
            }
            break;
        }
    }

    rh_u8 bytes[CONTROL_RESPONSE_WIRE_SIZE];
    if (ControlResponse_serialize(&response, bytes, sizeof(bytes)) == 0u) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    return robohub_core_event_publish_with_tx(MESSAGE_ID_CONTROL_RESPONSE,
                                              robohub_core_event_current_tx(),
                                              bytes, (rh_size)CONTROL_RESPONSE_WIRE_SIZE);
}

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

RoboHubError robohub_device_node_init(RoboHubDeviceNode* node,
                                      const RoboHubDeviceNodeConfig* config)
{
    if (node == NULL || config == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    memset(node, 0, sizeof(*node));
    node->config = *config;
    node->poll_task = RH_CORE_TASK_INVALID;
    g_node = node;

    if (ROBOHUB_FAILED(robohub_telemetry_init())) {
        return ROBOHUB_ERR_STATE;
    }
    if (ROBOHUB_FAILED(robohub_transport_init(&node->transport, &config->phy))) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    RoboHubTelemetrySink sink = {
        .type = ROBOHUB_SINK_TRANSPORT,
        .handle.transport = &node->transport,
        .enabled = RH_TRUE,
    };
    if (ROBOHUB_FAILED(robohub_telemetry_add_sink(&sink))) {
        return ROBOHUB_ERR_NO_MEM;
    }
    return robohub_core_app_register(&node_app);
}

RoboHubError robohub_device_node_add_device(RoboHubDeviceNode* node,
                                            RoboHubDeviceId id,
                                            RoboHubDeviceCommandFn command,
                                            void* user)
{
    if (node == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (node->device_count >= ROBOHUB_DEVICE_NODE_MAX_DEVICES) {
        return ROBOHUB_ERR_NO_MEM;
    }
    node->devices[node->device_count].id = id;
    node->devices[node->device_count].command = command;
    node->devices[node->device_count].user = user;
    node->device_count++;
    return ROBOHUB_OK;
}

RoboHubError robohub_device_node_set_poll(RoboHubDeviceNode* node,
                                          RoboHubDevicePollFn poll,
                                          void* user)
{
    if (node == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    node->poll = poll;
    node->poll_user = user;
    return ROBOHUB_OK;
}

RoboHubError robohub_device_node_run(RoboHubDeviceNode* node)
{
    if (node == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (ROBOHUB_FAILED(robohub_core_init(NULL))) {
        return ROBOHUB_ERR_STATE;
    }
    node->ready = RH_TRUE;

    /* Enabled after the runtime is up: bringing it up initialises the event bus,
     * which starts empty and would drop a rule added before it. */
    (void)robohub_telemetry_enable_event(MESSAGE_ID_TELEMETRY, 1u);
    (void)robohub_telemetry_enable_event(MESSAGE_ID_POWER_READING, 1u);
    (void)robohub_telemetry_enable_event(MESSAGE_ID_STATE_READING, 1u);
    (void)robohub_telemetry_enable_event(MESSAGE_ID_CONTROL_RESPONSE, 1u);
    (void)robohub_core_event_subscribe(MESSAGE_ID_CONTROL_REQUEST, node_on_control, node);

    (void)robohub_core_run();
    return ROBOHUB_ERR_STATE; /* the loop is not meant to return */
}

/* -------------------------------------------------------------------------- */
/*  Publishing                                                                */
/* -------------------------------------------------------------------------- */

RoboHubError robohub_device_node_publish_temperature(RoboHubDeviceNode* node,
                                                     RoboHubDeviceId id,
                                                     rh_f32 celsius,
                                                     rh_u16 status)
{
    (void)node;
    Telemetry message;
    memset(&message, 0, sizeof(message));
    message.device.area_id = id.area_id;
    message.device.node_id = id.node_id;
    message.timestamp_ms = robohub_core_uptime_ms();
    message.temperature_c = celsius;
    message.status = (NodeStatus)status;

    rh_u8 payload[TELEMETRY_WIRE_SIZE];
    if (Telemetry_serialize(&message, payload, sizeof(payload)) == 0u) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    return robohub_core_event_publish(MESSAGE_ID_TELEMETRY, payload, (rh_size)TELEMETRY_WIRE_SIZE);
}

RoboHubError robohub_device_node_publish_power(RoboHubDeviceNode* node,
                                               const RoboHubPowerReading* reading)
{
    (void)node;
    if (reading == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    PowerReading message;
    memset(&message, 0, sizeof(message));
    message.device.area_id = reading->device.area_id;
    message.device.node_id = reading->device.node_id;
    message.timestamp_ms = robohub_core_uptime_ms();
    message.kind = (rh_u32)reading->kind;
    message.status = reading->status;
    message.valid = reading->valid;
    message.pv_power_w = reading->pv_power_w;
    message.load_power_w = reading->load_power_w;
    message.grid_power_w = reading->grid_power_w;
    message.battery_power_w = reading->battery_power_w;
    message.battery_soc = reading->battery_soc;
    message.energy_today_kwh = reading->energy_today_kwh;
    message.energy_total_kwh = reading->energy_total_kwh;

    rh_u8 payload[POWER_READING_WIRE_SIZE];
    if (PowerReading_serialize(&message, payload, sizeof(payload)) == 0u) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    return robohub_core_event_publish(MESSAGE_ID_POWER_READING, payload, (rh_size)POWER_READING_WIRE_SIZE);
}

RoboHubError robohub_device_node_publish_state(RoboHubDeviceNode* node,
                                               RoboHubDeviceId id,
                                               RoboHubDeviceKind kind,
                                               rh_u16 valid,
                                               rh_bool on,
                                               rh_f32 setpoint,
                                               rh_u16 status)
{
    (void)node;
    StateReading message;
    memset(&message, 0, sizeof(message));
    message.device.area_id = id.area_id;
    message.device.node_id = id.node_id;
    message.timestamp_ms = robohub_core_uptime_ms();
    message.kind = (rh_u32)kind;
    message.status = status;
    message.valid = valid;
    message.switch_state = on ? 1u : 0u;
    message.setpoint = setpoint;

    rh_u8 payload[STATE_READING_WIRE_SIZE];
    if (StateReading_serialize(&message, payload, sizeof(payload)) == 0u) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    return robohub_core_event_publish(MESSAGE_ID_STATE_READING, payload, (rh_size)STATE_READING_WIRE_SIZE);
}
