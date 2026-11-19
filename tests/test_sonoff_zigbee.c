/*
 * test_sonoff_zigbee.c — the Sonoff S60ZBTPF's manifest, applied to the messages
 * Zigbee2MQTT documents for the plug, as Mesh applies it.
 *
 * The plug has no C driver (ADR-0003): its manifest is the whole integration, so
 * the manifest's lines are what is tested. The messages are Zigbee2MQTT 2.x's for
 * the S60ZBTPF (its device page and the SONOFF converter), under the prefix Mesh
 * gives a Zigbee device, zigbee/<mesh>/<coordinator>/<address>:
 *
 *   <prefix>                the state: state ON/OFF, power, voltage, current,
 *                           energy, energy_today, ... in W, V, A and kWh
 *   <prefix>/availability   {"state":"online"} or {"state":"offline"}
 *   <prefix>/set            what the switch is told: {"state":"ON"} / {"state":"OFF"}
 *
 * The field lines are applied as the contract between this collection and Mesh
 * says (ADR-0001, DEVS-0009): a member a message does not carry leaves its field
 * as it was, `onoff` reads ON/OFF in any case, a value the type refuses keeps
 * the last one. The online state is read at `online_path` of a JSON payload and
 * compared with `online_up` letter for letter, unreachable when the payload is
 * not JSON or lacks the path (robohub-mesh MESH-0034); an action's payload has
 * {on} replaced by true/false and {ONOFF} by ON/OFF (MESH-0035). Those rules are
 * written here from the contract, not taken from Mesh's code.
 *
 * Run with no argument, every test runs (the host plan); run with a test's name,
 * only that one (the per-test CTest cases tests/CMakeLists.txt registers).
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "robohub/protocols/json.h"

#ifndef S60ZB_MANIFEST
#error "build with -DS60ZB_MANIFEST=\"<path to devices/plug/sonoff-s60zbtpf/module.yml>\""
#endif
#ifndef S60ZB_DOCS
#error "build with -DS60ZB_DOCS=\"<path to devices/plug/sonoff-s60zbtpf/docs/doc/index.md>\""
#endif

static int failures;

#define CHECK(cond, what) do { \
    if (cond) { printf("[PASS] %s\n", what); } \
    else { printf("[FAIL] %s (%s:%d)\n", what, __FILE__, __LINE__); failures++; } \
} while (0)

static int near(double a, double b) { return fabs(a - b) < 1e-6; }

/* ------------------------------------------------------------------------ */
/*  Zigbee2MQTT's documents                                                 */
/* ------------------------------------------------------------------------ */

#define PREFIX "zigbee/home/zb1/plug-desk"

static const char* STATE_ON =
    "{\"state\":\"ON\",\"power\":12.5,\"voltage\":231,\"current\":0.06,\"energy\":3.42,"
    "\"energy_today\":0.12,\"energy_yesterday\":0.4,\"energy_month\":2.1,\"linkquality\":120}";

/* Off: the plug forces power and current to 0, and says nothing of voltage. */
static const char* STATE_OFF =
    "{\"state\":\"OFF\",\"power\":0,\"current\":0,\"energy\":3.42}";

static const char* ONLINE  = "{\"state\":\"online\"}";
static const char* OFFLINE = "{\"state\":\"offline\"}";

/* ------------------------------------------------------------------------ */
/*  The manifest, read as Mesh reads it                                     */
/* ------------------------------------------------------------------------ */

#define MAX_LINES 16

typedef struct {
    char text[MAX_LINES][128];
    int count;
} Lines;

typedef struct {
    rh_bool on;
    rh_bool have_on;
    double number[8];
    rh_bool have[8];
    char names[8][32];
    int fields;
} Fields;

static char* read_text(const char* path, char* text, size_t cap)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    size_t n = fread(text, 1, cap - 1u, f);
    fclose(f);
    text[n] = '\0';
    return text;
}

static const char* read_manifest(void)
{
    static char text[32768];
    return read_text(S60ZB_MANIFEST, text, sizeof(text));
}

