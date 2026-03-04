// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#ifndef _ECC_GENERATE_KEY_H
#define _ECC_GENERATE_KEY_H

#include "ecdsa_generic_api.h"
#include "ucl_defs.h"
#include "ucl_sha256.h"
#include "ucl_rng.h"

#ifndef GEN_ECC_KEY
int deep_cover_generate_publickey(u8 *private_key, u8 *pubkey_x, u8 *pubkey_y);
#endif /* GEN_ECC_KEY */
#endif /* _ECC_GENERATE_KEY_H */
