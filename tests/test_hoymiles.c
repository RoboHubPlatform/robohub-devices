/*
 * test_hoymiles.c — the HTTP client and the Hoymiles device, end to end.
 *
 * A loopback server answers one request with a document shaped like an
 * OpenDTU's, and the device is read through the real HTTP client over a real
 * socket. What it proves is the whole path: connect, request, response, body,
 * JSON field, reading.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "robohub/devices/inverter/hoymiles.h"

static const char* RESPONSE =
    "{\"inverters\":[{\"name\":\"HMS-800\",\"data\":{"
    "\"0\":{\"Power\":{\"v\":411.0}},"
    "\"total\":{\"Power\":{\"v\":823.5},\"YieldDay\":{\"v\":1.75},"
    "\"YieldTotal\":{\"v\":4321.125}}}}]}";

static int g_listen_fd = -1;

static void* serve_one(void* arg)
{
    (void)arg;
    int client = accept(g_listen_fd, NULL, NULL);
    if (client < 0) {
        return NULL;
    }
    char request[512];
    (void)read(client, request, sizeof(request));

    char response[1024];
    int n = snprintf(response, sizeof(response),
                     "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
                     "Content-Length: %zu\r\n\r\n%s",
                     strlen(RESPONSE), RESPONSE);
    if (n > 0) {
        (void)write(client, response, (size_t)n);
    }
    close(client);
    return NULL;
}

static int near(float a, float b)
{
    float d = a - b;
    if (d < 0) d = -d;
    return d < 0.0005f;
}

int main(void)
{
    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int ok = g_listen_fd >= 0;
    if (!ok) {
        printf("Hoymiles FAILED (socket)\n");
        return 1;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    ok = ok && bind(g_listen_fd, (struct sockaddr*)&address, sizeof(address)) == 0;
    ok = ok && listen(g_listen_fd, 1) == 0;

    socklen_t length = sizeof(address);
    ok = ok && getsockname(g_listen_fd, (struct sockaddr*)&address, &length) == 0;
    rh_u16 port = ntohs(address.sin_port);

    RoboHubDeviceId id = { .area_id = 7u, .node_id = 11u };
    RoboHubHoymiles hoymiles;
    ok = ok && robohub_hoymiles_init(&hoymiles, "127.0.0.1", port, NULL, id) == ROBOHUB_OK;

    pthread_t thread;
    ok = ok && pthread_create(&thread, NULL, serve_one, NULL) == 0;

    RoboHubPowerReading reading;
    memset(&reading, 0, sizeof(reading));
    ok = ok && hoymiles.interface.read(&hoymiles.interface, &reading) == ROBOHUB_OK;

    ok = ok && reading.kind == ROBOHUB_DEVICE_INVERTER;
    ok = ok && reading.device.area_id == 7u && reading.device.node_id == 11u;
    ok = ok && near(reading.pv_power_w, 823.5f);
    ok = ok && near(reading.energy_today_kwh, 1.75f);
    ok = ok && near(reading.energy_total_kwh, 4321.125f);
    ok = ok && (reading.valid & ROBOHUB_POWER_PV_POWER) != 0u;
    ok = ok && (reading.valid & ROBOHUB_POWER_ENERGY_TOTAL) != 0u;
    /* A microinverter has no battery: the field is absent, not zero. */
    ok = ok && (reading.valid & ROBOHUB_POWER_BATTERY_SOC) == 0u;

    pthread_join(thread, NULL);
    close(g_listen_fd);

    printf(ok ? "Hoymiles OK\n" : "Hoymiles FAILED\n");
    return ok ? 0 : 1;
}
