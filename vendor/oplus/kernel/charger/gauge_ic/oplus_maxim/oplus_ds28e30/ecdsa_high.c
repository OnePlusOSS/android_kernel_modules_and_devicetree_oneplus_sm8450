// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

/* 1.0.0: first release,  with sign and verify functions,  taken from UCL */
/* 1.0.1: some cleaning in the code */
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
#include "bignum_ecdsa_generic_api.h"
#include "deep_cover_coproc.h"
#include "ecdsa_generic_api.h"
#include "ucl_config.h"
#include "ucl_types.h"
#include "ucl_defs.h"
#include "ucl_retdefs.h"
#include "ucl_rng.h"
#include "ucl_hash.h"
#include "ecdsa_high.h"
#ifdef HASH_SHA256
#include "ucl_sha256.h"
#endif

__API__ int hash_size[MAX_HASH_FUNCTIONS];

int __API__ ucl_init(void)
{
#ifdef HASH_SHA256
	hash_size[UCL_SHA256] = UCL_SHA256_HASHSIZE;
#endif
#ifdef HASH_SIA256
	hash_size[UCL_SIA256] = UCL_SIA256_HASHSIZE;
#endif
	return(UCL_OK);
}

int ucl_ecdsa_signature(struct ucl_type_ecdsa_signature *signature, u8 *d, int(*ucl_hash)(u8*, u8*, u32),
			u8 *input, u32 inputlength, struct ucl_type_curve *curve_params, u32 configuration)
{
	/* the larger size to cover any hash function */
	u8 e[64];
	int resu;
	/* SECP521R1_WORDSIZE = 17 */
	u32 r[SECP521R1_WORDSIZE], e1[SECP521R1_WORDSIZE], s[SECP521R1_WORDSIZE], u2[SECP521R1_WORDSIZE + 1];
	u32 x1[SECP521R1_WORDSIZE], y1[SECP521R1_WORDSIZE], k[SECP521R1_WORDSIZE];
	u32 w[SECP521R1_WORDSIZE], d1[SECP521R1_WORDSIZE];
	struct ucl_type_ecc_digit_affine_point q;
	struct ucl_type_ecc_digit_affine_point p;
	int hash, input_format;
	int curve_wsize, curve_bsize, hashsize;

	/* check parameters */
	if (NULL == input)
		return(UCL_INVALID_INPUT);
	if (NULL == d)
		return(UCL_INVALID_INPUT);

	/* retrieve configuration */
	hash = (configuration >> UCL_HASH_SHIFT) & UCL_HASH_MASK;
	input_format = (configuration >> UCL_INPUT_SHIFT) & UCL_INPUT_MASK;

	/* if no-input,  the function call is only for r & k-1 precomputation */
	if (UCL_NO_INPUT == input_format)
		return(UCL_INVALID_INPUT);

	/* hash computation only if input format is UCL_MSG_INPUT */
	hashsize = hash_size[hash];
	/* 1. e = SHA(m) */
	if (UCL_MSG_INPUT == input_format) {
		ucl_hash(e, input, inputlength);
	} else {
		if (UCL_NO_INPUT != input_format) {
			/* here,  the hash is provided as input */
			if (inputlength != UCL_SHA256_HASHSIZE)
				if (inputlength != UCL_SIA256_HASHSIZE)
					return(UCL_INVALID_INPUT);
			hashsize = (int)inputlength;
			memcpy(e, input, inputlength);
		}
	}
	curve_wsize = (int)(curve_params->curve_wsize);    /* curve_wsize = 8 */
	curve_bsize = (int)(curve_params->curve_bsize);    /* curve_bsize = 32 */
/*
 2 generate k for q computation
 this has to be really random,  otherwise the key is exposed
 do
 ucl_rng_read((u8*)K, (u32)curve_bsize);
 while(bignum_cmp(K, (u32*)(curve_params->n), curve_wsize)>= 0);
*/
	do {
		ucl_rng_read((u8*)k, (u32)curve_bsize);
	} while (bignum_cmp(k, (u32*)(curve_params->n), curve_wsize)>= ZERO_VALUE);

#ifdef ECDSA_FIXED_RANDOM
	ptr = (u8*) & k[0];
	for (i = 0; i < 32; i += BYTE_LENGTH_2) {
		ptr[i++] = BYTE_VALUE_55;
		ptr[i] = BYTE_VALUE_AA;
	}
#endif
	/* 3 compute r = x1(mod n) where (x1, y1) = k.G */
	/* compute k.G */
	q.x = x1;
	q.y = y1;
	p.x = (u32*)curve_params->xg;
	p.y = (u32*)curve_params->yg;
	resu = ecc_mult_jacobian(&q, k, &p, curve_params);

	if (UCL_OK != resu)
		return(resu);

	bignum_d2us(signature->r, (u32)curve_bsize, x1, (u32)curve_wsize);
	/* r = x1 mod n */
	bignum_mod(r, x1, (u32)curve_wsize, (u32*)curve_params->n, (u32)curve_wsize);
	/* store R in r */
	bignum_d2us(signature->r, (u32)curve_bsize, r, (u32)curve_wsize);
	/* 4 compute s = k_inv.(z+r.d)mod n */
	bignum_modinv(w, k, (u32*)curve_params->n, (u32)curve_wsize);
	/* parameter check */
	/* u2 = r.d */
	bignum_us2d(d1, (u32)curve_wsize, d, (u32)curve_bsize);
	bignum_modmult(u2, r, d1, (u32*)curve_params->n, (u32)curve_wsize);
	/* z+r.d where z is e */
	bignum_us2d(e1, (u32)curve_wsize, e, (u32)min(curve_bsize, hashsize));
	/* sm2.A5 r = (e+x1) mod n */
	bignum_modadd(u2, e1, u2, (u32*)curve_params->n, (u32)curve_wsize);
	/* k_inv . (z+r.d) */
	bignum_modmult(s, w, u2, (u32*)curve_params->n, (u32)curve_wsize);
	bignum_d2us(signature->s, (u32)curve_bsize, s, (u32)curve_wsize);
	/* 6 result */
	return(UCL_OK);
}

