/**
 * @file main.c
 * @brief smart-plug — a temperature sensor and a relay, as one device, on a board.
 *
 * The wiring for a device built from `smart_plug.c`: a DS18B20 on the 1-Wire bus
 * the platform registered, a relay on a GPIO, and the node runtime over the
 * UART the gateway is wired to. What the device is — how it reads, what it
 * answers — is the composition's; this file is only which sensor and which pin.
 *
 *     rho build -target pico2w .
 *
 * The wiring is overridable from the build: `RH_SMART_PLUG_UART`, `_GPIO`,
 * `_RELAY_PIN`, `_RELAY_ACTIVE_HIGH`, `_AREA`, `_NODE`.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "platform.h"
#include "registry.h"
#include "types.h"
#include "error.h"

#include <stddef.h>

#include "robohub/hal/uart.h"
#include "robohub/hal/gpio.h"
#include "robohub/hal/timer.h"
#include "robohub/hal/onewire.h"
#include "robohub/device-node/device_node.h"
#include "robohub/drivers/temperature/ds18b20.h"
#include "robohub/devices/actuator/relay.h"
#include "smart_plug.h"

#ifndef RH_SMART_PLUG_AREA
#  define RH_SMART_PLUG_AREA 1u
#endif
#ifndef RH_SMART_PLUG_NODE
#  define RH_SMART_PLUG_NODE 20u
#endif
#ifndef RH_SMART_PLUG_UART
#  define RH_SMART_PLUG_UART "uart0"
#endif
#ifndef RH_SMART_PLUG_GPIO
#  define RH_SMART_PLUG_GPIO "gpio0"
#endif
#ifndef RH_SMART_PLUG_RELAY_PIN
#  define RH_SMART_PLUG_RELAY_PIN 21u
#endif
#ifndef RH_SMART_PLUG_RELAY_ACTIVE_HIGH
#  define RH_SMART_PLUG_RELAY_ACTIVE_HIGH 1
#endif

#define ONEWIRE_NAME "onewire0"
#define TIMER_NAME   "system_timer"
#define REPORT_MS    2000u

static RoboHubDS18B20 sensor;
static RoboHubRelay   relay;
static RoboHubDeviceNode node;
static RoboHubSmartPlug plug;
static RoboHubUART* gateway_link;

/* The link to the gateway: write a frame, and read what arrives so a command
 * reaches the runtime. */
static int link_write(const rh_u8* data, rh_size len, void* context)
{
    (void)context;
    if (gateway_link == NULL || gateway_link->write == NULL) {
        return -1;
    }
    return gateway_link->write(gateway_link, data, len) == ROBOHUB_OK ? 0 : -1;
}

static rh_size link_read(rh_u8* data, rh_size max_len, void* context)
{
    (void)context;
    if (gateway_link == NULL || gateway_link->read_available == NULL) {
        return 0u;
    }
    return gateway_link->read_available(gateway_link, data, max_len);
}

int main(void)
{
    if (robohub_platform_init() != ROBOHUB_OK) {
        return 1;
    }

    gateway_link = robohub_get_uart(RH_SMART_PLUG_UART);
    RoboHubOneWire* bus = robohub_get_onewire(ONEWIRE_NAME);
    RoboHubTimer* timer = robohub_get_timer(TIMER_NAME);
    RoboHubGPIO* gpio = robohub_get_gpio(RH_SMART_PLUG_GPIO);
    if (gateway_link == NULL || bus == NULL || timer == NULL || gpio == NULL) {
        return 1;
    }
    /* Bring the link up before anything writes to it. */
    if (gateway_link->init != NULL) {
        (void)gateway_link->init(gateway_link, 115200u);
    }

    RoboHubDS18B20Config sensor_config = { .bus = bus, .timer = timer };
    if (ROBOHUB_FAILED(robohub_ds18b20_init(&sensor, &sensor_config))) {
        return 1;
    }
    if (ROBOHUB_FAILED(robohub_relay_init(&relay, gpio, RH_SMART_PLUG_RELAY_PIN,
                                          RH_SMART_PLUG_RELAY_ACTIVE_HIGH ? RH_TRUE : RH_FALSE))) {
        return 1;
    }

    RoboHubDeviceId id = { .area_id = RH_SMART_PLUG_AREA, .node_id = RH_SMART_PLUG_NODE };
    RoboHubDeviceNodeConfig config = {
        .phy = { .write = link_write, .context = NULL },
        .read = link_read,
        .read_context = NULL,
        .poll_interval_ms = REPORT_MS,
    };
    if (ROBOHUB_FAILED(robohub_device_node_init(&node, &config))) {
        return 1;
    }

    robohub_smart_plug_init(&plug, &sensor.interface, &relay.interface, id, &node);
    if (ROBOHUB_FAILED(robohub_device_node_add_device(&node, id, robohub_smart_plug_command, &plug))) {
        return 1;
    }
    if (ROBOHUB_FAILED(robohub_device_node_set_poll(&node, robohub_smart_plug_poll, &plug))) {
        return 1;
    }

    (void)robohub_device_node_run(&node);
    for (;;) {
    }
}
