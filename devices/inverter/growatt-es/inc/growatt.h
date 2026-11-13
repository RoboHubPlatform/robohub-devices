/**
 * @file growatt.h
 * @brief Device - a Growatt hybrid inverter over Modbus RTU.
 *
 * A Growatt SPF 6000 ES Plus is read over its RS485 Modbus port (9600 8N1,
 * address 1 unless changed in holding register 30): one block of input
 * registers, function 04, holds the PV, load, grid and battery figures and the
 * energy counters. This turns that block into a
 * `RoboHubPowerReading`, so a Growatt and a Hoymiles are the same kind of thing
 * to everything above.
 *
 * The register map is data, not code. `RoboHubGrowattMap` is the address, width,
 * sign and scale of each figure. The default is the SPF 3000-6000 ES Plus layout
 * of Growatt's off-grid RS485 protocol (V0.26); an SPH or another family has its
 * own document and its own map, passed to `robohub_growatt_init`. It has not yet
 * been read from a real inverter: confirm the figures against the inverter's
 * display before trusting them. Nothing in this file changes to move a register.
 *
 * There is no clock on the bus and no clock in the reading: the caller that
 * publishes the reading stamps it, so the timestamp is the node's and does not
 * depend on the device having one.
 *
 * @project RoboHub Platform
 * @author Aurel Miron
 * @version 0.1
 * @date 2026
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GROWATT_H
#define GROWATT_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma once

/* -------------------------------------------------------------------------- */
/*  Includes                                                                  */
/* -------------------------------------------------------------------------- */
#include "types.h"
#include "error.h"
#include "robohub/devices/device.h"
#include "robohub/devices/inverter.h"
#include "robohub/protocols/modbus.h"

/* -------------------------------------------------------------------------- */
/*  Register map                                                              */
/* -------------------------------------------------------------------------- */

/** One figure: where it is, how wide, and the units per count. */
typedef struct {
    rh_u16 addr;
    /** How many registers it takes: 1, or 2 for a 32-bit figure carried as a
     *  high register then a low one. 0 is a figure the model does not have. */
    rh_u8  width;
    /** Two's complement: a power that flows either way. */
    rh_bool is_signed;
    /** The value is `raw * scale` in the reading's own unit. A register that
     *  counts tenths of a watt has 0.1; a negative scale turns the model's sign
     *  into the reading's (Growatt counts battery discharge as positive, the
     *  reading counts charge). */
    rh_f32 scale;
} RoboHubGrowattField;

/**
 * @brief Where a Growatt's figures are.
 *
 * One contiguous block is read, from `base` for `count` registers, and each
 * figure names its register within it. A figure whose registers fall outside
 * the block is absent from the reading rather than read from elsewhere. The
 * second PV string's figures are added to the first's, so a one-string model
 * leaves them at width 0.
 */
typedef struct {
    rh_u16 base;   /* first input register of the block */
    rh_u16 count;  /* how many registers the block holds (at most 125) */
    RoboHubGrowattField pv_power;        /* W, PV1 */
    RoboHubGrowattField pv2_power;       /* W, PV2, added to PV1 */
    RoboHubGrowattField load_power;      /* W, non-negative */
    RoboHubGrowattField grid_power;      /* W, signed: + import */
    RoboHubGrowattField battery_power;   /* W, signed: + charge */
    RoboHubGrowattField battery_soc;     /* percent */
    RoboHubGrowattField energy_today;    /* kWh, PV1 */
    RoboHubGrowattField energy2_today;   /* kWh, PV2, added */
    RoboHubGrowattField energy_total;    /* kWh, PV1 */
    RoboHubGrowattField energy2_total;   /* kWh, PV2, added */
} RoboHubGrowattMap;

/* -------------------------------------------------------------------------- */
/*  Device                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief One Growatt, and the inverter interface it presents.
 */
typedef struct {
    RoboHubInverter interface;   /* Must remain the first member. */
    RoboHubModbus*  modbus;      /* The master the device is behind. */
    RoboHubGrowattMap map;       /* Where the figures are. */
    RoboHubDeviceId id;          /* The device this reading names. */
    rh_bool         ready;
    rh_u16          raw[ROBOHUB_MODBUS_MAX_REGISTERS];
} RoboHubGrowatt;

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Bind a Growatt to a Modbus master.
 *
 * `map` may be NULL for the default map (SPF 6000 ES Plus). A map whose block
 * is larger than a Modbus read (125 registers) is refused.
 */
RoboHubError robohub_growatt_init(RoboHubGrowatt* dev,
                                  RoboHubModbus* modbus,
                                  RoboHubDeviceId id,
                                  const RoboHubGrowattMap* map);

/** The default map: the SPF 3000-6000 ES Plus, off-grid protocol V0.26. */
const RoboHubGrowattMap* robohub_growatt_default_map(void);

#ifdef __cplusplus
}
#endif

#endif /* GROWATT_H */
