/**
 * @file relay.h
 * @brief Device - a relay (or any on/off output) over a GPIO.
 *
 * The simplest thing a program drives: a pin that is high or low, behind a
 * relay that switches a load. It is a `RoboHubSwitch`, so an application holds
 * the interface and not this, and a smart plug that drives a relay is written
 * the same way whatever the pin is on.
 *
 * `active_high` is the wiring: a relay board whose input is pulled high to close
 * the contact is active-high, one that is pulled low is active-low. The
 * interface is in terms of the switch being on, so the level is inverted here
 * and a caller never thinks in it.
 *
 * @project RoboHub Platform
 * @author Aurel Miron
 * @version 0.1
 * @date 2026
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RELAY_H
#define RELAY_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma once

/* -------------------------------------------------------------------------- */
/*  Includes                                                                  */
/* -------------------------------------------------------------------------- */
#include "types.h"
#include "error.h"
#include "robohub/hal/gpio.h"
#include "robohub/devices/switch.h"

/* -------------------------------------------------------------------------- */
/*  Device                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief One relay on one GPIO pin.
 */
typedef struct {
    RoboHubSwitch interface;  /* Must remain the first member. */
    RoboHubGPIO*  gpio;       /* The controller the pin is on. */
    rh_u32        pin;        /* The pin. */
    rh_bool       active_high;/* Whether driving high closes the contact. */
    rh_bool       ready;
} RoboHubRelay;

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Bind a relay to a GPIO pin.
 *
 * Brings the pin up as an output and leaves it off. The GPIO must outlive the
 * relay.
 */
RoboHubError robohub_relay_init(RoboHubRelay* relay,
                                RoboHubGPIO* gpio,
                                rh_u32 pin,
                                rh_bool active_high);

#ifdef __cplusplus
}
#endif

#endif /* RELAY_H */
