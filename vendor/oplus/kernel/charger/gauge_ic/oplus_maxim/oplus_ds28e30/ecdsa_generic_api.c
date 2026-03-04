// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#define MAJVER	1
#define MINVER	0
#define ZVER	0
/* 1.0.0: initial release from UCL file 1.9.0 */
#include "bignum_ecdsa_generic_api.h"
#include "deep_cover_coproc.h"
#include "ecdsa_generic_api.h"
#include "ucl_config.h"
#include "ucl_types.h"
#include "ucl_defs.h"
#include "ucl_retdefs.h"
#include "ucl_rng.h"
#include "ucl_hash.h"
#ifdef HASH_SHA256
#include "ucl_sha256.h"
#endif

/* default modular reduction */
/* not efficient for special NIST primes, as not using their structure */
void ecc_mod(u32 *b, u32 *c, u32 cdigits, u32 *p, u32 pdigits)
{
	bignum_mod(b, c, cdigits, p, pdigits);
}

#ifdef P192
/* default modular reduction */
/* not efficient for special NIST primes, as not using their structure */
void ecc_mod192r1(u32 *b, u32 *c, u32 cdigits, u32 *p, u32 pdigits)
{
	bignum_mod(b, c, cdigits, p, pdigits);
}
#endif /* P192 */
#ifdef P256
/* default modular reduction */
/* not efficient for special NIST primes, as not using their structure */
void ecc_mod256r1(u32 *b, u32 *c, u32 cdigits, u32 *p, u32 pdigits)
{
	bignum_mod(b, c, cdigits, p, pdigits);
}
#endif /* P256 */

int point_less_than_psquare(u32 *c, u32 cdigits, u32 *psquare, u32 pdigits)
{
	if (cdigits < pdigits) {
		return(UCL_TRUE);
	} else {
		if (cdigits > pdigits)
			return(UCL_FALSE);
		else
			if (bignum_cmp(c, psquare, cdigits) >= ZERO_VALUE)
				return(UCL_FALSE);
			else
				return(UCL_TRUE);
	}
}

void ecc_modcurve(u32 *b, u32 *c, u32 cdigits, struct ucl_type_curve *curve_params)
{
	switch(curve_params->curve) {
#ifdef P192
	case SECP192R1:
		ecc_mod192r1(b, c, cdigits, (u32*)(curve_params->p), curve_params->curve_wsize);
	break;
#endif /* P192 */
#ifdef P256
	case SECP256R1:
		ecc_mod256r1(b, c, cdigits, (u32*)(curve_params->p), curve_params->curve_wsize);
	break;
#endif /* P256 */
	default:
		ecc_mod(b, c, cdigits, (u32*)(curve_params->p), curve_params->curve_wsize);
	break;
	}
}

int ecc_modsub(u32 *p_result, u32 *p_left, u32 *p_right, struct ucl_type_curve *curve_params)
{
	u32 carry;
	carry = bignum_sub(p_result, p_left, p_right, curve_params->curve_wsize);
	if (carry)
		bignum_add(p_result, p_result, (u32*)(curve_params->p), curve_params->curve_wsize);
	return(UCL_OK);
}

int ecc_modadd(u32 *r, u32 *a, u32 *b, struct ucl_type_curve *curve_params)
{
	u32 resu[1 + ECDSA_DIGITS];
	bignum_add(resu, a, b, curve_params->curve_wsize);
	ecc_modcurve(r, resu, 1 + curve_params->curve_wsize, curve_params);
	return(UCL_OK);
}

int ecc_modleftshift(u32 *a, u32 *b, u32 c, u32 digits, struct ucl_type_curve *curve_params)
{
	u32 tmp[ECDSA_DIGITS + 1];

	tmp[digits] = bignum_leftshift(tmp, b, c, digits);
	ecc_modcurve(a, tmp, digits + 1, curve_params);
	return(UCL_OK);
}

int ecc_modmult(u32 *r, u32 *a, u32 *b, struct ucl_type_curve *curve_params)
{
	u32 mult[2 * ECDSA_DIGITS];

	bignum_mult(mult, a, b, curve_params->curve_wsize);
	ecc_modcurve(r, mult, 2 * curve_params->curve_wsize, curve_params);
	return(UCL_OK);
}