/* The quoted items of a top-level list or of a list nested one level, starting
 * at the line `key:`. */
static void list_items(const char* manifest, const char* key, Lines* out)
{
    out->count = 0;
    char header[64];
    snprintf(header, sizeof(header), "\n%s:\n", key);
    const char* p = strstr(manifest, header);
    if (p == NULL) {
        return;
    }
    p += strlen(header);
    while (*p != '\0') {
        const char* end = strchr(p, '\n');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        const char* item = p;
        while (*item == ' ') {
            item++;
        }
        if (strncmp(item, "- \"", 3) != 0) {
            break;
        }
        item += 3;
        const char* close = memchr(item, '"', len - (size_t)(item - p));
        if (close != NULL && out->count < MAX_LINES) {
            snprintf(out->text[out->count], sizeof(out->text[0]), "%.*s",
                     (int)(close - item), item);
            out->count++;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
}

/* The scalar value of `key:` (indented as given), quotes removed. */
static rh_bool scalar(const char* manifest, const char* key, char* out, size_t cap)
{
    char header[64];
    snprintf(header, sizeof(header), "\n%s: ", key);
    const char* p = strstr(manifest, header);
    if (p == NULL) {
        return RH_FALSE;
    }
    p += strlen(header);
    const char* end = strchr(p, '\n');
    size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
    if (len >= 2u && (p[0] == '"' || p[0] == '\'') && p[len - 1u] == p[0]) {
        p++;
        len -= 2u;
    }
    snprintf(out, cap, "%.*s", (int)len, p);
    return RH_TRUE;
}

static int split(char* line, char* parts[6])
{
    int n = 0;
    parts[n++] = line;
    for (char* c = line; *c != '\0' && n < 6; c++) {
        if (*c == '|') {
            *c = '\0';
            parts[n++] = c + 1;
        }
    }
    return n;
}

/* The contract's onoff type: ON/OFF in any letter case, true/false, 1/0. */
static rh_bool contract_onoff(const char* word, rh_bool* on)
{
    char w[16];
    size_t n = 0;
    while (*word == ' ' || *word == '\n' || *word == '\r' || *word == '\t') {
        word++;
    }
    for (; word[n] != '\0' && n + 1u < sizeof(w); n++) {
        w[n] = word[n];
    }
    while (n > 0u && (w[n - 1u] == ' ' || w[n - 1u] == '\n' || w[n - 1u] == '\r')) {
        n--;
    }
    w[n] = '\0';
    if (strcmp(w, "true") == 0 || strcmp(w, "1") == 0 ||
        ((w[0] == 'o' || w[0] == 'O') && (w[1] == 'n' || w[1] == 'N') && w[2] == '\0')) {
        *on = RH_TRUE;
        return RH_TRUE;
    }
    if (strcmp(w, "false") == 0 || strcmp(w, "0") == 0 ||
        ((w[0] == 'o' || w[0] == 'O') && (w[1] == 'f' || w[1] == 'F') &&
         (w[2] == 'f' || w[2] == 'F') && w[3] == '\0')) {
        *on = RH_FALSE;
        return RH_TRUE;
    }
    return RH_FALSE;
}

static int field_slot(Fields* f, const char* name)
{
    for (int i = 0; i < f->fields; i++) {
        if (strcmp(f->names[i], name) == 0) {
            return i;
        }
    }
    snprintf(f->names[f->fields], sizeof(f->names[0]), "%s", name);
    return f->fields++;
}

/* Apply every field line to one message: a field whose path the message does
 * not carry keeps its value; `$` is the whole payload, for a payload that is
 * not JSON; a JSON path reads only a JSON payload. */
static void apply_fields(const Lines* lines, const char* payload, Fields* f)
{
    rh_bool json = payload[0] == '{';
    for (int i = 0; i < lines->count; i++) {
        char line[128];
        snprintf(line, sizeof(line), "%s", lines->text[i]);
        char* parts[6];
        if (split(line, parts) != 6) {
            continue;
        }
        const char* name = parts[1];
        const char* type = parts[3];
        const char* path = parts[4];
        double scale = atof(parts[5]);
        rh_bool whole = strcmp(path, "$") == 0;
        if (whole == json) {
            continue;                         /* not carried by this message */
        }
        if (strcmp(type, "onoff") == 0) {
            char word[32];
            rh_bool on = RH_FALSE;
            if (whole) {
                snprintf(word, sizeof(word), "%s", payload);
            } else if (!robohub_json_string(payload, path, word, sizeof(word))) {
                continue;
            }
            if (contract_onoff(word, &on)) {
                f->on = on;
                f->have_on = RH_TRUE;
            }
            continue;
        }
        rh_f64 value = 0;
        if (!whole && strcmp(type, "f32") == 0 && robohub_json_number(payload, path, &value)) {
            int slot = field_slot(f, name);
            f->number[slot] = value * scale;
            f->have[slot] = RH_TRUE;
        }
    }
}

static double field_value(Fields* f, const char* name, rh_bool* have)
{
    int slot = field_slot(f, name);
    *have = f->have[slot];
    return f->number[slot];
}

/* The online state, as MESH-0034 says: with a path, the payload is read as JSON
 * and the string at the path compared with online_up letter for letter; not
 * JSON, or no such member, is unreachable; nothing said yet is "". Without a
 * path the whole payload is compared (MESH-0032). */
static const char* online_state(const char* payload, const char* path, const char* up)
{
    const char* word = up[0] != '\0' ? up : "true";
    while (*payload == ' ' || *payload == '\n' || *payload == '\r' || *payload == '\t') {
        payload++;
    }
    if (payload[0] == '\0') {
        return "";
    }
    if (path[0] == '\0') {
        return strcmp(payload, word) == 0 ? "online" : "unreachable";
    }
    if (payload[0] != '{') {
        return "unreachable";
    }
    char said[64];
    if (!robohub_json_string(payload, path, said, sizeof(said))) {
        return "unreachable";
    }
    return strcmp(said, word) == 0 ? "online" : "unreachable";
}

/* An action's template filled as MESH-0035 says: {prefix}, {on} as true/false
 * and {ONOFF} as ON/OFF. */
static void fill(const char* template_text, rh_bool on, char* out, size_t cap)
{
    size_t n = 0;
    const char* p = template_text;
    while (*p != '\0' && n + 1u < cap) {
        const char* with = NULL;
        size_t skip = 0;
        if (strncmp(p, "{prefix}", 8) == 0) {
            with = PREFIX;
            skip = 8;
        } else if (strncmp(p, "{ONOFF}", 7) == 0) {
            with = on ? "ON" : "OFF";
            skip = 7;
        } else if (strncmp(p, "{on}", 4) == 0) {
            with = on ? "true" : "false";
            skip = 4;
        }
        if (with != NULL) {
            size_t len = strlen(with);
            if (n + len >= cap) {
                break;
            }
            memcpy(out + n, with, len);
            n += len;
            p += skip;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = '\0';
}

/* The manifest's lines, read once per test. */
typedef struct {
    Lines fields;
    char online_path[32];
    char online_up[32];
    char action_topic[128];
    char action_payload[128];
} Manifest;

static rh_bool manifest_load(Manifest* m)
{
    memset(m, 0, sizeof(*m));
    const char* text = read_manifest();
    if (text == NULL) {
        CHECK(0, "the module's manifest is read");
        return RH_FALSE;
    }
    list_items(text, "telemetry_mqtt_fields", &m->fields);
    scalar(text, "  online_path", m->online_path, sizeof(m->online_path));
    scalar(text, "  online_up", m->online_up, sizeof(m->online_up));
    scalar(text, "    mqtt_topic", m->action_topic, sizeof(m->action_topic));
    scalar(text, "    mqtt_payload", m->action_payload, sizeof(m->action_payload));
    return RH_TRUE;
}

/* ------------------------------------------------------------------------ */
/*  The tests                                                               */
/* ------------------------------------------------------------------------ */

// @verifies DEVS-PLUG-0012
static void test_s60zb_manifest_declares(void)
{
    const char* text = read_manifest();
    if (text == NULL) {
        CHECK(0, "the module's manifest is read");
        return;
    }
    char value[128];
    Lines topics;
    list_items(text, "  status_topics", &topics);
    CHECK(topics.count == 1 && strcmp(topics.text[0], "{prefix}") == 0,
          "the one status topic is {prefix} itself");
    CHECK(strstr(text, "\n  status_topic:") == NULL, "status_topic and status_topics are not both given");
    CHECK(scalar(text, "  online_topic", value, sizeof(value)) &&
          strcmp(value, "{prefix}/availability") == 0, "the online topic is {prefix}/availability");
    CHECK(scalar(text, "  online_path", value, sizeof(value)) && strcmp(value, "state") == 0 &&
          scalar(text, "  online_up", value, sizeof(value)) && strcmp(value, "online") == 0,
          "online is the member state, compared with online");
    CHECK(scalar(text, "  model", value, sizeof(value)) && strcmp(value, "S60ZBTPF") == 0 &&
          scalar(text, "  manufacturer", value, sizeof(value)) && strcmp(value, "SONOFF") == 0 &&
          scalar(text, "  transports", value, sizeof(value)) && strcmp(value, "[zigbee]") == 0 &&
          scalar(text, "  firmware", value, sizeof(value)) && strcmp(value, "stock") == 0,
          "the device block names the S60ZBTPF by SONOFF, over Zigbee, on stock firmware");
    const char* zigbee = strstr(text, "\nzigbee:\n");
    CHECK(zigbee != NULL && scalar(zigbee, "  model", value, sizeof(value)) &&
          strcmp(value, "S60ZBTPF") == 0 && scalar(zigbee, "  vendor", value, sizeof(value)) &&
          strcmp(value, "SONOFF") == 0,
          "the zigbee block names the model and vendor Zigbee2MQTT reports");
    static const char* const signals[] = { "switch", "power", "voltage", "current",
                                           "energy_today", "energy_total" };
    int all = 1;
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        char needle[48];
        snprintf(needle, sizeof(needle), "\n  - id: %s\n", signals[i]);
        if (strstr(text, needle) == NULL) {
            printf("  no signal %s\n", signals[i]);
            all = 0;
        }
    }
    CHECK(all, "the signals are the S60TPF's: switch, power, voltage, current, energy today and total");
    CHECK(strstr(text, "password") == NULL && strstr(text, "secret") == NULL &&
          strstr(text, "network_key") == NULL, "the manifest carries no credential");
}

// @verifies DEVS-PLUG-0013, DEVS-PLUG-0012
static void test_s60zb_state_on_and_off(void)
{
    Manifest m;
    if (!manifest_load(&m)) {
        return;
    }
    CHECK(m.fields.count == 6, "the manifest declares six MQTT field lines");
    Fields f;
    memset(&f, 0, sizeof(f));
    rh_bool have = RH_FALSE;

    apply_fields(&m.fields, STATE_ON, &f);
    CHECK(f.have_on && f.on, "state ON reads as on");
    double power = field_value(&f, "power_w", &have);
    CHECK(have && near(power, 12.5), "power is 12.5 W");
    double voltage = field_value(&f, "voltage_v", &have);
    CHECK(have && near(voltage, 231.0), "voltage is 231 V");
    double current = field_value(&f, "current_a", &have);
    CHECK(have && near(current, 0.06), "current is 0.06 A");
    double today = field_value(&f, "energy_today_kwh", &have);
    CHECK(have && near(today, 0.12), "energy today is 0.12 kWh, from energy_today");
    double total = field_value(&f, "energy_total_kwh", &have);
    CHECK(have && near(total, 3.42), "energy total is 3.42 kWh, from energy (not energy_month or yesterday)");
    CHECK(!f.have[field_slot(&f, "state")], "no field line reads the switch as a number");

    apply_fields(&m.fields, STATE_OFF, &f);
    CHECK(f.have_on && !f.on, "state OFF reads as off");
    CHECK(near(field_value(&f, "power_w", &have), 0.0) && have, "power reads 0 while off");
    CHECK(near(field_value(&f, "current_a", &have), 0.0) && have, "current reads 0 while off");
    CHECK(near(field_value(&f, "voltage_v", &have), 231.0) && have,
          "a message without voltage leaves the voltage as it was");
    CHECK(near(field_value(&f, "energy_today_kwh", &have), 0.12) && have,
          "a message without energy_today leaves it as it was");
    CHECK(near(field_value(&f, "energy_total_kwh", &have), 3.42) && have,
          "energy total is read again from the off message");

    apply_fields(&m.fields, "{\"state\":\"on\"}", &f);
    CHECK(f.on, "state in lower case reads as on (onoff takes any case)");
}

// @verifies DEVS-PLUG-0013
static void test_s60zb_availability(void)
{
    Manifest m;
    if (!manifest_load(&m)) {
        return;
    }
    CHECK(strcmp(online_state(ONLINE, m.online_path, m.online_up), "online") == 0,
          "{\"state\":\"online\"} on availability is online");
    CHECK(strcmp(online_state(OFFLINE, m.online_path, m.online_up), "unreachable") == 0,
          "{\"state\":\"offline\"} on availability is unreachable");
    CHECK(strcmp(online_state("", m.online_path, m.online_up), "") == 0,
          "an availability topic that has said nothing says nothing");
    CHECK(strcmp(online_state("online", m.online_path, m.online_up), "unreachable") == 0,
          "the bare word online (Zigbee2MQTT 1.x's legacy payload) is unreachable: it is not JSON");
    CHECK(strcmp(online_state("{\"availability\":\"online\"}", m.online_path, m.online_up),
                 "unreachable") == 0,
          "a JSON payload without the member state is unreachable");
    CHECK(strcmp(online_state("{\"state\":\"Online\"}", m.online_path, m.online_up),
                 "unreachable") == 0,
          "the online word is compared letter for letter");
    /* Without online_path the whole payload would be compared: the JSON never
     * equals the word, which is why the manifest names the path. */
    CHECK(strcmp(online_state(ONLINE, "", m.online_up), "unreachable") == 0,
          "without online_path the JSON payload would never read as online");
}

// @verifies DEVS-PLUG-0013
static void test_s60zb_malformed(void)
{
    Manifest m;
    if (!manifest_load(&m)) {
        return;
    }
    Fields f;
    memset(&f, 0, sizeof(f));
    rh_bool have = RH_FALSE;
    apply_fields(&m.fields, STATE_ON, &f);

    apply_fields(&m.fields, "garbage", &f);
    apply_fields(&m.fields, "ON", &f);
    apply_fields(&m.fields, "{\"state\":\"MAYBE\",\"power\":\"n/a\",\"voltage\":null}", &f);
    apply_fields(&m.fields, "{\"power\":", &f);
    apply_fields(&m.fields, "{\"state\":true}", &f);
    CHECK(f.have_on && f.on, "a payload that is not JSON, a state that is not a word, or a boolean leaves the switch on");
    CHECK(near(field_value(&f, "power_w", &have), 12.5) &&
          near(field_value(&f, "voltage_v", &have), 231.0) &&
          near(field_value(&f, "current_a", &have), 0.06) &&
          near(field_value(&f, "energy_total_kwh", &have), 3.42),
          "the malformed messages left every figure as it was");
    apply_fields(&m.fields, "{\"linkquality\":96,\"power_on_behavior\":\"previous\"}", &f);
    CHECK(f.on && near(field_value(&f, "power_w", &have), 12.5),
          "a message with none of the plug's readings changes nothing");
}

// @verifies DEVS-PLUG-0013, DEVS-PLUG-0012
static void test_s60zb_switch_action(void)
{
    Manifest m;
    if (!manifest_load(&m)) {
        return;
    }
    char topic[192];
    char payload[192];
    fill(m.action_topic, RH_TRUE, topic, sizeof(topic));
    CHECK(strcmp(topic, PREFIX "/set") == 0, "the switch is told on <prefix>/set");
    fill(m.action_payload, RH_TRUE, payload, sizeof(payload));
    CHECK(strcmp(payload, "{\"state\":\"ON\"}") == 0, "switching on publishes {\"state\":\"ON\"}");
    fill(m.action_payload, RH_FALSE, payload, sizeof(payload));
    CHECK(strcmp(payload, "{\"state\":\"OFF\"}") == 0, "switching off publishes {\"state\":\"OFF\"}");
    CHECK(strstr(m.action_payload, "{on}") == NULL,
          "the payload carries no {on}: Zigbee2MQTT refuses a JSON boolean");

    /* What the plug answers with must read back as the state that was asked. */
    Fields f;
    memset(&f, 0, sizeof(f));
    fill(m.action_payload, RH_TRUE, payload, sizeof(payload));
    apply_fields(&m.fields, payload, &f);
    CHECK(f.have_on && f.on, "the set payload for on, read by the field lines, is on");
    fill(m.action_payload, RH_FALSE, payload, sizeof(payload));
    apply_fields(&m.fields, payload, &f);
    CHECK(f.have_on && !f.on, "the set payload for off, read by the field lines, is off");
}

/* What a person pairing the plug must be told; the page is checked for it, so a
 * fact cannot be dropped from it unnoticed. */
// @verifies DEVS-PLUG-0014
static void test_s60zb_docs_cover_pairing(void)
{
    static char text[32768];
    const char* page = read_text(S60ZB_DOCS, text, sizeof(text));
    CHECK(page != NULL, "the module's documentation is read");
    if (page == NULL) {
        return;
    }
    static const struct { const char* text; const char* what; } musts[] = {
        { "about 5 seconds, until its LED\n   blinks", "the long press until the LED blinks" },
        { "open joining", "joining opened in Mesh" },
        { "at most every 10 s, on change", "power and current every 10 s on change" },
        { "every 1 to 60 minutes", "the day's energy every 1 to 60 minutes" },
        { "read when the plug is paired, not reported afterwards", "voltage read at pairing only" },
        { "Power and current read 0 while the plug is off", "0 while off" },
        { "no reflash and no eWeLink account", "no reflash and no eWeLink account" },
    };
    int all = 1;
    for (size_t i = 0; i < sizeof(musts) / sizeof(musts[0]); i++) {
        if (strstr(page, musts[i].text) == NULL) {
            printf("  the page does not say: %s\n", musts[i].what);
            all = 0;
        }
    }
    CHECK(all, "the page covers pairing, what is reported and how often, and no reflash");
}

/* ------------------------------------------------------------------------ */

static const struct {
    const char* name;
    void (*run)(void);
} tests[] = {
    { "test_s60zb_manifest_declares", test_s60zb_manifest_declares },
    { "test_s60zb_state_on_and_off", test_s60zb_state_on_and_off },
    { "test_s60zb_availability", test_s60zb_availability },
    { "test_s60zb_malformed", test_s60zb_malformed },
    { "test_s60zb_switch_action", test_s60zb_switch_action },
    { "test_s60zb_docs_cover_pairing", test_s60zb_docs_cover_pairing },
};

int main(int argc, char** argv)
{
    int ran = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (argc > 1 && strcmp(argv[1], tests[i].name) != 0) {
            continue;
        }
        tests[i].run();
        ran++;
    }
    if (ran == 0) {
        printf("no test named %s\n", argc > 1 ? argv[1] : "(none)");
        return 2;
    }
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all sonoff zigbee checks passed\n");
    return 0;
}
