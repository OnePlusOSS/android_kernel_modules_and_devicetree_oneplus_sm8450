// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */
#define pr_fmt(fmt) "[GENERATE_KEY]([%s][%d]): " fmt, __func__, __LINE__

#define GEN_ECC_KEY
#include "ecc_generate_key.h"

int deep_cover_generate_publickey(u8 *private_key, u8 *pubkey_x, u8 *pubkey_y);
/*
 Generate ECC secp256r1 from provided Private Key
 @param[in] private_key
 pointer to buffer that contains the private key
 (minumum 32-byte buffer, SECP256R1_BYTESIZE)
 @param[out] pubkey_x
 32-byte buffer container the public key x value
 @param[out] pubkey_y
 32-byte buffer container the public key y value
 @return
 TRUE - command successful @n
 FALSE - command failed
 */
int deep_cover_generate_publickey(u8 *private_key, u8 *pubkey_x, u8 *pubkey_y)
{
	struct ucl_type_ecc_digit_affine_point g_point, public_key;
	u32 private_key_words[SECP256R1_WORDSIZE];
	u32 gx[SECP256R1_WORDSIZE];
	u32 gy[SECP256R1_WORDSIZE];
	u32 public_key_x_words[SECP256R1_WORDSIZE];
	u32 public_key_y_words[SECP256R1_WORDSIZE];
	int i, rslt;

	/* Convert bytes to words. */
	bignum_us2d(private_key_words, SECP256R1_WORDSIZE, private_key, SECP256R1_BYTESIZE);

	/* Copy multiplication constants. */
	for (i = 0; i < SECP256R1_WORDSIZE; i++) {
		gx[i] = local_xg_p256r1[i];
		gy[i] = local_yg_p256r1[i];
	}

	/* Generate public key. */
	public_key.x = public_key_x_words;
	public_key.y = public_key_y_words;
	g_point.x = gx;
	g_point.y = gy;
	rslt = ecc_mult_jacobian(&public_key, private_key_words, &g_point, &secp256r1);

	/* Convert words to bytes. */
	bignum_d2us(pubkey_x, SECP256R1_BYTESIZE, public_key_x_words, SECP256R1_WORDSIZE);
	bignum_d2us(pubkey_y, SECP256R1_BYTESIZE, public_key_y_words, SECP256R1_WORDSIZE);

	return (rslt == 0);
}