void ecc_modmultscalar(u32 *r, u32 a, u32 *b, struct ucl_type_curve *curve_params)
{
	u32 mult[2 * ECDSA_DIGITS];

	bignum_multscalar(mult, a, b, curve_params->curve_wsize);
	ecc_modcurve(r, mult, 2 * curve_params->curve_wsize, curve_params);
}

int ecc_modsquare(u32 *r, u32 *a, struct ucl_type_curve *curve_params)
{
	u32 mult[2 * ECDSA_DIGITS];

	bignum_square(mult, a, curve_params->curve_wsize);
	ecc_modcurve(r, mult, 2 * curve_params->curve_wsize, curve_params);
	return(UCL_OK);
}

int  ecc_infinite_affine(struct ucl_type_ecc_digit_affine_point *q, struct ucl_type_curve *curve_params)
{
	if (bignum_isnul(q->x, (u32)(curve_params->curve_wsize)) &&
	   bignum_isnul(q->y, (u32)(curve_params->curve_wsize)))
		return(UCL_TRUE);

	return(UCL_ERROR);
}

int ecc_infinite_jacobian(struct ucl_type_ecc_jacobian_point *q, struct ucl_type_curve *curve_params)
{
	int i;

	if ((q->x[0] != TRUE) || (q->y[0] != TRUE))
		return(UCL_ERROR);
	if (!bignum_isnul(q->z, curve_params->curve_wsize))
		return(UCL_ERROR);

	for (i = 1; i < (int)curve_params->curve_wsize; i++)
		if ((q->x[i] != FALSE) || (q->y[i] != FALSE))
			return(UCL_ERROR);
	return(UCL_TRUE);
}

int ecc_double_jacobian(struct ucl_type_ecc_jacobian_point *q3, struct ucl_type_ecc_jacobian_point *q1,
						struct ucl_type_curve *curve_params)
{
	u32 t1[ECDSA_DIGITS];
	u32 t2[ECDSA_DIGITS];
	u32 t3[ECDSA_DIGITS + 1];
	int digits;
	digits = curve_params->curve_wsize;

	/* 2.t1 = z1^2 */
	if (ecc_infinite_jacobian(q1, curve_params) == UCL_TRUE) {
		/* return(x2:y2:1) */
		bignum_copy(q3->x, q1->x, curve_params->curve_wsize);
		bignum_copy(q3->y, q1->y, curve_params->curve_wsize);
		bignum_copydigit(q3->z, ZERO_VALUE, curve_params->curve_wsize);
		return(UCL_OK);
	}
	ecc_modsquare(t1, q1->z, curve_params);
	/* 3.t2 = x1-t1 */
	ecc_modsub(t2, q1->x, t1, curve_params);
	/* 4.t1 = x1 + t1 */
	bignum_modadd(t1, t1, q1->x, (u32*)curve_params->p, curve_params->curve_wsize);
	/* 5.t2 = t2*t1 */
	ecc_modmult(t2, t2, t1, curve_params);
	/* 6.t2 = 3*t2 */
	ecc_modmultscalar(t2, BYTE_VALUE_3, t2, curve_params);
	/* 7.y3 = 2*y1 */
	ecc_modleftshift(q3->y, q1->y, BYTE_VALUE_1, digits, curve_params);
	/* 8.z3 = y3*z1 */
	ecc_modmult(q3->z, q1->z, q3->y, curve_params);
	/* 9.y3^2 */
	ecc_modsquare(q3->y, q3->y, curve_params);
	/* 10.t3 = y3.x1 */
	ecc_modmult(t3, q1->x, q3->y, curve_params);
	/* 11.y3 = y3^2 */
	ecc_modsquare(q3->y, q3->y, curve_params);
	/* 12.y3 = y3/2 equiv. to y3 = y3*(2^-1) */
	ecc_modmult(q3->y, q3->y, curve_params->invp2, curve_params);
	/* 13.x3 = t2^2 */
	ecc_modsquare(q3->x, t2, curve_params);
	/* 14.t1 = 2*t3 */
	ecc_modleftshift(t1, t3, BYTE_VALUE_1, digits, curve_params);
	/* 15.x3 = x3-t1 */
	ecc_modsub(q3->x, q3->x, t1, curve_params);
	/* 16.t1 = t3-x3 */
	ecc_modsub(t1, t3, q3->x, curve_params);
	/* 17.t1 = t1*t2 */
	ecc_modmult(t1, t1, t2, curve_params);
	/* 18.y3 = t1-y3 */
	ecc_modsub(q3->y, t1, q3->y, curve_params);
	/* result in x3, y3, z3 */
	return(UCL_OK);
}

