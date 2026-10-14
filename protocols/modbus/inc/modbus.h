/**
 * @file modbus.h
 * @brief Modbus RTU master over an RS485 bus.
 *
 * Modbus is what most solar equipment speaks on its RS485 port, and RTU is the
 * form it speaks there: a request is a unit id, a function code and its
 * arguments, closed with a CRC-16; the answer comes back the same way. This is
 * the master side — the side that asks — and it is written against
 * `RoboHubRS485`, so it runs over a fitted MAX485, a USB-RS485 adapter or a
 * platform's own 485 without knowing which.
 *
 * It carries registers and knows nothing about what they mean. A Growatt's
 * register 0 is not this module's business; `devices/` reads the registers and
 * says what they are. That split is the same one the drivers use: the bus moves
 * bytes, the device says what the bytes are.
 *
 * The response is read with the bus's timed read, because a Modbus reply is a
 * length nobody guarantees in advance: the slave may answer with an exception,
 * and may answer with fewer bytes. The count that arrived is checked against the
 * frame the function code promises, and a short or malformed answer is a
 * protocol error rather than a wrong number.
 *
 * @project RoboHub Platform
 * @author Aurel Miron
 * @version 0.1
 * @date 2026
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef MODBUS_H
#define MODBUS_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma once

/* -------------------------------------------------------------------------- */
/*  Includes                                                                  */
/* -------------------------------------------------------------------------- */
#include "types.h"
#include "error.h"
#include "robohub/hal/rs485.h"

/* -------------------------------------------------------------------------- */
/*  Constants                                                                 */
/* -------------------------------------------------------------------------- */

/** How long to wait for a reply, in milliseconds, when none is given. */
#define ROBOHUB_MODBUS_TIMEOUT_MS 500u
/** The largest block one request reads, the protocol's own limit for 16-bit
 *  registers in a single read. */
#define ROBOHUB_MODBUS_MAX_REGISTERS 125u

/** Function codes this master issues. */
#define ROBOHUB_MODBUS_FC_READ_HOLDING 0x03u
#define ROBOHUB_MODBUS_FC_READ_INPUT   0x04u
#define ROBOHUB_MODBUS_FC_WRITE_REG    0x06u

/* -------------------------------------------------------------------------- */
/*  Type Definitions                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief One master on one bus, talking to one slave.
 *
 * A value the caller owns. `slave` is the unit id the device answers to; one
 * master per slave is the ordinary arrangement on a short bus, and a program
 * that talks to several makes one of these per unit id over the same bus.
 */
typedef struct {
    RoboHubRS485* bus;        /* The RS485 bus the device is on. */
    rh_u8         slave;      /* The unit id to address. */
    rh_u32        timeout_ms; /* Reply window, or 0 for the default. */
} RoboHubModbus;

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

/** Bind a master to a bus and a unit id. */
RoboHubError robohub_modbus_init(RoboHubModbus* self,
                                 RoboHubRS485* bus,
                                 rh_u8 slave,
                                 rh_u32 timeout_ms);

/** Read `count` input registers (function 04) starting at `addr`. */
RoboHubError robohub_modbus_read_input(RoboHubModbus* self,
                                       rh_u16 addr,
                                       rh_u16 count,
                                       rh_u16* out);

/** Read `count` holding registers (function 03) starting at `addr`. */
RoboHubError robohub_modbus_read_holding(RoboHubModbus* self,
                                         rh_u16 addr,
                                         rh_u16 count,
                                         rh_u16* out);

/** Write one holding register (function 06). */
RoboHubError robohub_modbus_write_register(RoboHubModbus* self,
                                           rh_u16 addr,
                                           rh_u16 value);

/** The CRC-16/Modbus of a byte range, as the protocol defines it. */
rh_u16 robohub_modbus_crc16(const rh_u8* data, rh_size len);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_H */
