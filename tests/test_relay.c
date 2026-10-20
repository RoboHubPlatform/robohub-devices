/*
 * test_relay.c — the relay over a fake GPIO.
 *
 * The checks are the wiring: bring-up sets the pin an output and leaves the
 * relay off, driving the switch on writes the level `active_high` says, and a
 * relay whose pin was changed by something else is read back as on.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include <stdio.h>
#include <string.h>
#include "robohub/devices/actuator/relay.h"

typedef struct {
    int mode;
    int level;
    int writes[8];
    int write_count;
} FakeGpio;

static RoboHubError gpio_init(RoboHubGPIO* s) { (void)s; return ROBOHUB_OK; }
static RoboHubError gpio_set_mode(RoboHubGPIO* s, rh_u32 pin, rh_u32 mode)
{
    (void)pin;
    ((FakeGpio*)s->context)->mode = (int)mode;
    return ROBOHUB_OK;
}
static RoboHubError gpio_write(RoboHubGPIO* s, rh_u32 pin, rh_u32 value)
{
    (void)pin;
    FakeGpio* g = (FakeGpio*)s->context;
    g->level = (int)value;
    if (g->write_count < 8) g->writes[g->write_count++] = (int)value;
    return ROBOHUB_OK;
}
static RoboHubError gpio_read(RoboHubGPIO* s, rh_u32 pin, rh_u32* value)
{
    (void)pin;
    *value = (rh_u32)((FakeGpio*)s->context)->level;
    return ROBOHUB_OK;
}

static RoboHubGPIO make_gpio(FakeGpio* fake)
{
    RoboHubGPIO gpio;
    memset(&gpio, 0, sizeof(gpio));
    gpio.name = "gpio0";
    gpio.context = fake;
    gpio.init = gpio_init;
    gpio.set_mode = gpio_set_mode;
    gpio.write = gpio_write;
    gpio.read = gpio_read;
    return gpio;
}

int main(void)
{
    int ok = 1;

    /* Active-high: on is a high level, and bring-up leaves it off. */
    FakeGpio high;
    memset(&high, 0, sizeof(high));
    high.level = 1; /* left high before we touch it */
    RoboHubGPIO gpio = make_gpio(&high);

    RoboHubRelay relay;
    ok = ok && robohub_relay_init(&relay, &gpio, 21u, RH_TRUE) == ROBOHUB_OK;
    ok = ok && high.mode == (int)ROBOHUB_GPIO_MODE_OUTPUT;
    ok = ok && high.write_count == 1 && high.writes[0] == 0; /* off at bring-up */
    ok = ok && high.level == 0;

    rh_bool on = RH_TRUE;
    ok = ok && relay.interface.set(&relay.interface, RH_TRUE) == ROBOHUB_OK && high.level == 1;
    ok = ok && relay.interface.get(&relay.interface, &on) == ROBOHUB_OK && on == RH_TRUE;
    ok = ok && relay.interface.set(&relay.interface, RH_FALSE) == ROBOHUB_OK && high.level == 0;
    ok = ok && relay.interface.get(&relay.interface, &on) == ROBOHUB_OK && on == RH_FALSE;

    /* A relay changed behind our back is reported as it is: the pin went high. */
    high.level = 1;
    ok = ok && relay.interface.get(&relay.interface, &on) == ROBOHUB_OK && on == RH_TRUE;

    /* Active-low: on is a low level. */
    FakeGpio low;
    memset(&low, 0, sizeof(low));
    RoboHubGPIO gpio_low = make_gpio(&low);
    RoboHubRelay relay_low;
    ok = ok && robohub_relay_init(&relay_low, &gpio_low, 5u, RH_FALSE) == ROBOHUB_OK;
    ok = ok && low.level == 1; /* off is high when active-low */
    ok = ok && relay_low.interface.set(&relay_low.interface, RH_TRUE) == ROBOHUB_OK && low.level == 0;
    ok = ok && relay_low.interface.get(&relay_low.interface, &on) == ROBOHUB_OK && on == RH_TRUE;

    printf(ok ? "Relay OK\n" : "Relay FAILED\n");
    return ok ? 0 : 1;
}