int ecc_add_jacobian_affine(struct ucl_type_ecc_jacobian_point *q3, struct ucl_type_ecc_jacobian_point *q1,
			    struct ucl_type_ecc_digit_affine_point *q2, struct ucl_type_curve *curve_params)
{
	u32 t1[ECDSA_DIGITS];
	u32 t2[ECDSA_DIGITS];
	u32 t3[ECDSA_DIGITS];
	u32 t4[ECDSA_DIGITS];
	u32 scalar[ECDSA_DIGITS];
	struct ucl_type_ecc_jacobian_point q2tmp;
	int digits;

	digits = curve_params->curve_wsize;
	if (ecc_infinite_affine(q2, curve_params) == UCL_TRUE) {
		bignum_copy(q3->x, q1->x, curve_params->curve_wsize);
		bignum_copy(q3->y, q1->y, curve_params->curve_wsize);
		bignum_copy(q3->z, q1->z, curve_params->curve_wsize);
		return(UCL_OK);
	}

	if (ecc_infinite_jacobian(q1, curve_params) == UCL_TRUE) {
		/* return(x2:y2:1) */
		bignum_copy(q3->x, q2->x, curve_params->curve_wsize);
		bignum_copy(q3->y, q2->y, curve_params->curve_wsize);
		bignum_copydigit(q3->z, BYTE_VALUE_1, curve_params->curve_wsize);
		return(UCL_OK);
	}
	/* 3.t1 = z1^2 */
	ecc_modsquare(t1, q1->z, curve_params);
	/* 4.t2 = t1*z1 */
	ecc_modmult(t2, t1, q1->z, curve_params);
	/* 6.t2 = t2*y2 */
	ecc_modmult(t2, t2, q2->y, curve_params);
	/* 5.t1 = t1*x2 */
	ecc_modmult(t1, t1, q2->x, curve_params);
	/* 7.t1 = t1-x1 */
	ecc_modsub(t1, t1, q1->x, curve_params);
	/* 8.t2 = t2-y1 */
	ecc_modsub(t2, t2, q1->y, curve_params);
	/* 9. */
	if (bignum_isnul(t1, curve_params->curve_wsize)) {
		bignum_copyzero(scalar, curve_params->curve_wsize);
		/* 9.1 */
		if (bignum_isnul(t2, curve_params->curve_wsize)) {
			/* double (x2:y2:1) */
			scalar[0] = BYTE_VALUE_1;
			q2tmp.x = q2->x;
			q2tmp.y = q2->y;
			q2tmp.z = scalar;
			ecc_double_jacobian(q3, &q2tmp, curve_params);
			return(UCL_OK);
		} else {
			/* return infinite */
			bignum_copy(q3->x, scalar, curve_params->curve_wsize);
			bignum_copy(q3->y, scalar, curve_params->curve_wsize);
			bignum_copyzero(q3->z, curve_params->curve_wsize);
			return(UCL_OK);
		}
	}
	/* 10.z3 = z1*t1 */
	ecc_modmult(q3->z, q1->z, t1, curve_params);
	/* 11.t3 = t1^2 */
	ecc_modsquare(t3, t1, curve_params);
	/* 12.t4 = t3*t1 */
	ecc_modmult(t4, t3, t1, curve_params);
	/* 13.t3 = t3*x1 */
	ecc_modmult(t3, t3, q1->x, curve_params);
	/* 14.t1 = 2*t3 */
	ecc_modleftshift(t1, t3,  BYTE_VALUE_1, digits, curve_params);
	/* 15.x3 = t2^2 */
	ecc_modsquare(q3->x, t2, curve_params);
	/* 16.x3 = Q3.x-t1 */
	ecc_modsub(q3->x, q3->x, t1, curve_params);
	/* 17.x3 = x3-t4 */
	ecc_modsub(q3->x, q3->x, t4, curve_params);
	/* 18.t3 = t3-x3 */
	ecc_modsub(t3, t3, q3->x, curve_params);
	/* 19.t3 = t3*t2 */
	ecc_modmult(t3, t3, t2, curve_params);
	/* 20.t4 = t4*y1 */
	ecc_modmult(t4, t4, q1->y, curve_params);
	/* 21.y3 = t3-t4 */
	ecc_modsub(q3->y, t3, t4, curve_params);
	/* result in x3, y3, z3 */
	return(UCL_OK);
}

