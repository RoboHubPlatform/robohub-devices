/**
 * @file main.c
 * @brief smart-plug host node — the composition, on a host, before hardware.
 *
 * The real composition (`smart_plug.c`), the real relay and the real DS18B20
 * driver, over the real device-node runtime — with the two parts a host does not
 * have replaced by fakes: a GPIO and a 1-Wire bus with one simulated DS18B20. It
 * speaks the real protocol to the real gateway over a tty, so everything except
 * the sensor and the relay is the code that will run on the board.
 *
 *     rho build -target native .
 *
 * Configuration is from the environment: `RH_NODE_UART` (a tty), `AREA`, `NODE`.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "platform.h"
#include "registry.h"
#include "types.h"
#include "error.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "robohub/hal/uart.h"
#include "robohub/hal/timer.h"
#include "robohub/hal/onewire.h"
#include "robohub/hal/gpio.h"
#include "robohub/device-node/device_node.h"
#include "robohub/drivers/temperature/ds18b20.h"
#include "robohub/devices/actuator/relay.h"
#include "smart_plug.h"
#include "fakes.h"

#define ONEWIRE_NAME    "onewire0"
#define GPIO_NAME       "gpio0"
#define TIMER_NAME      "system_timer"
#define RELAY_PIN       21u
#define REPORT_MS       1000u

static RoboHubDS18B20 sensor;
static RoboHubRelay   relay;
static RoboHubDeviceNode node;
static RoboHubSmartPlug plug;
static RoboHubUART* gateway_link;

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

static const char* env_str(const char* name, const char* fallback)
{
    const char* value = getenv(name);
    return (value != NULL && value[0] != '\0') ? value : fallback;
}

static rh_u32 env_u32(const char* name, rh_u32 fallback)
{
    const char* value = getenv(name);
    return (value != NULL && value[0] != '\0') ? (rh_u32)strtoul(value, NULL, 0) : fallback;
}

int main(void)
{
    if (robohub_platform_init() != ROBOHUB_OK) {
        return 1;
    }

    /* The two parts a host does not have, as fakes that implement the contracts. */
    (void)robohub_register_gpio(GPIO_NAME, host_fake_gpio());
    (void)robohub_register_onewire(ONEWIRE_NAME, host_fake_onewire());

    gateway_link = robohub_get_uart(env_str("RH_NODE_UART", "uart0"));
    RoboHubOneWire* bus = robohub_get_onewire(ONEWIRE_NAME);
    RoboHubGPIO* gpio = robohub_get_gpio(GPIO_NAME);
    RoboHubTimer* timer = robohub_get_timer(TIMER_NAME);
    if (gateway_link == NULL || bus == NULL || gpio == NULL || timer == NULL) {
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
    if (ROBOHUB_FAILED(robohub_relay_init(&relay, gpio, RELAY_PIN, RH_TRUE))) {
        return 1;
    }

    RoboHubDeviceId id = {
        .area_id = (rh_u16)env_u32("AREA", 1u),
        .node_id = (rh_u16)env_u32("NODE", 20u),
    };
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
