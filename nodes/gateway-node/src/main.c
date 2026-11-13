/**
 * @file main.c
 * @brief robohub-device-node — publish a device's power reading to a gateway.
 *
 * A device the ecosystem does not sense itself — a Growatt over RS485 Modbus, a
 * Hoymiles through an OpenDTU — is brought into the tree by a node that reads it
 * and publishes the reading as a `PowerReading` frame, the same frame a board
 * sends. The gateway lists the inverter like any other device; from there the
 * relay, the uplink and the REST API are unchanged.
 *
 * Which device, where it is and where the gateway is all come from the
 * environment, so one binary is every device without a rebuild:
 *
 *   RH_DEVICE        growatt | hoymiles                (default growatt)
 *   AREA, NODE       the device's identity             (default 1, 10)
 *   INTERVAL_MS      how often to read                 (default 5000)
 *
 *   Link to the gateway:
 *   RH_NODE_LINK     uart | tcp                        (default uart)
 *   RH_NODE_UART     the UART the gateway is wired to  (default uart0)
 *   RH_GATEWAY_HOST  gateway host, for a tcp link
 *   RH_GATEWAY_PORT  gateway port, for a tcp link
 *
 *   Growatt:
 *   RH_RS485_UART    the UART the MAX485 is on         (default uart1)
 *   RH_RS485_DE_GPIO the direction pin's controller; empty means automatic
 *                                                      (default empty)
 *   RH_RS485_DE_PIN  the direction pin                 (default 20)
 *   RH_RS485_BAUD    the bus speed                     (default 9600)
 *   RH_MODBUS_SLAVE  the inverter's unit id            (default 1)
 *
 *   Hoymiles:
 *   RH_OPENDTU_HOST  the OpenDTU's host                (default opendtu.local)
 *   RH_OPENDTU_PORT  the OpenDTU's HTTP port           (default 80)
 *
 *   The link's authentication (robohub-transport's secure.h): the board's id
 *   and device key, from the robohub_secret.h Mesh writes into a build it
 *   makes, or for a node run on a host:
 *   RH_SECURE_UID      the board's id, 16 hex digits
 *   RH_SECURE_KEY_FILE a file holding the device key, 32 hex digits; it should
 *                      be readable by the node's user alone
 *   With either, every frame is sealed and the gateway's session answer is read
 *   from the link; with neither the link is plain.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "registry.h"
#include "types.h"
#include "error.h"
#include "robohub/hal/console.h"
#include "robohub/hal/timer.h"
#include "robohub/hal/uart.h"
#include "robohub/net.h"
#include "robohub/transport.h"
#include "robohub/devices/inverter.h"
#include "robohub/devices/inverter/growatt.h"
#include "robohub/devices/inverter/hoymiles.h"
#include "max485_module.h"   /* max485-module: the RS485 carrier */
#include "protocol.h"       /* the generated schema: PowerReading */
#include "robohub/secure.h"

/* The board's id and key, written into this build by Mesh when it planned the
 * board, and tracked by no repository. */
#if defined(__has_include)
#  if __has_include("robohub_secret.h")
#    include "robohub_secret.h"
#  endif
#endif

/* -------------------------------------------------------------------------- */
/*  Configuration from the environment                                        */
/* -------------------------------------------------------------------------- */

static const char* env_str(const char* name, const char* fallback)
{
    const char* value = getenv(name);
    return (value != NULL && value[0] != '\0') ? value : fallback;
}

static rh_u32 env_u32(const char* name, rh_u32 fallback)
{
    const char* value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }
    return (rh_u32)strtoul(value, NULL, 0);
}

/* -------------------------------------------------------------------------- */
/*  The link to the gateway                                                   */
/* -------------------------------------------------------------------------- */

typedef struct {
    rh_bool       tcp;
    RoboHubUART*  uart;
    RoboHubSocket socket;
} Link;

static Link g_link;

/* What has arrived from the gateway, without waiting: a session answer, a
 * command. Zero when nothing has. */
static rh_size link_read(Link* link, rh_u8* data, rh_size cap)
{
    if (link->tcp) {
        int got = robohub_socket_recv(link->socket, data, cap, 0u);
        return got > 0 ? (rh_size)got : 0u;
    }
    if (link->uart != NULL && link->uart->read_available != NULL) {
        return link->uart->read_available(link->uart, data, cap);
    }
    return 0u;
}

static int link_write(const rh_u8* data, rh_size len, void* context)
{
    Link* link = (Link*)context;
    if (link->tcp) {
        return robohub_socket_send(link->socket, data, len) < 0 ? -1 : 0;
    }
    if (link->uart != NULL && link->uart->write != NULL) {
        return link->uart->write(link->uart, data, len) == ROBOHUB_OK ? 0 : -1;
    }
    return -1;
}

/* -------------------------------------------------------------------------- */
/*  The device                                                                */
/* -------------------------------------------------------------------------- */

