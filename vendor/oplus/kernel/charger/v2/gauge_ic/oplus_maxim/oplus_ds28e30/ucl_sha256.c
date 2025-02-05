// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */
#define pr_fmt(fmt) "[UCL_SHA256]([%s][%d]): " fmt, __func__, __LINE__

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

#include "ucl_config.h"
#include "ucl_defs.h"
#include "ucl_retdefs.h"
#include "ucl_types.h"
#include "sha256.h"

static u32 _wsb_b2w(u8 *src)
{
	return ((u32)src[3] | ((u32)src[2] << 8) |
		((u32)src[1] << 16) | ((u32)src[0] << 24));
}

static void _wsb_w2b(u8 *dst, u32 src)
{
	dst[3] = src & 0xFF;
	src >>= 8;
	dst[2] = src & 0xFF;
	src >>= 8;
	dst[1] = src & 0xFF;
	src >>= 8;
	dst[0] = src & 0xFF;
}

void swapcpy_b2w(u32 *dst, const u8 *src, u32 wordlen)
{
	int i;

	for (i = 0; i < (int)wordlen; i++) {
		dst[i] = _wsb_b2w((u8 *)src);
		src += 4;
	}
}


void swapcpy_w2b(u8 *dst, const u32 *src, u32 wordlen)
{
	int i;

	for (i = 0; i < (int)wordlen; i++) {
		_wsb_w2b(dst, src[i]);
		dst += 4;
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
		dst += 4;
		src += 4;
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
	if ((data == NULL) || (datalen == 0))
		return UCL_NOP;
	/** Compute number of bytes mod 64 */
	indexh = (u32)((ctx->count[1] >> 3) & 0x3F);

	/* Update number of bits */
	if ((ctx->count[1] += ((u32)datalen << 3)) < ((u32)datalen << 3))
		ctx->count[0]++;
	ctx->count[0] += ((u32)datalen >> 29);
	partlen = 64 - indexh;

	/* Process 512-bits block as many times as possible. */

	if (datalen >= partlen) {
		memcpy(&ctx->buffer[indexh], data, partlen);
		swapcpy_b2b(ctx->buffer, ctx->buffer, 16);
		sha256_stone(ctx->state, (u32 *)ctx->buffer);
		for (i = partlen; i + 63 < datalen; i += 64) {
			swapcpy_b2b(ctx->buffer, &data[i], 16);
			sha256_stone(ctx->state, (u32 *) ctx->buffer);
		}
		indexh = 0;
	} else {
		i = 0;
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
	padding[0] = 0x80;
	memset((void *)padding + 1, 0, 63);

	if (hash == NULL)
		return UCL_INVALID_OUTPUT;

	if (ctx == NULL)
		return UCL_INVALID_INPUT;
	/* Save number of bits */
	swapcpy_w2b(bits, ctx->count, 2);
	/* Pad out to 56 mod 64. */
	indexh = (u32)((ctx->count[1] >> 3) & 0x3f);
	padlen = (indexh < 56) ? (56 - indexh) : (120 - indexh);
	ucl_sha256_core(ctx, padding, padlen);
	/* Append length (before padding) */
	ucl_sha256_core(ctx, bits, 8);
	/* Store state in digest */
	swapcpy_w2b(hash, ctx->state, 8);
	/* Zeroize sensitive information. */
	memset(ctx, 0, sizeof(*ctx));

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
