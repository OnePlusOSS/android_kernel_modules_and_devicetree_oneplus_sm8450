// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#ifndef __UCL_SHA256_H_
#define __UCL_SHA256_H_

#include "ucl_sha256.h"

/** The main loop of sha256.
 *
 * @param[in,out] hash The intermediate hash, u32[8]
 * @param[in] stone A "Stone" of the padded message,u32[16]
 *
 * @ingroup UCL_SHA256
 * @internal
 */
void sha256_stone(u32 hash[8], u32 stone[16]);

#endif /* __UCL_SHA256_H_ */
