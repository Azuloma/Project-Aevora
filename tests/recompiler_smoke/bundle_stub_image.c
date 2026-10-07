/* SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* A valid shared image that is not a generated one: it exports a single symbol that is
   none of the image entry points. Built under the shared image's file name so the bundle
   loader finds a loadable DLL that lacks its entry points (bundle_host case
   dll-missing-export). */

#ifdef _WIN32
#define STUB_EXPORT __declspec(dllexport)
#else
#define STUB_EXPORT __attribute__((visibility("default")))
#endif

STUB_EXPORT int bundle_stub_marker(void) {
    return 1;
}
