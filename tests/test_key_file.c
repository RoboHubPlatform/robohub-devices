/*
 * test_key_file.c — the gateway node's device key file.
 *
 * A host gateway node takes its device key from the file RH_SECURE_KEY_FILE
 * names. The key opens the board's sessions, so the node must refuse a file any
 * other user of the machine can read or write, or one another user owns, and say
 * how to put it right; and it must refuse a file that is not a key. Each case
 * here writes a file in a scratch directory and reads it as the node does.
 *
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "key_file.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

/* The user the node runs as, as key_file.c asks for it: this test's own unless
 * a case pretends to be another user, which is how a file another user owns is
 * made without root. */
static int pretend = 0;
static uid_t pretend_uid;

uid_t geteuid(void)
{
    return pretend ? pretend_uid : getuid();
}

static void expect(int condition, const char* what)
{
    if (condition) {
        printf("[PASS] %s\n", what);
    } else {
        printf("[FAIL] %s\n", what);
        failures++;
    }
}

static char dir[256];

static const char* write_file(const char* name, const char* text, mode_t mode)
{
    static char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    (void)unlink(path);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0 || write(fd, text, strlen(text)) != (ssize_t)strlen(text) || close(fd) != 0) {
        perror(path);
        exit(2);
    }
    if (chmod(path, mode) != 0) {
        perror(path);
        exit(2);
    }
    return path;
}

static const rh_u8 expected_key[16] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
};

#define KEY_HEX "00112233445566778899aabbccddeeff"

static RhKeyFileStatus read_key(const char* path, rh_u32* epoch, rh_u8 key[16], char* why, size_t why_size)
{
    memset(key, 0xA5, 16);
    *epoch = 12345u;
    return rh_key_file_read(path, epoch, key, 16, why, why_size);
}

static int key_is_cleared(const rh_u8 key[16])
{
    for (int i = 0; i < 16; i++) {
        if (key[i] != 0) {
            return 0;
        }
    }
    return 1;
}

// @verifies DEVS-NODE-0005, DEVS-NODE-0007
int main(void)
{
    const char* base = getenv("TMPDIR");
    snprintf(dir, sizeof(dir), "%s/robohub-key-file-XXXXXX", (base != NULL && base[0] != '\0') ? base : "/tmp");
    if (mkdtemp(dir) == NULL) {
        perror("mkdtemp");
        return 2;
    }

    rh_u32 epoch;
    rh_u8 key[16];
    char why[1280];
    char path_copy[512];

    /* A good file: owner-only, with and without an epoch. */
    const char* path = write_file("good.key", "3:" KEY_HEX "\n", 0600);
    expect(read_key(path, &epoch, key, why, sizeof(why)) == RH_KEY_FILE_OK, "an owner-only key file is read");
    expect(epoch == 3u && memcmp(key, expected_key, 16) == 0, "its epoch and key are what it holds");
    path = write_file("bare.key", KEY_HEX, 0400);
    expect(read_key(path, &epoch, key, why, sizeof(why)) == RH_KEY_FILE_OK && epoch == 0u &&
               memcmp(key, expected_key, 16) == 0,
           "a read-only owner file with the hex alone is epoch 0");

    /* No file. */
    snprintf(path_copy, sizeof(path_copy), "%s/absent.key", dir);
    expect(read_key(path_copy, &epoch, key, why, sizeof(why)) == RH_KEY_FILE_MISSING, "a missing key file is refused");
    expect(strstr(why, path_copy) != NULL, "and the refusal names the file");
    expect(read_key("", &epoch, key, why, sizeof(why)) == RH_KEY_FILE_MISSING, "an empty path is refused");

    /* Too open: group- or world-readable, or writable by others. */
    const struct { mode_t mode; const char* what; } open_modes[] = {
        { 0644, "a world-readable key file is refused" },
        { 0640, "a group-readable key file is refused" },
        { 0604, "an other-readable key file is refused" },
        { 0620, "a group-writable key file is refused" },
        { 0602, "an other-writable key file is refused" },
    };
    for (size_t i = 0; i < sizeof(open_modes) / sizeof(open_modes[0]); i++) {
        path = write_file("open.key", "3:" KEY_HEX "\n", open_modes[i].mode);
        snprintf(path_copy, sizeof(path_copy), "%s", path);
        RhKeyFileStatus status = read_key(path_copy, &epoch, key, why, sizeof(why));
        expect(status == RH_KEY_FILE_TOO_OPEN && key_is_cleared(key) && epoch == 0u, open_modes[i].what);
    }
    char fix[600];
    snprintf(fix, sizeof(fix), "chmod 600 %s", path_copy);
    expect(strstr(why, fix) != NULL, "the refusal says how to fix it: chmod 600 <file>");

    /* Not a regular file. */
    expect(read_key(dir, &epoch, key, why, sizeof(why)) == RH_KEY_FILE_NOT_REGULAR, "a directory is no key file");

    /* Another user's file: the node pretends to run as someone else. */
    path = write_file("theirs.key", "3:" KEY_HEX "\n", 0600);
    snprintf(path_copy, sizeof(path_copy), "%s", path);
    pretend = 1;
    pretend_uid = getuid() + 1u;
    expect(read_key(path_copy, &epoch, key, why, sizeof(why)) == RH_KEY_FILE_NOT_OWNER && key_is_cleared(key),
           "a key file another user owns is refused");
    pretend = 0;
    expect(strstr(why, "chmod 600") != NULL && strstr(why, "chown") != NULL,
           "and the refusal says to give it to the node's user, owner-only");

    /* Wrong content. */
    const struct { const char* text; const char* what; } bad[] = {
        { "3:00112233445566778899aabbccddee\n", "a key one byte short is refused" },
        { "3:00112233445566778899aabbccddeeff00\n", "a key one byte long is refused" },
        { "", "an empty key file is refused" },
        { "3:00112233445566778899aabbccddeefg\n", "a key with a stray character is refused" },
        { ":00112233445566778899aabbccddeeff\n", "an empty epoch is refused" },
        { "x:00112233445566778899aabbccddeeff\n", "an epoch that is not a number is refused" },
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        path = write_file("bad.key", bad[i].text, 0600);
        expect(read_key(path, &epoch, key, why, sizeof(why)) == RH_KEY_FILE_BAD_CONTENT && key_is_cleared(key),
               bad[i].what);
    }
    expect(strstr(why, "does not hold a device key") != NULL, "and the refusal says what a key file holds");

    /* A file longer than any key file is not read as one. */
    char longer[200];
    memset(longer, 'a', sizeof(longer) - 1u);
    longer[sizeof(longer) - 1u] = '\0';
    path = write_file("long.key", longer, 0600);
    expect(read_key(path, &epoch, key, why, sizeof(why)) == RH_KEY_FILE_BAD_CONTENT, "an over-long key file is refused");

    const char* names[] = { "good.key", "bare.key", "open.key", "theirs.key", "bad.key", "long.key" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path_copy, sizeof(path_copy), "%s/%s", dir, names[i]);
        (void)unlink(path_copy);
    }
    (void)rmdir(dir);

    printf("%s\n", failures == 0 ? "all key-file tests passed" : "key-file tests FAILED");
    return failures == 0 ? 0 : 1;
}
