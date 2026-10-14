/*
 * test_growatt.c — the Growatt device over a fake Modbus block, and the reading
 * it produces round-tripped through the generated PowerReading payload.
 *
 * The fake RS485 answers a function-04 read with a block of registers built from
 * a test map, so the decode is checked independently of the default map's
 * addresses. The generated payload is then written and read back, which is what
 * proves the device's reading and the gateway's message are one layout.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include <stdio.h>
#include <string.h>
#include "robohub/protocols/modbus.h"
#include "robohub/devices/inverter/growatt.h"
#include "protocol.h"

/* A figure scaled by 0.1 is not exactly representable, so a scaled field is
 * compared to a tolerance and an integer field exactly. */
static int near(rh_f32 a, rh_f32 b)
{
    rh_f32 d = a - b;
    if (d < 0) d = -d;
    return d < 0.0005f;
}

typedef struct {
    rh_u16 block[ROBOHUB_MODBUS_MAX_REGISTERS];
    rh_u16 count;
} Fake;

static RoboHubError bus_init(RoboHubRS485* s, rh_u32 baud) { (void)s; (void)baud; return ROBOHUB_OK; }
static RoboHubError bus_set_dir(RoboHubRS485* s, rh_u32 dir) { (void)s; (void)dir; return ROBOHUB_OK; }
static RoboHubError bus_read(RoboHubRS485* s, rh_u8* d, rh_size n) { (void)s; (void)d; (void)n; return ROBOHUB_ERR_NOT_SUPPORTED; }

static RoboHubError bus_write(RoboHubRS485* s, const rh_u8* d, rh_size n)
{
    (void)s; (void)d; (void)n;
    return ROBOHUB_OK; /* the request is not inspected here */
}

static rh_size bus_read_timed(RoboHubRS485* s, rh_u8* d, rh_size n, rh_u32 t)
{
    (void)t;
    Fake* f = (Fake*)s->context;
    rh_u8 reply[3u + 2u * ROBOHUB_MODBUS_MAX_REGISTERS + 2u];
    reply[0] = 1u;
    reply[1] = ROBOHUB_MODBUS_FC_READ_INPUT;
    reply[2] = (rh_u8)(f->count * 2u);
    for (rh_u16 i = 0; i < f->count; i++) {
        reply[3u + 2u * i] = (rh_u8)(f->block[i] >> 8);
        reply[4u + 2u * i] = (rh_u8)(f->block[i] & 0xFFu);
    }
    rh_size len = 3u + 2u * f->count;
    rh_u16 crc = robohub_modbus_crc16(reply, len);
    reply[len] = (rh_u8)(crc & 0xFFu);
    reply[len + 1u] = (rh_u8)(crc >> 8);
    len += 2u;

    rh_size copy = len < n ? len : n;
    memcpy(d, reply, copy);
    return copy;
}

int main(void)
{
    Fake fake;
    memset(&fake, 0, sizeof(fake));
    fake.count = 16u;
    /* 0.1 kW figures, signed where they flow either way. */
    fake.block[1] = 1200u;    /* pv  1200 W  (scale 1) */
    fake.block[2] = 800u;     /* load 800 W  */
    fake.block[3] = 0xFFCEu;  /* grid -50 W  (signed) */
    fake.block[4] = 100u;     /* battery +100 W */
    fake.block[5] = 855u;     /* soc 85.5 %  (scale 0.1) */
    fake.block[6] = 42u;      /* energy today 4.2 kWh (scale 0.1) */
    fake.block[7] = 12345u;   /* energy total 1234.5 kWh */

    RoboHubRS485 bus;
    memset(&bus, 0, sizeof(bus));
    bus.name = "rs485_0";
    bus.context = &fake;
    bus.init = bus_init;
    bus.write = bus_write;
    bus.read = bus_read;
    bus.read_timed = bus_read_timed;
    bus.set_direction = bus_set_dir;

    RoboHubModbus master;
    int ok = robohub_modbus_init(&master, &bus, 1u, 0u) == ROBOHUB_OK;

    RoboHubGrowattMap map = {
        .base = 0u, .count = 16u,
        .pv_power = { 1u, 1.0f }, .load_power = { 2u, 1.0f },
        .grid_power = { 3u, 1.0f }, .battery_power = { 4u, 1.0f },
        .battery_soc = { 5u, 0.1f }, .energy_today = { 6u, 0.1f },
        .energy_total = { 7u, 0.1f },
    };
    RoboHubDeviceId id = { .area_id = 7u, .node_id = 10u };

    RoboHubGrowatt growatt;
    ok = ok && robohub_growatt_init(&growatt, &master, id, &map) == ROBOHUB_OK;

    RoboHubPowerReading reading;
    memset(&reading, 0, sizeof(reading));
    ok = ok && growatt.interface.read(&growatt.interface, &reading) == ROBOHUB_OK;

    ok = ok && reading.kind == ROBOHUB_DEVICE_INVERTER;
    ok = ok && reading.device.area_id == 7u && reading.device.node_id == 10u;
    ok = ok && reading.pv_power_w == 1200.0f;
    ok = ok && reading.load_power_w == 800.0f;
    ok = ok && reading.grid_power_w == -50.0f;
    ok = ok && reading.battery_power_w == 100.0f;
    ok = ok && near(reading.battery_soc, 85.5f);
    ok = ok && near(reading.energy_today_kwh, 4.2f);
    ok = ok && near(reading.energy_total_kwh, 1234.5f);
    ok = ok && (reading.valid & ROBOHUB_POWER_PV_POWER) != 0u &&
         (reading.valid & ROBOHUB_POWER_ENERGY_TOTAL) != 0u;

    /* The reading and the gateway's message are one layout. */
    PowerReading payload;
    memset(&payload, 0, sizeof(payload));
    payload.device.area_id = reading.device.area_id;
    payload.device.node_id = reading.device.node_id;
    payload.kind = (rh_u32)reading.kind;
    payload.valid = reading.valid;
    payload.pv_power_w = reading.pv_power_w;
    payload.grid_power_w = reading.grid_power_w;
    payload.energy_total_kwh = reading.energy_total_kwh;

    rh_u8 encoded[POWER_READING_WIRE_SIZE];
    rh_size length = PowerReading_serialize(&payload, encoded, sizeof(encoded));
    ok = ok && length == POWER_READING_WIRE_SIZE;

    PowerReading back;
    memset(&back, 0, sizeof(back));
    ok = ok && PowerReading_deserialize(encoded, length, &back);
    ok = ok && back.device.area_id == 7u && back.device.node_id == 10u;
    ok = ok && back.kind == (rh_u32)ROBOHUB_DEVICE_INVERTER;
    ok = ok && back.pv_power_w == 1200.0f && back.grid_power_w == -50.0f;
    ok = ok && back.energy_total_kwh == 1234.5f;

    printf(ok ? "Growatt OK\n" : "Growatt FAILED\n");
    return ok ? 0 : 1;
}
