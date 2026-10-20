/*
 * relay.c — a relay, or any on/off output, over a GPIO.
 *
 * The pin's level and the switch's state are two different things and the
 * inversion is the only place they meet: a caller sets the switch on, and the
 * level that means is `active_high` or its opposite. Reading does the same in
 * reverse, so a relay closed by something else is reported as on.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "relay.h"

#include <stddef.h>

static rh_u32 level_for(const RoboHubRelay* relay, rh_bool on)
{
    if (relay->active_high) {
        return on ? 1u : 0u;
    }
    return on ? 0u : 1u;
}

static RoboHubError relay_op_init(RoboHubSwitch* self)
{
    RoboHubRelay* relay = (RoboHubRelay*)self->context;
    if (relay->gpio == NULL || relay->gpio->set_mode == NULL || relay->gpio->write == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    RoboHubError err = relay->gpio->set_mode(relay->gpio, relay->pin, ROBOHUB_GPIO_MODE_OUTPUT);
    if (ROBOHUB_FAILED(err)) {
        return err;
    }
    /* Off at bring-up, so a relay does not close the moment the program starts. */
    err = relay->gpio->write(relay->gpio, relay->pin, level_for(relay, RH_FALSE));
    if (ROBOHUB_SUCCEEDED(err)) {
        relay->ready = RH_TRUE;
    }
    return err;
}

static RoboHubError relay_op_deinit(RoboHubSwitch* self)
{
    RoboHubRelay* relay = (RoboHubRelay*)self->context;
    relay->ready = RH_FALSE;
    if (relay->gpio != NULL && relay->gpio->write != NULL) {
        return relay->gpio->write(relay->gpio, relay->pin, level_for(relay, RH_FALSE));
    }
    return ROBOHUB_OK;
}

static RoboHubError relay_op_get(RoboHubSwitch* self, rh_bool* on)
{
    RoboHubRelay* relay = (RoboHubRelay*)self->context;
    if (on == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (!relay->ready || relay->gpio->read == NULL) {
        return ROBOHUB_ERR_STATE;
    }
    rh_u32 level = 0;
    RoboHubError err = relay->gpio->read(relay->gpio, relay->pin, &level);
    if (ROBOHUB_FAILED(err)) {
        return err;
    }
    *on = relay->active_high ? (level != 0u) : (level == 0u);
    return ROBOHUB_OK;
}

static RoboHubError relay_op_set(RoboHubSwitch* self, rh_bool on)
{
    RoboHubRelay* relay = (RoboHubRelay*)self->context;
    if (!relay->ready || relay->gpio->write == NULL) {
        return ROBOHUB_ERR_STATE;
    }
    return relay->gpio->write(relay->gpio, relay->pin, level_for(relay, on));
}

RoboHubError robohub_relay_init(RoboHubRelay* relay,
                                RoboHubGPIO* gpio,
                                rh_u32 pin,
                                rh_bool active_high)
{
    if (relay == NULL || gpio == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    relay->gpio = gpio;
    relay->pin = pin;
    relay->active_high = active_high;
    relay->ready = RH_FALSE;

    relay->interface.name = "relay";
    relay->interface.context = relay;
    relay->interface.init = relay_op_init;
    relay->interface.deinit = relay_op_deinit;
    relay->interface.get = relay_op_get;
    relay->interface.set = relay_op_set;

    return relay_op_init(&relay->interface);
}
