/* SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* The identity contract of a split export's shared image (topology v1), usable
   from C and C++. A shared image exports one function, named by
   RECOMP_IMAGE_IDENTITY_V1_SYMBOL, so a launcher can check that the image is the
   module its registry expects, from the same export. The generator
   (arm64_to_c.h) emits it and the launcher and tests resolve it from here. */

#ifndef RECOMP_IMAGE_IDENTITY_H
#define RECOMP_IMAGE_IDENTITY_H

#include <stdint.h>

#define RECOMP_IMAGE_IDENTITY_V1_SYMBOL "recomp_image_identity_v1"

/* The bundle id is the raw SHA-256 digest of the export, not its hex text. */
#define RECOMP_BUNDLE_ID_BYTES 32

/* Returns 1 after filling *canonical_name (the module's NSO name, e.g. "main",
   owned by the image) and *bundle_id (RECOMP_BUNDLE_ID_BYTES bytes, owned by the
   image). Any other return value means the image does not provide the v1
   identity and the output pointers must not be used. */
typedef unsigned (*RecompImageIdentityV1Fn)(const char** canonical_name,
                                            const uint8_t** bundle_id);

#endif /* RECOMP_IMAGE_IDENTITY_H */
