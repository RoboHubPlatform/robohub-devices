/*
 * test_modbus.c — the Modbus RTU master over a fake RS485 bus.
 *
 * The fake answers with a hand-built reply and records the request, so the
 * checks are the framing itself: the request bytes, the CRC, the decoded
 * registers, and that a bad CRC, an exception and a silent slave are each the
 * right failure.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include <stdio.h>
#include <string.h>
#include "modbus.h"

typedef struct {
    rh_u8   request[32];
    rh_size request_len;
    rh_u8   reply[64];
    rh_size reply_len;
    rh_bool answer;
} Fake;

static RoboHubError bus_init(RoboHubRS485* s, rh_u32 baud) { (void)s; (void)baud; return ROBOHUB_OK; }
static RoboHubError bus_write(RoboHubRS485* s, const rh_u8* d, rh_size n)
{
    Fake* f = (Fake*)s->context;
    memcpy(f->request, d, n);
    f->request_len = n;
    return ROBOHUB_OK;
}
static rh_size bus_read_timed(RoboHubRS485* s, rh_u8* d, rh_size n, rh_u32 t)
{
    (void)t;
    Fake* f = (Fake*)s->context;
    if (!f->answer || f->reply_len == 0u) return 0u;
    rh_size copy = f->reply_len < n ? f->reply_len : n;
    memcpy(d, f->reply, copy);
    return copy;
}
static RoboHubError bus_read(RoboHubRS485* s, rh_u8* d, rh_size n) { (void)s; (void)d; (void)n; return ROBOHUB_ERR_NOT_SUPPORTED; }
static RoboHubError bus_set_dir(RoboHubRS485* s, rh_u32 dir) { (void)s; (void)dir; return ROBOHUB_OK; }

static rh_size make_read_reply(rh_u8 slave, rh_u8 fc, const rh_u16* regs, rh_u16 count, rh_u8* out)
{
    out[0] = slave;
    out[1] = fc;
    out[2] = (rh_u8)(count * 2u);
    for (rh_u16 i = 0; i < count; i++) {
        out[3u + 2u * i] = (rh_u8)(regs[i] >> 8);
        out[4u + 2u * i] = (rh_u8)(regs[i] & 0xFFu);
    }
    rh_size len = 3u + 2u * count;
    rh_u16 crc = robohub_modbus_crc16(out, len);
    out[len] = (rh_u8)(crc & 0xFFu);
    out[len + 1u] = (rh_u8)(crc >> 8);
    return len + 2u;
}

int main(void)
{
    Fake fake;
    memset(&fake, 0, sizeof(fake));
    fake.answer = RH_TRUE;

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
    int ok = 1;
    ok = ok && robohub_modbus_init(&master, &bus, 1u, 50u) == ROBOHUB_OK;

    /* A clean read: the request is the address and count, and the registers come
     * back in order. */
    rh_u16 regs[4] = { 10u, 20u, 30u, 40u };
    fake.reply_len = make_read_reply(1u, ROBOHUB_MODBUS_FC_READ_INPUT, regs, 4u, fake.reply);

    rh_u16 got[4] = { 0 };
    ok = ok && robohub_modbus_read_input(&master, 3u, 4u, got) == ROBOHUB_OK;
    ok = ok && got[0] == 10u && got[1] == 20u && got[2] == 30u && got[3] == 40u;

    /* The request carries unit 1, function 4, address 3 and count 4. */
    ok = ok && fake.request_len == 8u && fake.request[0] == 1u &&
         fake.request[1] == ROBOHUB_MODBUS_FC_READ_INPUT &&
         fake.request[2] == 0u && fake.request[3] == 3u &&
         fake.request[4] == 0u && fake.request[5] == 4u;
    ok = ok && (rh_u16)(fake.request[6] | ((rh_u16)fake.request[7] << 8)) ==
         robohub_modbus_crc16(fake.request, 6u);

    /* A malformed CRC is a protocol error, not a wrong register. */
    fake.reply[fake.reply_len - 1u] ^= 0xFFu;
    ok = ok && robohub_modbus_read_input(&master, 3u, 4u, got) == ROBOHUB_ERR_PROTOCOL;

    /* An exception reply is the same. */
    fake.reply[0] = 1u; fake.reply[1] = 0x84u; fake.reply[2] = 0x02u;
    rh_u16 crc = robohub_modbus_crc16(fake.reply, 3u);
    fake.reply[3] = (rh_u8)(crc & 0xFFu); fake.reply[4] = (rh_u8)(crc >> 8); fake.reply_len = 5u;
    ok = ok && robohub_modbus_read_input(&master, 3u, 4u, got) == ROBOHUB_ERR_PROTOCOL;

    /* A silent slave is a timeout. */
    fake.answer = RH_FALSE;
    ok = ok && robohub_modbus_read_input(&master, 3u, 4u, got) == ROBOHUB_ERR_TIMEOUT;

    printf(ok ? "Modbus OK\n" : "Modbus FAILED\n");
    return ok ? 0 : 1;
}
