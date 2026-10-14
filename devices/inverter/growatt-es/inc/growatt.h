/**
 * @file growatt.h
 * @brief Device - a Growatt hybrid inverter over Modbus RTU.
 *
 * A Growatt 6000 ES Plus (SPH) is read over its RS485 Modbus port: a block of
 * input registers holds the PV, load, grid and battery figures, and a pair of
 * registers holds the energy counters. This turns that block into a
 * `RoboHubPowerReading`, so a Growatt and a Hoymiles are the same kind of thing
 * to everything above.
 *
 * The register map is data, not code. `RoboHubGrowattMap` is the addresses and
 * the scale of each figure, and the default is a starting point — a Growatt's
 * register map is model-specific, and the default below must be confirmed
 * against the particular model's Modbus document before it is trusted. Getting
 * an address wrong is a wrong number, and the map is where that is corrected;
 * nothing in this file changes to move a register.
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

/** One figure: the register it is in and the units per count. */
typedef struct {
    rh_u16 addr;
    /** The value is `raw * scale` in the reading's own unit. A register that
     *  counts watts has scale 1; one that counts tenths has 0.1. */
    rh_f32 scale;
} RoboHubGrowattField;

/**
 * @brief Where a Growatt's figures are.
 *
 * One contiguous block is read, from `base` for `count` registers, and each
 * figure names its register within it. A figure whose address falls outside the
 * block is absent from the reading rather than read from elsewhere.
 */
typedef struct {
    rh_u16 base;   /* first input register of the block */
    rh_u16 count;  /* how many registers the block holds */

    RoboHubGrowattField pv_power;      /* W */
    RoboHubGrowattField load_power;    /* W, non-negative */
    RoboHubGrowattField grid_power;    /* W, signed: + import */
    RoboHubGrowattField battery_power; /* W, signed: + charge */
    RoboHubGrowattField battery_soc;   /* percent */
    RoboHubGrowattField energy_today;  /* kWh */
    RoboHubGrowattField energy_total;  /* kWh */
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
 * `map` may be NULL for the default map, which is a starting point to be
 * confirmed against the model's own register document.
 */
RoboHubError robohub_growatt_init(RoboHubGrowatt* dev,
                                  RoboHubModbus* modbus,
                                  RoboHubDeviceId id,
                                  const RoboHubGrowattMap* map);

/** The default map. See the warning above: confirm it against your model. */
const RoboHubGrowattMap* robohub_growatt_default_map(void);

#ifdef __cplusplus
}
#endif

#endif /* GROWATT_H */
