// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#include <linux/slab.h> /* kfree() */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include "ucl_hash.h"
#ifdef HASH_SHA256

#include "deep_cover_coproc.h"
#include "ucl_config.h"
#include "ucl_defs.h"
#include "ucl_retdefs.h"
#include "ucl_types.h"
#include "sha256.h"

static u32 _wsb_b2w(u8 *src)
{
	return ((u32)src[3] | ((u32)src[2] << BYTE_LENGTH_8) |
		((u32)src[1] << BYTE_LENGTH_16) | ((u32)src[0] << BYTE_LENGTH_24));
}

static void _wsb_w2b(u8 *dst, u32 src)
{
	dst[3] = src & BYTE_VALUE_FF;
	src >>= BYTE_VALUE_8;
	dst[2] = src & BYTE_VALUE_FF;
	src >>= BYTE_VALUE_8;
	dst[1] = src & BYTE_VALUE_FF;
	src >>= BYTE_VALUE_8;
	dst[0] = src & BYTE_VALUE_FF;
}

void swapcpy_b2w(u32 *dst, const u8 *src, u32 wordlen)
{
	int i;

	for (i = 0; i < (int)wordlen; i++) {
		dst[i] = _wsb_b2w((u8 *)src);
		src += BYTE_LENGTH_4;
	}
}


void swapcpy_w2b(u8 *dst, const u32 *src, u32 wordlen)
{
	int i;

	for (i = 0; i < (int)wordlen; i++) {
		_wsb_w2b(dst, src[i]);
		dst += BYTE_LENGTH_4;
	}
}

void swapcpy_b2b(u8 *dst, u8 *src, u32 wordlen)
{
	u8 tmp;
	int i;

	for (i = 0; i < (int)wordlen; i++) {
		tmp = src[0];
		dst[0] = src[3];
		dst[3] = tmp;
		tmp = src[1];
		dst[1] = src[2];
		dst[2] = tmp;
		dst += BYTE_LENGTH_4;
		src += BYTE_LENGTH_4;
	}
}

int ucl_sha256_init(struct ucl_sha256_ctx_t *ctx)
{
	if (ctx == NULL)
		return UCL_INVALID_INPUT;
	ctx->state[0] = 0x6A09E667;
	ctx->state[1] = 0xBB67AE85;
	ctx->state[2] = 0x3C6EF372;
	ctx->state[3] = 0xa54ff53a;
	ctx->state[4] = 0x510e527f;
	ctx->state[5] = 0x9b05688c;
	ctx->state[6] = 0x1f83d9ab;
	ctx->state[7] = 0x5be0cd19;
	ctx->count[0] = 0;
	ctx->count[1] = 0;

	return UCL_OK;
}

int ucl_sha256_core(struct ucl_sha256_ctx_t *ctx, u8 *data, u32 datalen)
{
	u32 indexh, partlen, i;

	if (ctx == NULL)
		return UCL_INVALID_INPUT;
	if ((data == NULL) || (datalen == ZERO_VALUE))
		return UCL_NOP;
	/** Compute number of bytes mod 64 */
	indexh = (u32)((ctx->count[1] >> BYTE_LENGTH_3) & BYTE_VALUE_3F);

	/* Update number of bits */
	if ((ctx->count[1] += ((u32)datalen << BYTE_LENGTH_3)) < ((u32)datalen << BYTE_LENGTH_3))
		ctx->count[0]++;
	ctx->count[0] += ((u32)datalen >> BYTE_LENGTH_29);
	partlen = BYTE_LENGTH_64 - indexh;

	/* Process 512-bits block as many times as possible. */

	if (datalen >= partlen) {
		memcpy(&ctx->buffer[indexh], data, partlen);
		swapcpy_b2b(ctx->buffer, ctx->buffer, BYTE_LENGTH_16);
		sha256_stone(ctx->state, (u32 *)ctx->buffer);
		for (i = partlen; i + BYTE_LENGTH_63 < datalen; i += BYTE_LENGTH_64) {
			swapcpy_b2b(ctx->buffer, &data[i], BYTE_LENGTH_16);
			sha256_stone(ctx->state, (u32 *) ctx->buffer);
		}
		indexh = ZERO_VALUE;
	} else {
		i = ZERO_VALUE;
	}

	/* Buffer remaining data */
	memcpy((void *)&ctx->buffer[indexh], &data[i], datalen - i);

	return UCL_OK;
}


int ucl_sha256_finish(u8 *hash, struct ucl_sha256_ctx_t *ctx)
{
	u8 bits[8];
	u32 indexh, padlen;
	u8 padding[64];
	padding[0] = BYTE_VALUE_80;
	memset((void *)padding + 1, ZERO_VALUE, BYTE_LENGTH_63);

	if (hash == NULL)
		return UCL_INVALID_OUTPUT;

	if (ctx == NULL)
		return UCL_INVALID_INPUT;
	/* Save number of bits */
	swapcpy_w2b(bits, ctx->count, BYTE_LENGTH_2);
	/* Pad out to 56 mod 64. */
	indexh = (u32)((ctx->count[1] >> 3) & BYTE_VALUE_3F);
	padlen = (indexh < BYTE_LENGTH_56) ? (BYTE_LENGTH_56 - indexh) : (BYTE_LENGTH_120 - indexh);
	ucl_sha256_core(ctx, padding, padlen);
	/* Append length (before padding) */
	ucl_sha256_core(ctx, bits, BYTE_LENGTH_8);
	/* Store state in digest */
	swapcpy_w2b(hash, ctx->state, BYTE_LENGTH_8);
	/* Zeroize sensitive information. */
	memset(ctx, ZERO_VALUE, sizeof(*ctx));

	return UCL_OK;
}

int ucl_sha256(u8 *hash, u8 *message, u32 bytelength)
{
	struct ucl_sha256_ctx_t ctx;

	if (hash == NULL)
		return UCL_INVALID_OUTPUT;

	ucl_sha256_init(&ctx);
	ucl_sha256_core(&ctx, message, bytelength);
	ucl_sha256_finish(hash, &ctx);

	return UCL_OK;
}
#endif /* HASH_SHA256 */
