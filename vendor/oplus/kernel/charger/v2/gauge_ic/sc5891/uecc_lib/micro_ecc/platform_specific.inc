// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2025 Oplus. All rights reserved.
 */

#ifndef _UECC_PLATFORM_SPECIFIC_H_
#define _UECC_PLATFORM_SPECIFIC_H_

#include "types.h"

#include <linux/fcntl.h>
#include <linux/unistd.h>
#include <linux/random.h>

static int default_rng(uint8_t *dest, unsigned size)
{
	get_random_bytes(dest, size);
	return true;
}
#define DEFAULT_RNG_DEFINED 1

#endif /* _UECC_PLATFORM_SPECIFIC_H_ */
