/**
 * @file hoymiles.h
 * @brief Device - a Hoymiles microinverter through an OpenDTU.
 *
 * A Hoymiles microinverter has no user-facing port. Its figures leave over the
 * vendor's radio to a DTU, and an OpenDTU is a DTU on the local network: it
 * answers `GET /api/livedata/status` with a JSON document holding each
 * inverter's power and yields. This reads that document and turns it into a
 * `RoboHubPowerReading`, so a microinverter is the same kind of thing to
 * everything above as a hybrid over Modbus.
 *
 * A microinverter generates and does nothing else: it has no battery, no grid
 * meter and no load. The reading therefore carries the photovoltaic power and
 * the energy counters and leaves the rest absent, which is what the `valid` mask
 * is for — absent is a device that has no such figure, not a zero.
 *
 * The two DC inputs (the two panels) are not separate devices; they are inputs
 * to this one. The document's `total` object is what is read, so the figures are
 * the inverter's, not one channel's.
 *
 * @project RoboHub Platform
 * @author Aurel Miron
 * @version 0.1
 * @date 2026
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HOYMILES_H
#define HOYMILES_H

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

/** The largest status document read, in bytes. */
#define ROBOHUB_HOYMILES_BODY_MAX 4096u

/** The path an OpenDTU answers its live figures on. */
#define ROBOHUB_HOYMILES_PATH "/api/livedata/status"

/* -------------------------------------------------------------------------- */
/*  Device                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief One Hoymiles microinverter, reached through an OpenDTU.
 *
 * `host` and `path` are read during a call and not kept, so the caller may build
 * them on its own stack, but they must stay valid for the life of the device —
 * they are the address, not a copy of the document.
 */
typedef struct {
    RoboHubInverter interface;   /* Must remain the first member. */
    const char* host;            /* The OpenDTU's host or address. */
    rh_u16      port;            /* The OpenDTU's HTTP port. */
    const char* path;            /* The status path, or NULL for the default. */
    RoboHubDeviceId id;          /* The device this reading names. */
    rh_bool     ready;
    char        body[ROBOHUB_HOYMILES_BODY_MAX];
} RoboHubHoymiles;

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Bind a Hoymiles to the OpenDTU that fronts it.
 *
 * `path` of NULL means `ROBOHUB_HOYMILES_PATH`.
 */
RoboHubError robohub_hoymiles_init(RoboHubHoymiles* dev,
                                   const char* host,
                                   rh_u16 port,
                                   const char* path,
                                   RoboHubDeviceId id);

#ifdef __cplusplus
}
#endif

#endif /* HOYMILES_H */
