// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads a generated shared image at run time and checks the identity it reports
// through the recomp_image_identity_v1 contract (core/recompiler/
// recomp_image_identity.h), the way a split-export launcher will.
//
//   identity_host <image path> <expected name> <expected bundle id, 64 hex digits>
//
// Exit 0 when the image reports the expected name and the expected 32-byte
// digest, 1 on any mismatch or load failure. On Windows the entry point is wide,
// so the image path reaches LoadLibraryW exactly as the process was given it,
// whatever the ANSI code page.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "core/recompiler/recomp_image_identity.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define IDENTITY_V1_FILLED 1u

#ifdef _WIN32
typedef wchar_t PathChar;
#define PATH_ARG_FORMAT "%ls"
#else
typedef char PathChar;
#define PATH_ARG_FORMAT "%s"
#endif

// Decodes 2*RECOMP_BUNDLE_ID_BYTES lowercase hex digits; returns 0 on bad input.
static int DecodeBundleHex(const char* hex, uint8_t* bytes) {
    if (strlen(hex) != 2u * RECOMP_BUNDLE_ID_BYTES) {
        return 0;
    }
    for (size_t i = 0; i < RECOMP_BUNDLE_ID_BYTES; ++i) {
        char pair[3] = {hex[2 * i], hex[2 * i + 1], 0};
        char* end = NULL;
        const unsigned long value = strtoul(pair, &end, 16);
        if (*end != 0) {
            return 0;
        }
        bytes[i] = (uint8_t)value;
    }
    return 1;
}

static int CheckImage(const PathChar* image_path, const char* expected_name,
                      const char* expected_bundle_hex) {
    uint8_t expected_bundle[RECOMP_BUNDLE_ID_BYTES];
    if (!DecodeBundleHex(expected_bundle_hex, expected_bundle)) {
        fprintf(stderr, "identity_host: '%s' is not a %d-byte hex bundle id\n",
                expected_bundle_hex, RECOMP_BUNDLE_ID_BYTES);
        return 1;
    }
#ifdef _WIN32
    HMODULE image = LoadLibraryW(image_path);
    if (!image) {
        fprintf(stderr, "identity_host: cannot load '" PATH_ARG_FORMAT "' (error %lu)\n",
                image_path, (unsigned long)GetLastError());
        return 1;
    }
    RecompImageIdentityV1Fn identity =
        (RecompImageIdentityV1Fn)(void*)GetProcAddress(image, RECOMP_IMAGE_IDENTITY_V1_SYMBOL);
#else
    void* image = dlopen(image_path, RTLD_NOW);
    if (!image) {
        fprintf(stderr, "identity_host: cannot load '%s': %s\n", image_path, dlerror());
        return 1;
    }
    RecompImageIdentityV1Fn identity =
        (RecompImageIdentityV1Fn)dlsym(image, RECOMP_IMAGE_IDENTITY_V1_SYMBOL);
#endif
    if (!identity) {
        fprintf(stderr, "identity_host: '" PATH_ARG_FORMAT "' does not export %s\n", image_path,
                RECOMP_IMAGE_IDENTITY_V1_SYMBOL);
        return 1;
    }
    const char* name = NULL;
    const uint8_t* bundle = NULL;
    if (identity(&name, &bundle) != IDENTITY_V1_FILLED || !name || !bundle) {
        fprintf(stderr, "identity_host: '" PATH_ARG_FORMAT "' did not fill the v1 identity\n",
                image_path);
        return 1;
    }
    if (strcmp(name, expected_name) != 0) {
        fprintf(stderr, "identity_host: module name '%s' does not match expected '%s'\n", name,
                expected_name);
        return 1;
    }
    if (memcmp(bundle, expected_bundle, RECOMP_BUNDLE_ID_BYTES) != 0) {
        fprintf(stderr, "identity_host: the bundle id of module '%s' does not match '%s'\n", name,
                expected_bundle_hex);
        return 1;
    }
    printf("identity ok: %s %s\n", name, expected_bundle_hex);
    return 0;
}

#ifdef _WIN32
// Textual arguments are ASCII (module name, hex), so narrowing them is lossless.
static int NarrowAscii(const wchar_t* wide, char* narrow, size_t capacity) {
    size_t i = 0;
    for (; wide[i]; ++i) {
        if (i + 1 >= capacity || wide[i] > 0x7f) {
            return 0;
        }
        narrow[i] = (char)wide[i];
    }
    narrow[i] = 0;
    return 1;
}

int wmain(int argc, wchar_t** argv) {
    char name[256];
    char bundle_hex[256];
    if (argc != 4 || !NarrowAscii(argv[2], name, sizeof name) ||
        !NarrowAscii(argv[3], bundle_hex, sizeof bundle_hex)) {
        fprintf(stderr, "usage: identity_host <image path> <name> <bundle id hex>\n");
        return 1;
    }
    return CheckImage(argv[1], name, bundle_hex);
}
#else
int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: identity_host <image path> <name> <bundle id hex>\n");
        return 1;
    }
    return CheckImage(argv[1], argv[2], argv[3]);
}
#endif