int ucl_ecdsa_verification(struct ucl_type_ecc_u8_affine_point *q, struct ucl_type_ecdsa_signature *signature,
			   int(*ucl_hash)(u8*, u8*, u32), u8 *input, u32 inputlength,
			   struct ucl_type_curve *curve_params, u32 configuration)
{
	u32 s[SECP521R1_WORDSIZE+1], r[SECP521R1_WORDSIZE], w[SECP521R1_WORDSIZE];
	u32 e1[SECP521R1_WORDSIZE], u1[SECP521R1_WORDSIZE], u2[SECP521R1_WORDSIZE];
	u32 x1[SECP521R1_WORDSIZE], y1[SECP521R1_WORDSIZE], x2[SECP521R1_WORDSIZE], y2[SECP521R1_WORDSIZE];
	u32 x[SECP521R1_WORDSIZE], y[SECP521R1_WORDSIZE];
	u32 xq[SECP521R1_WORDSIZE], yq[SECP521R1_WORDSIZE];
	struct ucl_type_ecc_digit_affine_point pq;
	struct ucl_type_ecc_digit_affine_point pp;
	struct ucl_type_ecc_digit_affine_point pr;

	/* the hash digest has the largest size,  to fit any hash function */
	u8 e[64];
	int hash, input_format;
	int curve_wsize, curve_bsize, hashsize;

	/* check parameters */
	if (NULL == input)
		return(UCL_INVALID_INPUT);

	/* retrieve configuration */
	hash = (configuration >> UCL_HASH_SHIFT) & UCL_HASH_MASK;
	input_format = (configuration >> UCL_INPUT_SHIFT) & UCL_INPUT_MASK;

	/* no input is non sense for verify */
	if (UCL_NO_INPUT == input_format)
		return(UCL_INVALID_INPUT);

	/* hash computation only if input format is UCL_MSG_INPUT */
	/* 1. e = SHA(m) */
	hashsize = hash_size[hash];
	if (UCL_MSG_INPUT == input_format) {
		ucl_hash(e, input, inputlength);
	} else {
		/* or here,  the hash is provided as input */
		if(inputlength != UCL_SHA256_HASHSIZE)
			return(UCL_INVALID_INPUT);
		hashsize = (int)inputlength;
		memcpy(e, input, inputlength);
	}
	curve_wsize = curve_params->curve_wsize;
	curve_bsize = curve_params->curve_bsize;

	/* 2. Verification of the r/s intervals (shall be <n) */
	bignum_us2d(s, (u32)curve_wsize, signature->s, (u32)curve_bsize);
	bignum_us2d(r, (u32)curve_wsize, signature->r, (u32)curve_bsize);
	if((bignum_cmp(s, (u32*)curve_params->n, (u32)curve_wsize)>= ZERO_VALUE)||
	   (bignum_cmp(r, (u32*)curve_params->n, (u32)curve_wsize) >= ZERO_VALUE))
		return(UCL_ERROR);
	/* 3. w = s^-1 */
	bignum_modinv(w, s, (u32*)curve_params->n, (u32)curve_wsize);
	/* 4. U1 = e.w mod n and U2 = r.w mod n */
	bignum_us2d(e1, (u32)curve_wsize, e, (u32)min(hashsize, curve_bsize));
	/* U1 = E*W mod n */
	bignum_modmult(u1, e1, w, (u32*)curve_params->n, (u32)curve_wsize);
	bignum_modmult(u2, r, w, (u32*)curve_params->n, (u32)curve_wsize);
	/* 5. (x1, y1) = u1*G+u2*q */
	/* u1*G */
	pp.x = (u32*)curve_params->xg;
	pp.y = (u32*)curve_params->yg;
	pq.x = x1;
	pq.y = y1;
	ecc_mult_jacobian(&pq, u1, &pp, curve_params);

	/* u2*q */
	bignum_us2d(xq, (u32)curve_wsize, q->x, (u32)curve_bsize);
	bignum_us2d(yq, (u32)curve_wsize, q->y, (u32)curve_bsize);
	pp.x = xq;
	pp.y = yq;
	pq.x = x2;
	pq.y = y2;
	ecc_mult_jacobian(&pq, u2, &pp, curve_params);

	/* u1*G+u2*q */
	if (bignum_cmp(x1, x2, (u32)curve_wsize) != ZERO_VALUE || bignum_cmp(x1, x2, (u32)curve_wsize) != ZERO_VALUE) {
		pp.x = x1;
		pp.y = y1;
		pq.x = x2;
		pq.y = y2;
		pr.x = x;
		pr.y = y;
		ecc_add(&pr, &pp, &pq, curve_params);
	} else {
		pp.x = x1;
		pp.y = y1;
		pr.x = x;
		pr.y = y;
		ecc_double(&pr, &pp, curve_params);
	}
	/* 5.4.4 2. v = x1 mod n */
	bignum_mod(y, x, (u32)curve_wsize, (u32*)curve_params->n, (u32)curve_wsize);
	/* 3. if r == v) ok */
	if (bignum_cmp(r, y, (u32)curve_wsize) == ZERO_VALUE)
		return(UCL_OK);
	else
		return(UCL_ERROR);
}
