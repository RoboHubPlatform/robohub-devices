/*
 * growatt.c — a Growatt hybrid inverter over Modbus RTU.
 *
 * One block read, then each figure is taken from the register the map names and
 * scaled. Nothing here knows an address by heart; the map does, and the default
 * map is the one place to correct when a model's document disagrees.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "growatt.h"

#include <stddef.h>
#include <string.h>

/*
 * The default map.
 *
 * These addresses are a starting point, not a verified map: Growatt's register
 * layouts differ between series and firmware. Confirm every address against the
 * specific model's Modbus document before trusting a reading, and correct this
 * table rather than the decode below. The block is read whole, so a register
 * that moves within it is a change to the number here and nothing else.
 *
 * Scale is in the reading's unit per register count: power figures here are
 * counted in 0.1 kW by the published layout, which is 100 W per count, so the
 * scales below turn them into watts; the energy figures are counted in 0.1 kWh,
 * which is 0.1 per count. If your model counts in different units, the scale is
 * what changes.
 */
static const RoboHubGrowattMap growatt_default_map = {
    .base = 0u,
    .count = 48u,
    .pv_power      = { 3u,  100.0f },
    .load_power    = { 7u,  100.0f },
    .grid_power    = { 9u,  100.0f },
    .battery_power = { 11u, 100.0f },
    .battery_soc   = { 13u, 1.0f },
    .energy_today  = { 26u, 0.1f },
    .energy_total  = { 28u, 0.1f },
};

const RoboHubGrowattMap* robohub_growatt_default_map(void)
{
    return &growatt_default_map;
}

/* The value of one field, or 0 when its register is outside the block that was
 * read. `signed_field` takes the register as a two's-complement 16-bit value,
 * which is how a power that flows either way is carried. */
static rh_f32 growatt_field(const RoboHubGrowatt* dev, RoboHubGrowattField field, rh_bool signed_field)
{
    if (field.addr < dev->map.base || field.addr >= (rh_u16)(dev->map.base + dev->map.count)) {
        return 0.0f;
    }
    rh_u16 raw = dev->raw[field.addr - dev->map.base];
    rh_i32 value = signed_field ? (rh_i32)(rh_i16)raw : (rh_i32)raw;
    return (rh_f32)value * field.scale;
}

/* -------------------------------------------------------------------------- */
/*  Interface operations                                                      */
/* -------------------------------------------------------------------------- */

static RoboHubError growatt_op_init(RoboHubInverter* self)
{
    RoboHubGrowatt* dev = (RoboHubGrowatt*)self->context;
    if (dev->modbus == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    dev->ready = RH_TRUE;
    return ROBOHUB_OK;
}

static RoboHubError growatt_op_deinit(RoboHubInverter* self)
{
    ((RoboHubGrowatt*)self->context)->ready = RH_FALSE;
    return ROBOHUB_OK;
}

static RoboHubError growatt_op_read(RoboHubInverter* self, RoboHubPowerReading* out)
{
    RoboHubGrowatt* dev = (RoboHubGrowatt*)self->context;
    if (out == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (!dev->ready) {
        return ROBOHUB_ERR_STATE;
    }

    RoboHubError err = robohub_modbus_read_input(dev->modbus, dev->map.base, dev->map.count, dev->raw);
    if (ROBOHUB_FAILED(err)) {
        return err;
    }

    memset(out, 0, sizeof(*out));
    out->device = dev->id;
    out->kind = ROBOHUB_DEVICE_INVERTER;
    out->status = 0u;
    out->valid = (rh_u16)(ROBOHUB_POWER_PV_POWER | ROBOHUB_POWER_LOAD_POWER |
                          ROBOHUB_POWER_GRID_POWER | ROBOHUB_POWER_BATTERY_POWER |
                          ROBOHUB_POWER_BATTERY_SOC | ROBOHUB_POWER_ENERGY_TODAY |
                          ROBOHUB_POWER_ENERGY_TOTAL);

    out->pv_power_w = growatt_field(dev, dev->map.pv_power, RH_FALSE);
    out->load_power_w = growatt_field(dev, dev->map.load_power, RH_FALSE);
    out->grid_power_w = growatt_field(dev, dev->map.grid_power, RH_TRUE);
    out->battery_power_w = growatt_field(dev, dev->map.battery_power, RH_TRUE);
    out->battery_soc = growatt_field(dev, dev->map.battery_soc, RH_FALSE);
    out->energy_today_kwh = growatt_field(dev, dev->map.energy_today, RH_FALSE);
    out->energy_total_kwh = growatt_field(dev, dev->map.energy_total, RH_FALSE);
    return ROBOHUB_OK;
}

/* -------------------------------------------------------------------------- */
/*  Binding                                                                   */
/* -------------------------------------------------------------------------- */

RoboHubError robohub_growatt_init(RoboHubGrowatt* dev,
                                  RoboHubModbus* modbus,
                                  RoboHubDeviceId id,
                                  const RoboHubGrowattMap* map)
{
    if (dev == NULL || modbus == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (map != NULL && map->count > ROBOHUB_MODBUS_MAX_REGISTERS) {
        return ROBOHUB_ERR_INVALID_ARG;
    }

    memset(dev, 0, sizeof(*dev));
    dev->modbus = modbus;
    dev->map = (map != NULL) ? *map : growatt_default_map;
    dev->id = id;
    dev->ready = RH_FALSE;

    dev->interface.name = "growatt.inverter";
    dev->interface.context = dev;
    dev->interface.init = growatt_op_init;
    dev->interface.deinit = growatt_op_deinit;
    dev->interface.read = growatt_op_read;

    return growatt_op_init(&dev->interface);
}
