// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#include "ucl_config.h"
#include "ucl_types.h"
#include "ucl_defs.h"
#include "ucl_retdefs.h"
#include "ucl_rng.h"
#include "ucl_hash.h"
#ifdef HASH_SHA256
#include "ucl_sha256.h"
#endif


/* this is not secure for ECDSA signatures,  as being pseudo random number generator */
/* this is for test and demo only */
int ucl_rng_read(u8 *rand, u32 rand_bytelen)
{
	int msgi, j;
	static u8 pseudo[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x00, 0x11, 0x22,
				0x33, 0x44, 0x55, 0x00, 0x11, 0x22, 0x33, 0x44};
	u8 output[32], input[16];
	u8 blocksize;
	blocksize = 16;

	for (msgi = 0;msgi < (int)rand_bytelen;) {
		for (j = 0; j < blocksize; j++)
			input[j] = pseudo[j];
		ucl_sha256(output, input, blocksize);
		for (j = 0; j < blocksize; j++)
			pseudo[j] = output[j];
		for (j = 0; j < blocksize; j++) {
			if (msgi < (int)rand_bytelen) {
				rand[msgi] = output[j];
				msgi++;
			}
		}
	}
	return(rand_bytelen);
}