static RoboHubRS485Uart g_rs485;
static RoboHubModbus    g_modbus;
static RoboHubGrowatt   g_growatt;
static RoboHubHoymiles  g_hoymiles;

static RoboHubInverter* open_growatt(RoboHubDeviceId id)
{
    RoboHubMax485Config config = *robohub_max485_default_config();
    config.uart = env_str("RH_RS485_UART", "uart1");
    config.de_gpio = env_str("RH_RS485_DE_GPIO", "");   /* automatic unless named */
    config.de_pin = env_u32("RH_RS485_DE_PIN", 20u);
    config.de_active_high = RH_TRUE;
    config.baud = env_u32("RH_RS485_BAUD", 9600u);

    if (ROBOHUB_FAILED(robohub_max485_init(&g_rs485, &config))) {
        return NULL;
    }
    RoboHubRS485* bus = robohub_get_rs485(config.name);
    if (bus == NULL) {
        return NULL;
    }
    if (ROBOHUB_FAILED(robohub_modbus_init(&g_modbus, bus,
                                           (rh_u8)env_u32("RH_MODBUS_SLAVE", 1u), 0u))) {
        return NULL;
    }
    if (ROBOHUB_FAILED(robohub_growatt_init(&g_growatt, &g_modbus, id, NULL))) {
        return NULL;
    }
    return &g_growatt.interface;
}

static RoboHubInverter* open_hoymiles(RoboHubDeviceId id)
{
    const char* host = env_str("RH_OPENDTU_HOST", "opendtu.local");
    rh_u16 port = (rh_u16)env_u32("RH_OPENDTU_PORT", 80u);
    if (ROBOHUB_FAILED(robohub_hoymiles_init(&g_hoymiles, host, port, NULL, id))) {
        return NULL;
    }
    return &g_hoymiles.interface;
}

/* -------------------------------------------------------------------------- */
/*  Publish one reading                                                       */
/* -------------------------------------------------------------------------- */

static void emit(const char* text)
{
    (void)robohub_console_puts(text);
}

static void emit_u32(rh_u32 value)
{
    char digits[12];
    rh_u32 count = 0;
    if (value == 0u) {
        digits[count++] = '0';
    }
    while (value > 0u) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0u) {
        char c = digits[--count];
        (void)robohub_console_write(&c, 1);
    }
}

/* Turn a reading into the schema's payload and frame it to the gateway. */
static RoboHubError publish(RoboHubTransport* transport,
                            const RoboHubPowerReading* reading,
                            rh_u32 timestamp_ms)
{
    PowerReading payload;
    memset(&payload, 0, sizeof(payload));
    payload.device.area_id = reading->device.area_id;
    payload.device.node_id = reading->device.node_id;
    payload.timestamp_ms = timestamp_ms;
    payload.kind = (rh_u32)reading->kind;
    payload.status = reading->status;
    payload.valid = reading->valid;
    payload.pv_power_w = reading->pv_power_w;
    payload.load_power_w = reading->load_power_w;
    payload.grid_power_w = reading->grid_power_w;
    payload.battery_power_w = reading->battery_power_w;
    payload.battery_soc = reading->battery_soc;
    payload.energy_today_kwh = reading->energy_today_kwh;
    payload.energy_total_kwh = reading->energy_total_kwh;

    rh_u8 encoded[POWER_READING_WIRE_SIZE];
    rh_size length = PowerReading_serialize(&payload, encoded, sizeof(encoded));
    if (length == 0u) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    return robohub_transport_send(transport, MESSAGE_ID_POWER_READING, 0u, encoded, (rh_u16)length);
}

/* -------------------------------------------------------------------------- */
/*  Main                                                                      */
/* -------------------------------------------------------------------------- */

static RoboHubTransport g_transport;
static RoboHubSecure    g_secure;

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Exactly `n` bytes of hex, ignoring a trailing line break; anything else is
 * refused, so a key file that is short or has a stray character is no key. */
static rh_bool hex_bytes(const char* text, rh_u8* out, rh_size n)
{
    rh_size i = 0u;
    for (; i < n; i++) {
        int hi = hex_digit(text[2u * i]);
        int lo = (hi < 0) ? -1 : hex_digit(text[2u * i + 1u]);
        if (lo < 0) {
            return RH_FALSE;
        }
        out[i] = (rh_u8)((hi << 4) | lo);
    }
    const char* rest = text + 2u * n;
    while (*rest == '\n' || *rest == '\r') {
        rest++;
    }
    return *rest == '\0';
}

static rh_u32 node_now_ms(void)
{
    RoboHubTimer* timer = robohub_get_timer("system_timer");
    return (timer != NULL && timer->get_millis != NULL) ? (rh_u32)timer->get_millis(timer) : 0u;
}

/* The board's id and key: the build's, else the environment's. Answers whether
 * the link is sealed; a key named but unreadable stops the node rather than
 * letting it fall back to a plain link. */
