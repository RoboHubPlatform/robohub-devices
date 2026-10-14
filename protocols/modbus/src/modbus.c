/*
 * modbus.c — Modbus RTU master over an RS485 bus.
 *
 * A request is built, written, and the reply is read with the bus's timed read
 * and checked: unit id, function code, the byte count the function promises, and
 * the CRC. Anything that does not match is a protocol error, so a frame that
 * arrived half-formed never becomes a register value.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "modbus.h"

#include <stddef.h>
#include <string.h>

#define MODBUS_EXCEPTION_BIT 0x80u
#define MODBUS_REQUEST_LEN   8u
/* A reply is unit id, function, byte count, the data and the CRC. */
#define MODBUS_REPLY_OVERHEAD 5u

rh_u16 robohub_modbus_crc16(const rh_u8* data, rh_size len)
{
    rh_u16 crc = 0xFFFFu;
    for (rh_size i = 0; i < len; i++) {
        crc ^= data[i];
        for (rh_u32 bit = 0; bit < 8u; bit++) {
            if (crc & 1u) {
                crc = (rh_u16)((crc >> 1) ^ 0xA001u);
            } else {
                crc = (rh_u16)(crc >> 1);
            }
        }
    }
    return crc;
}

RoboHubError robohub_modbus_init(RoboHubModbus* self,
                                 RoboHubRS485* bus,
                                 rh_u8 slave,
                                 rh_u32 timeout_ms)
{
    if (self == NULL || bus == NULL || bus->write == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    self->bus = bus;
    self->slave = slave;
    self->timeout_ms = timeout_ms != 0u ? timeout_ms : ROBOHUB_MODBUS_TIMEOUT_MS;
    return ROBOHUB_OK;
}

/* Send a request and return the reply, checking the unit id and the CRC. `frame`
 * receives the answer and `got` its length. The function code is checked by the
 * caller, because a write and a read answer with different shapes. */
static RoboHubError modbus_transact(RoboHubModbus* self,
                                    rh_u8* request,
                                    rh_size request_len,
                                    rh_u8* reply,
                                    rh_size reply_cap,
                                    rh_size* got)
{
    rh_u16 crc = robohub_modbus_crc16(request, request_len);
    request[request_len] = (rh_u8)(crc & 0xFFu);
    request[request_len + 1u] = (rh_u8)((crc >> 8) & 0xFFu);

    RoboHubError err = self->bus->write(self->bus, request, request_len + 2u);
    if (ROBOHUB_FAILED(err)) {
        return err;
    }

    if (self->bus->read_timed == NULL) {
        return ROBOHUB_ERR_NOT_SUPPORTED;
    }
    rh_size len = self->bus->read_timed(self->bus, reply, reply_cap, self->timeout_ms);
    if (len == 0u) {
        return ROBOHUB_ERR_TIMEOUT;
    }
    if (len < 3u) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    if (reply[0] != self->slave) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    *got = len;
    return ROBOHUB_OK;
}

static RoboHubError modbus_read(RoboHubModbus* self,
                                rh_u8 function,
                                rh_u16 addr,
                                rh_u16 count,
                                rh_u16* out)
{
    if (self == NULL || self->bus == NULL || out == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    if (count == 0u || count > ROBOHUB_MODBUS_MAX_REGISTERS) {
        return ROBOHUB_ERR_INVALID_ARG;
    }

    rh_u8 request[MODBUS_REQUEST_LEN];
    request[0] = self->slave;
    request[1] = function;
    request[2] = (rh_u8)(addr >> 8);
    request[3] = (rh_u8)(addr & 0xFFu);
    request[4] = (rh_u8)(count >> 8);
    request[5] = (rh_u8)(count & 0xFFu);

    rh_u8 reply[MODBUS_REPLY_OVERHEAD + 2u * ROBOHUB_MODBUS_MAX_REGISTERS];
    rh_size got = 0u;
    RoboHubError err = modbus_transact(self, request, 6u, reply,
                                       (rh_size)sizeof(reply), &got);
    if (ROBOHUB_FAILED(err)) {
        return err;
    }

    /* An exception reply is unit id, function|0x80, the exception code and the
     * CRC: a definite answer, and a definite failure. */
    if (reply[1] == (rh_u8)(function | MODBUS_EXCEPTION_BIT)) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    if (reply[1] != function) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    if (got < MODBUS_REPLY_OVERHEAD) {
        return ROBOHUB_ERR_PROTOCOL;
    }

    rh_u8 byte_count = reply[2];
    if (byte_count != (rh_u8)(count * 2u)) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    rh_size total = (rh_size)MODBUS_REPLY_OVERHEAD + byte_count;
    if (got < total) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    rh_u16 expected = robohub_modbus_crc16(reply, total - 2u);
    rh_u16 found = (rh_u16)(reply[total - 2u] | ((rh_u16)reply[total - 1u] << 8));
    if (expected != found) {
        return ROBOHUB_ERR_PROTOCOL;
    }

    for (rh_u16 i = 0; i < count; i++) {
        out[i] = (rh_u16)(((rh_u16)reply[3u + 2u * i] << 8) | (rh_u16)reply[4u + 2u * i]);
    }
    return ROBOHUB_OK;
}

RoboHubError robohub_modbus_read_input(RoboHubModbus* self,
                                       rh_u16 addr,
                                       rh_u16 count,
                                       rh_u16* out)
{
    return modbus_read(self, ROBOHUB_MODBUS_FC_READ_INPUT, addr, count, out);
}

RoboHubError robohub_modbus_read_holding(RoboHubModbus* self,
                                         rh_u16 addr,
                                         rh_u16 count,
                                         rh_u16* out)
{
    return modbus_read(self, ROBOHUB_MODBUS_FC_READ_HOLDING, addr, count, out);
}

RoboHubError robohub_modbus_write_register(RoboHubModbus* self,
                                           rh_u16 addr,
                                           rh_u16 value)
{
    if (self == NULL || self->bus == NULL) {
        return ROBOHUB_ERR_INVALID_ARG;
    }

    rh_u8 request[MODBUS_REQUEST_LEN];
    request[0] = self->slave;
    request[1] = ROBOHUB_MODBUS_FC_WRITE_REG;
    request[2] = (rh_u8)(addr >> 8);
    request[3] = (rh_u8)(addr & 0xFFu);
    request[4] = (rh_u8)(value >> 8);
    request[5] = (rh_u8)(value & 0xFFu);

    /* A write is echoed back whole: unit id, function, address, value, CRC. */
    rh_u8 reply[MODBUS_REQUEST_LEN];
    rh_size got = 0u;
    RoboHubError err = modbus_transact(self, request, 6u, reply,
                                       (rh_size)sizeof(reply), &got);
    if (ROBOHUB_FAILED(err)) {
        return err;
    }
    if (got < MODBUS_REQUEST_LEN || reply[1] != ROBOHUB_MODBUS_FC_WRITE_REG) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    rh_u16 expected = robohub_modbus_crc16(reply, MODBUS_REQUEST_LEN - 2u);
    rh_u16 found = (rh_u16)(reply[MODBUS_REQUEST_LEN - 2u] |
                            ((rh_u16)reply[MODBUS_REQUEST_LEN - 1u] << 8));
    if (expected != found) {
        return ROBOHUB_ERR_PROTOCOL;
    }
    return ROBOHUB_OK;
}
