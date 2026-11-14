# Relay

A relay, or any on/off output, on one GPIO pin. It presents the `RoboHubSwitch`
device interface from `robohub-common`, so a program holds the interface and not
the relay: a smart plug that drives a relay is written the same way whatever the
pin is on. It is the primitive a switched load is built from; `apps/smart-plug`
composes it with a DS18B20 into one device.

| | |
|---|---|
| Kind | actuator |
| Transport | GPIO, through the HAL (`robohub/hal/gpio.h`) |
| Interface | `RoboHubSwitch` (`init`, `deinit`, `get`, `set`) |
| Signal | `switch` — an output, field `state`, for a toggle or a button |
| Header | `robohub/devices/actuator/relay.h` |
| CMake target | `rho::robohub-device-relay` |
| Kconfig | `RH_DEV_RELAY` (selects `RH_HAL_GPIO`) |

## Using it

```c
#include "robohub/devices/actuator/relay.h"

static RoboHubRelay relay;

RoboHubGPIO* gpio = robohub_get_gpio("gpio0");
if (robohub_relay_init(&relay, gpio, 15u, RH_TRUE) == ROBOHUB_OK) {
    RoboHubSwitch* sw = &relay.interface;
    sw->set(sw, RH_TRUE);           /* close the contact */
    rh_bool on = RH_FALSE;
    sw->get(sw, &on);               /* read it back from the pin */
}
```

`robohub_relay_init(relay, gpio, pin, active_high)` brings the pin up as an
output and leaves it **off**, so a relay does not close the moment the program
starts. The GPIO controller must outlive the relay.

`active_high` is the wiring: a relay board whose input is driven high to close
the contact is active-high, one driven low is active-low. The interface speaks
of the switch being on; the level is inverted inside the device, so a caller
never thinks in it. `get` reads the pin back, and both `get` and `set` answer
`ROBOHUB_ERR_STATE` before the relay is up; `deinit` turns it off.

## In a plan

The manifest's `device:` block (kind `actuator`, transport `gpio`) and its
`signals:` entry are what Mesh reads: the relay's `switch` is an output a
dashboard button or switch can bind to, reported as the field `state`.