int ecc_convert_affine_to_jacobian(struct ucl_type_ecc_jacobian_point *q,
				struct ucl_type_ecc_digit_affine_point *x1, struct ucl_type_curve *curve_params)
{
	/* conversion from x:y to x*z^2:y*z^3:z; direct and simple for z = 1 */
	bignum_copy(q->x, x1->x, (u32)curve_params->curve_wsize);
	bignum_copy(q->y, x1->y, (u32)curve_params->curve_wsize);
	bignum_copydigit(q->z, BYTE_VALUE_1, (u32)curve_params->curve_wsize);
	return(UCL_OK);
}

int ecc_convert_jacobian_to_affine(u32 *x, u32 *y, u32 *xq, u32 *yq, u32 *zq,
			struct ucl_type_curve *curve_params)
{
	u32 tmp[ECDSA_DIGITS];
	u32 tmp1[ECDSA_DIGITS];
	int digits;
	digits = curve_params->curve_wsize;
	/* x:y:z corresponds to x/z^2:y/z^3 */
	/* z^2 */
	ecc_modsquare(tmp, zq, curve_params);
	/* z^-2 */
	bignum_modinv(tmp1, tmp, (u32*)curve_params->p, digits);
	ecc_modmult(x, xq, tmp1, curve_params);
	/* z^3 */
	ecc_modmult(tmp, tmp, zq, curve_params);
	/* z^-3 */
	bignum_modinv(tmp1, tmp, (u32*)curve_params->p, digits);
	ecc_modmult(y, yq, tmp1, curve_params);
	return(UCL_OK);
}

int ecc_mult_jacobian(struct ucl_type_ecc_digit_affine_point *q, u32 *m,
				struct ucl_type_ecc_digit_affine_point *x1, struct ucl_type_curve *curve_params)
{
	int i;
	int j;
	u32 zq[ECDSA_DIGITS];
	int size;
	struct ucl_type_ecc_jacobian_point t;
	u32 mask = (u32)0x80000000;
	u8 first;
	if (NULL == m)
		return(UCL_INVALID_INPUT);
	bignum_copyzero(q->x, curve_params->curve_wsize);
	bignum_copyzero(q->y, curve_params->curve_wsize);
	bignum_copyzero(zq, curve_params->curve_wsize);
	size = (int)curve_params->curve_wsize;
/*	mask = (u32)0x80000000;   */
	t.x = q->x;
	t.y = q->y;
	t.z = zq;
	first = BYTE_VALUE_1;
	for (i = (int)(size-1); i >= ZERO_VALUE; i--) {
		for (j = 0; j < (int)DIGIT_BITS; j++) {
			if (!first)
				ecc_double_jacobian(&t, &t, curve_params);
			if ((m[i] & (mask >> j)) != ZERO_VALUE) {
				if (first) {
					ecc_convert_affine_to_jacobian(&t, x1, curve_params);
					first = ZERO_VALUE;
				} else {
					ecc_add_jacobian_affine(&t, &t, x1, curve_params);
				}
			}
		}
	}
	ecc_convert_jacobian_to_affine(q->x, q->y, t.x, t.y, t.z, curve_params);
	return(UCL_OK);
}

