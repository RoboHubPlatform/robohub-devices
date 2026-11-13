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

/* The SPF 3000-6000 ES Plus, from Growatt's off-grid RS485 protocol V0.26.
 * Powers are 32-bit, high register first, in 0.1 W; energies 32-bit in 0.1 kWh.
 *
 *    3-4   Ppv1             5-6   Ppv2           9-10  OP_Watt (load)
 *   36-37  AC_InWatt (+ from grid, - export)     18    BatterySOC, %
 *   77-78  Bat_Watt (+ discharge, - charge): the scale is negative because
 *          the reading counts charge as positive
 *   48-49  PV1 energy today 50-51 PV1 total  52-53 PV2 today  54-55 PV2 total
 *
 * Registers 0-101 are read in one request. */
static const RoboHubGrowattMap growatt_default_map = {
    .base = 0u,
    .count = 102u,
    .pv_power      = { 3u,  2u, RH_FALSE, 0.1f },
    .pv2_power     = { 5u,  2u, RH_FALSE, 0.1f },
    .load_power    = { 9u,  2u, RH_FALSE, 0.1f },
    .grid_power    = { 36u, 2u, RH_TRUE,  0.1f },
    .battery_power = { 77u, 2u, RH_TRUE, -0.1f },
    .battery_soc   = { 18u, 1u, RH_FALSE, 1.0f },
    .energy_today  = { 48u, 2u, RH_FALSE, 0.1f },
    .energy2_today = { 52u, 2u, RH_FALSE, 0.1f },
    .energy_total  = { 50u, 2u, RH_FALSE, 0.1f },
    .energy2_total = { 54u, 2u, RH_FALSE, 0.1f },
};

const RoboHubGrowattMap* robohub_growatt_default_map(void)
{
    return &growatt_default_map;
}

/* Whether a field's registers are in the block that was read. */
static rh_bool growatt_has(const RoboHubGrowatt* dev, RoboHubGrowattField field)
{
    return field.width != 0u && field.addr >= dev->map.base &&
           (rh_u32)field.addr + field.width <= (rh_u32)dev->map.base + dev->map.count;
}

/* The value of one field, or 0 when its registers are outside the block. A
 * 32-bit figure is the high register then the low one; a signed figure is two's
 * complement at its own width. */
static rh_f32 growatt_field(const RoboHubGrowatt* dev, RoboHubGrowattField field)
{
    if (!growatt_has(dev, field)) {
        return 0.0f;
    }
    const rh_u16* at = &dev->raw[field.addr - dev->map.base];
    rh_f32 value;
    if (field.width == 2u) {
        rh_u32 raw = ((rh_u32)at[0] << 16) | (rh_u32)at[1];
        value = field.is_signed ? (rh_f32)(rh_i32)raw : (rh_f32)raw;
    } else {
        value = field.is_signed ? (rh_f32)(rh_i16)at[0] : (rh_f32)at[0];
    }
    return value * field.scale;
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
    const RoboHubGrowattMap* m = &dev->map;
    rh_u16 valid = 0u;
    if (growatt_has(dev, m->pv_power)) {
        valid |= ROBOHUB_POWER_PV_POWER;
        out->pv_power_w = growatt_field(dev, m->pv_power) + growatt_field(dev, m->pv2_power);
    }
    if (growatt_has(dev, m->load_power)) {
        valid |= ROBOHUB_POWER_LOAD_POWER;
        out->load_power_w = growatt_field(dev, m->load_power);
    }
    if (growatt_has(dev, m->grid_power)) {
        valid |= ROBOHUB_POWER_GRID_POWER;
        out->grid_power_w = growatt_field(dev, m->grid_power);
    }
    if (growatt_has(dev, m->battery_power)) {
        valid |= ROBOHUB_POWER_BATTERY_POWER;
        out->battery_power_w = growatt_field(dev, m->battery_power);
    }
    if (growatt_has(dev, m->battery_soc)) {
        valid |= ROBOHUB_POWER_BATTERY_SOC;
        out->battery_soc = growatt_field(dev, m->battery_soc);
    }
    if (growatt_has(dev, m->energy_today)) {
        valid |= ROBOHUB_POWER_ENERGY_TODAY;
        out->energy_today_kwh = growatt_field(dev, m->energy_today) + growatt_field(dev, m->energy2_today);
    }
    if (growatt_has(dev, m->energy_total)) {
        valid |= ROBOHUB_POWER_ENERGY_TOTAL;
        out->energy_total_kwh = growatt_field(dev, m->energy_total) + growatt_field(dev, m->energy2_total);
    }
    out->valid = valid;
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
    if (map != NULL && (map->count == 0u || map->count > ROBOHUB_MODBUS_MAX_REGISTERS)) {
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