static int secure_link(void)
{
    rh_u8 uid[RH_SECURE_UID_SIZE];
    rh_u8 key[RH_SECURE_KEY_SIZE];
#if defined(RH_SECURE_PROVISIONED)
    memcpy(uid, RH_DEVICE_UID, sizeof(uid));
    memcpy(key, RH_DEVICE_KEY, sizeof(key));
#else
    const char* uid_text = env_str("RH_SECURE_UID", "");
    const char* key_path = env_str("RH_SECURE_KEY_FILE", "");
    if (uid_text[0] == '\0' && key_path[0] == '\0') {
        return 0;
    }
    char key_text[80] = { 0 };
    FILE* file = (key_path[0] != '\0') ? fopen(key_path, "r") : NULL;
    if (file != NULL) {
        size_t got = fread(key_text, 1u, sizeof(key_text) - 1u, file);
        key_text[got] = '\0';
        fclose(file);
    }
    if (!hex_bytes(uid_text, uid, sizeof(uid)) || !hex_bytes(key_text, key, sizeof(key))) {
        emit("robohub-device-node: RH_SECURE_UID or RH_SECURE_KEY_FILE is not a board id and key\n");
        return -1;
    }
#endif
    if (ROBOHUB_FAILED(robohub_secure_init(&g_secure, uid, key, robohub_platform_nonce, node_now_ms)) ||
        ROBOHUB_FAILED(robohub_transport_secure(&g_transport, &g_secure))) {
        return -1;
    }
    memset(key, 0, sizeof(key));
    return 1;
}

int main(void)
{
    if (robohub_platform_init() != ROBOHUB_OK) {
        return 1;
    }

    RoboHubDeviceId id = {
        .area_id = (rh_u16)env_u32("AREA", 1u),
        .node_id = (rh_u16)env_u32("NODE", 10u),
    };

    const char* device = env_str("RH_DEVICE", "growatt");
    RoboHubInverter* inverter = (strcmp(device, "hoymiles") == 0)
        ? open_hoymiles(id)
        : open_growatt(id);
    if (inverter == NULL) {
        emit("robohub-device-node: the device could not be opened\n");
        return 1;
    }

    /* The link to the gateway: a UART the gateway is wired to, or a TCP socket. */
    if (strcmp(env_str("RH_NODE_LINK", "uart"), "tcp") == 0) {
        g_link.tcp = RH_TRUE;
        g_link.socket = robohub_socket_create(ROBOHUB_SOCKET_TCP);
        if (g_link.socket == NULL ||
            ROBOHUB_FAILED(robohub_socket_connect(g_link.socket,
                                                  env_str("RH_GATEWAY_HOST", "127.0.0.1"),
                                                  (rh_u16)env_u32("RH_GATEWAY_PORT", 9000u)))) {
            emit("robohub-device-node: the gateway socket could not be opened\n");
            return 1;
        }
    } else {
        g_link.tcp = RH_FALSE;
        g_link.uart = robohub_get_uart(env_str("RH_NODE_UART", "uart0"));
        if (g_link.uart == NULL) {
            emit("robohub-device-node: the link UART was not registered\n");
            return 1;
        }
        if (g_link.uart->init != NULL) {
            (void)g_link.uart->init(g_link.uart, 115200u);
        }
    }

    RoboHubTransportPhy phy = { .write = link_write, .context = &g_link };
    if (ROBOHUB_FAILED(robohub_transport_init(&g_transport, &phy))) {
        return 1;
    }
    int sealed = secure_link();
    if (sealed < 0) {
        return 1;
    }
    emit(sealed > 0 ? "robohub-device-node: link authenticated\n" : "robohub-device-node: link plain\n");

    RoboHubTimer* timer = robohub_get_timer("system_timer");
    rh_u32 interval = env_u32("INTERVAL_MS", 5000u);

    emit("robohub-device-node: ");
    emit(device);
    emit(" in area ");
    emit_u32(id.area_id);
    emit(", node ");
    emit_u32(id.node_id);
    emit("\n");

    for (;;) {
        RoboHubPowerReading reading;
        if (inverter->read(inverter, &reading) == ROBOHUB_OK) {
            rh_u32 now = (timer != NULL && timer->get_millis != NULL)
                ? (rh_u32)timer->get_millis(timer)
                : 0u;
            (void)publish(&g_transport, &reading, now);
            emit("robohub-device-node: read pv=");
            emit_u32((rh_u32)reading.pv_power_w);
            emit(" W\n");
        } else {
            emit("robohub-device-node: the device did not answer\n");
        }

        /* Wait out the interval reading the link: the gateway's session answer
         * and any command arrive here, and a sealed link needs the former
         * before its next reading can be sent. */
        rh_u32 waited = 0u;
        while (waited < interval) {
            rh_u8 incoming[128];
            rh_size got = link_read(&g_link, incoming, sizeof(incoming));
            if (got > 0u) {
                robohub_transport_feed(&g_transport, incoming, got);
                continue;
            }
            if (timer == NULL || timer->delay_ms == NULL) {
                break;
            }
            timer->delay_ms(timer, 20u);
            waited += 20u;
        }
    }
}