int ecc_add(struct ucl_type_ecc_digit_affine_point *q3, struct ucl_type_ecc_digit_affine_point *q1,
				struct ucl_type_ecc_digit_affine_point *q2, struct ucl_type_curve *curve_params)
{
	u32 lambda[ECDSA_DIGITS];
	u32 tmp1[ECDSA_DIGITS];
	u32 tmp2[ECDSA_DIGITS];

	/* tmp1 = (x2-x1) */
	ecc_modsub(tmp1, q2->x, q1->x, curve_params);
	bignum_modinv(tmp2, tmp1, (u32*)(curve_params->p), curve_params->curve_wsize);
	/* tmp1 = (y2-y1) */
	ecc_modsub(tmp1, q2->y, q1->y, curve_params);
	/* lambda = (y2-y1)*(x2-x1)^-1 mod p */
	ecc_modmult(lambda, tmp1, tmp2, curve_params);
	/* tmp1 = lambda^2 mod p */
	ecc_modsquare(tmp1, lambda, curve_params);
	/* tmp2 = lambda^2 mod p -x1 */
	ecc_modsub(tmp2, tmp1, q1->x, curve_params);
	/* x3  = lambda^2 mod p -x1 -x2 */
	ecc_modsub(q3->x, tmp2, q2->x, curve_params);
	/* tmp2 = x1-x3 */
	ecc_modsub(tmp2, q1->x, q3->x, curve_params);
	/* tmp1 = lambda * (x1-x3) */
	ecc_modmult(tmp1, lambda, tmp2, curve_params);
	/* y3 = lambda * (x1-x3) -y1 */
	ecc_modsub(q3->y, tmp1, q1->y, curve_params);
	return(UCL_OK);
}

int ecc_double(struct ucl_type_ecc_digit_affine_point *q3, struct ucl_type_ecc_digit_affine_point *q1,
				struct ucl_type_curve *curve_params)
{
	u32 lambda[ECDSA_DIGITS + 1];
	u32 tmp1[ECDSA_DIGITS + 1];
	u32 tmp2[ECDSA_DIGITS + 1];
	u32 tmp3[ECDSA_DIGITS + 1];
	u32 trois[ECDSA_DIGITS];

	bignum_copyzero(trois, curve_params->curve_wsize);
	trois[0] = BYTE_VALUE_3;
	/* tmp1 = x1^2 */
	ecc_modsquare(tmp1, q1->x, curve_params);
	/* lambda = 3*x1^2 */
	ecc_modmult(lambda, trois, tmp1, curve_params);
	/* tmp1 = 3*x1^2 + a */
	tmp1[curve_params->curve_wsize] = bignum_add(tmp1, lambda, (u32*)(curve_params->a),
				curve_params->curve_wsize);
	ecc_modcurve(tmp1, tmp1, curve_params->curve_wsize + 1, curve_params);
	/* tmp2 = 2*y1 */
	tmp2[curve_params->curve_wsize] = bignum_leftshift(tmp2, q1->y, BYTE_VALUE_1, curve_params->curve_wsize);
	ecc_modcurve(tmp2, tmp2, curve_params->curve_wsize + 1, curve_params);
	/* tmp3 = 2*y1^-1 mod p */
	bignum_modinv(tmp3, tmp2, (u32*)(curve_params->p), curve_params->curve_wsize);
	/* lambda = (3*x1^2 + a)*(2*y)^-1 modp */
	ecc_modmult(lambda, tmp1, tmp3, curve_params);
	/* tmp1 = Lambda^2 mod p */
	ecc_modsquare(tmp1, lambda, curve_params);
	/* tmp2 = Lambda^2 mod p -x1 */
	ecc_modsub(tmp2, tmp1, q1->x, curve_params);
	/* x3  = Lambda^2 mod p -x1 -x2 */
	ecc_modsub(q3->x, tmp2, q1->x, curve_params);
	/* tmp2 = x1-x3 */
	ecc_modsub(tmp2, q1->x, q3->x, curve_params);
	/* tmp1 = Lambda * (x1-x3) */
	ecc_modmult(tmp1, lambda, tmp2, curve_params);
	/* y3 = Lambda * (x1-x3) -y1 */
	ecc_modsub(q3->y, tmp1, q1->y, curve_params);
	return(UCL_OK);
}
