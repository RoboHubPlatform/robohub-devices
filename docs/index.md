# RoboHub Devices

The devices a node is made of: inverters, switches, actuators and what they
report. Each device is a collection of drivers and protocols that implement
the interfaces declared by `robohub-common`.

## The devices

- **Inverters**: Growatt, Hoymiles HMS
- **Actuators**: relay
- **Sensors**: photoresistor, thermistor, analog ADC, encoder, pulse ADC,
  infrared receiver

## Building

```sh
cd nodes/gateway-node
rho build .
```

## Testing

```sh
ROBOHUB_COMMON_DIR=/path/to/robohub-common \\
ROBOHUB_LINUX_DIR=/path/to/robohub-linux \\
ROBOHUB_ORCHESTRATOR_DIR=/path/to/robohub \\
  tests/run.sh
```
