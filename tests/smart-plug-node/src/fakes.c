/*
 * fakes.c — a host GPIO and a host 1-Wire with a DS18B20 on it.
 *
 * The GPIO keeps a level per pin. The 1-Wire answers the DS18B20 commands the
 * driver sends — skip ROM, convert, read scratchpad — and returns a scratchpad
 * at the configured temperature with the Dallas CRC the driver checks, so the
 * driver's own protocol and arithmetic run unchanged.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "fakes.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/*  GPIO                                                                      */
/* -------------------------------------------------------------------------- */

#define FAKE_PINS 64u

typedef struct {
    rh_u32 level[FAKE_PINS];
} FakeGpio;

static FakeGpio fake_gpio_state;
static RoboHubGPIO fake_gpio;

static RoboHubError gpio_init(RoboHubGPIO* self) { (void)self; return ROBOHUB_OK; }
static RoboHubError gpio_set_mode(RoboHubGPIO* self, rh_u32 pin, rh_u32 mode)
{
    (void)self; (void)pin; (void)mode;
    return ROBOHUB_OK;
}
static RoboHubError gpio_write(RoboHubGPIO* self, rh_u32 pin, rh_u32 value)
{
    FakeGpio* g = (FakeGpio*)self->context;
    if (pin < FAKE_PINS) {
        g->level[pin] = value ? 1u : 0u;
    }
    return ROBOHUB_OK;
}
static RoboHubError gpio_read(RoboHubGPIO* self, rh_u32 pin, rh_u32* value)
{
    FakeGpio* g = (FakeGpio*)self->context;
    *value = (pin < FAKE_PINS) ? g->level[pin] : 0u;
    return ROBOHUB_OK;
}

RoboHubGPIO* host_fake_gpio(void)
{
    memset(&fake_gpio, 0, sizeof(fake_gpio));
    fake_gpio.name = "gpio0";
    fake_gpio.context = &fake_gpio_state;
    fake_gpio.init = gpio_init;
    fake_gpio.set_mode = gpio_set_mode;
    fake_gpio.write = gpio_write;
    fake_gpio.read = gpio_read;
    return &fake_gpio;
}

/* -------------------------------------------------------------------------- */
/*  1-Wire with a DS18B20                                                     */
/* -------------------------------------------------------------------------- */

#define DS18B20_CMD_SKIP_ROM    0xCCu
#define DS18B20_CMD_CONVERT_T   0x44u
#define DS18B20_CMD_WRITE_SCR   0x4Eu
#define DS18B20_CMD_READ_SCR    0xBEu
#define DS18B20_CMD_COPY_SCR    0x48u

typedef struct {
    rh_f32  celsius;
    rh_bool reading;
    rh_u32  at;
    rh_u8   scratchpad[9];
} FakeOneWire;

static FakeOneWire fake_wire_state;
static RoboHubOneWire fake_wire;

/* The Dallas CRC-8 the DS18B20 scratchpad carries. */
static rh_u8 crc8(const rh_u8* data, rh_u32 len)
{
    rh_u8 crc = 0;
    for (rh_u32 i = 0; i < len; i++) {
        crc ^= data[i];
        for (rh_u32 bit = 0; bit < 8u; bit++) {
            if (crc & 1u) {
                crc = (rh_u8)((crc >> 1) ^ 0x8Cu);
            } else {
                crc = (rh_u8)(crc >> 1);
            }
        }
    }
    return crc;
}

/* Build the scratchpad for the configured temperature: the raw value is the
 * temperature in sixteenths, little-endian. */
static void wire_build_scratchpad(FakeOneWire* w)
{
    rh_i32 raw = (rh_i32)(w->celsius * 16.0f);
    w->scratchpad[0] = (rh_u8)(raw & 0xFF);
    w->scratchpad[1] = (rh_u8)((raw >> 8) & 0xFF);
    w->scratchpad[2] = 0x7Fu;  /* alarm high */
    w->scratchpad[3] = 0x00u;  /* alarm low */
    w->scratchpad[4] = 0x7Fu;  /* 12-bit configuration */
    w->scratchpad[5] = 0xFFu;
    w->scratchpad[6] = 0xFFu;
    w->scratchpad[7] = 0x10u;
    w->scratchpad[8] = crc8(w->scratchpad, 8u);
}

static RoboHubError wire_init(RoboHubOneWire* self) { (void)self; return ROBOHUB_OK; }
static RoboHubError wire_deinit(RoboHubOneWire* self) { (void)self; return ROBOHUB_OK; }

static RoboHubError wire_reset(RoboHubOneWire* self, rh_bool* present)
{
    FakeOneWire* w = (FakeOneWire*)self->context;
    w->reading = RH_FALSE;
    w->at = 0u;
    *present = RH_TRUE; /* one device on the bus */
    return ROBOHUB_OK;
}

static RoboHubError wire_write_byte(RoboHubOneWire* self, rh_u8 byte)
{
    FakeOneWire* w = (FakeOneWire*)self->context;
    switch (byte) {
    case DS18B20_CMD_CONVERT_T:
        wire_build_scratchpad(w);
        break;
    case DS18B20_CMD_READ_SCR:
        w->reading = RH_TRUE;
        w->at = 0u;
        break;
    case DS18B20_CMD_SKIP_ROM:
    case DS18B20_CMD_WRITE_SCR:
    case DS18B20_CMD_COPY_SCR:
    default:
        break; /* the ROM command, and the write-scratchpad data, are not modelled */
    }
    return ROBOHUB_OK;
}

static RoboHubError wire_read_byte(RoboHubOneWire* self, rh_u8* byte)
{
    FakeOneWire* w = (FakeOneWire*)self->context;
    if (w->reading && w->at < 9u) {
        *byte = w->scratchpad[w->at++];
    } else {
        *byte = 0xFFu; /* no device drives the line: reads float high */
    }
    return ROBOHUB_OK;
}

static RoboHubError wire_write(RoboHubOneWire* self, const rh_u8* data, rh_size len)
{
    for (rh_size i = 0; i < len; i++) {
        (void)wire_write_byte(self, data[i]);
    }
    return ROBOHUB_OK;
}

static RoboHubError wire_read(RoboHubOneWire* self, rh_u8* data, rh_size len)
{
    for (rh_size i = 0; i < len; i++) {
        (void)wire_read_byte(self, &data[i]);
    }
    return ROBOHUB_OK;
}

RoboHubOneWire* host_fake_onewire(void)
{
    memset(&fake_wire, 0, sizeof(fake_wire));
    fake_wire_state.celsius = 25.0f;
    wire_build_scratchpad(&fake_wire_state);
    fake_wire.name = "onewire0";
    fake_wire.context = &fake_wire_state;
    fake_wire.init = wire_init;
    fake_wire.deinit = wire_deinit;
    fake_wire.reset = wire_reset;
    fake_wire.write_byte = wire_write_byte;
    fake_wire.read_byte = wire_read_byte;
    fake_wire.write = wire_write;
    fake_wire.read = wire_read;
    return &fake_wire;
}

void host_fake_set_temperature(rh_f32 celsius)
{
    fake_wire_state.celsius = celsius;
    wire_build_scratchpad(&fake_wire_state);
}
