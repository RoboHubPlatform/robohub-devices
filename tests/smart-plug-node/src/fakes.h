/*
 * fakes.h — a host stand-in for the two parts a smart plug is built from.
 *
 * A host has no GPIO and no 1-Wire, so the composition is exercised here against
 * a fake of each: a GPIO whose pin levels are kept in memory, and a 1-Wire bus
 * with one DS18B20 on it answering at a fixed temperature. The fakes implement
 * the same HAL contracts a platform does, so the relay, the DS18B20 driver and
 * the composition above them are the real ones.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#ifndef FAKES_H
#define FAKES_H

#include "types.h"
#include "robohub/hal/gpio.h"
#include "robohub/hal/onewire.h"

/** The in-memory GPIO controller. */
RoboHubGPIO* host_fake_gpio(void);

/** The 1-Wire bus with one DS18B20 on it. */
RoboHubOneWire* host_fake_onewire(void);

/** The temperature the simulated DS18B20 reports, in degrees Celsius. */
void host_fake_set_temperature(rh_f32 celsius);

#endif /* FAKES_H */
