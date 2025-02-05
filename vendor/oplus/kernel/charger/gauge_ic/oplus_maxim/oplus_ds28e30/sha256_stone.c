// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#include "ucl_hash.h"
#ifdef HASH_SHA256

#include "ucl_config.h"
#include "ucl_types.h"
#include "ucl_defs.h"
#include "ucl_retdefs.h"

#define ROTR(x, n)   (((x) >> (n)) | ((x) << (32 - n)))
#define SHR(x, n)    ((x) >> (n))
#define CH(x, y, z)   ((z) ^ ((x) & ((y) ^ (z))))
#define MAJ(x, y, z)  (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x)   (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define SIGMA1(x)   (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define GAMMA0(x)   (ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define GAMMA1(x)   (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))
#define ROUND(a, b, c, e, f, g, h, i)				\
	t1 = h + SIGMA1(e) + CH(e, f, g) + k[i] + w[i];	\
	t2 = SIGMA0(a) + MAJ(a, b, c);

static const u32 k[64] = { 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
			   0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
			   0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
			   0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6,
			   0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			   0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3,
			   0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138,
			   0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e,
			   0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
			   0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
			   0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
			   0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814,
			   0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

void sha256_stone(u32 hash[8], u32 stone[16])
{
	int i;
	u32 w[64];
	u32 a, b, c, d, e, f, g, h, t1, t2;
	for (i = 0; i < 16; i++)
		w[i] = stone[i];

	for (; i < 64; i++)
		w[i] = GAMMA1(w[i - 2]) + w[i - 7] + GAMMA0(w[i - 15]) + w[i - 16];
	a = hash[0];
	b = hash[1];
	c = hash[2];
	d = hash[3];
	e = hash[4];
	f = hash[5];
	g = hash[6];
	h = hash[7];

	for (i = 0; i < 64; i++) {
		ROUND(a, b, c, e, f, g, h, i);
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	hash[0] += a;
	hash[1] += b;
	hash[2] += c;
	hash[3] += d;
	hash[4] += e;
	hash[5] += f;
	hash[6] += g;
	hash[7] += h;
}

#endif /* HASH_SHA256 */
