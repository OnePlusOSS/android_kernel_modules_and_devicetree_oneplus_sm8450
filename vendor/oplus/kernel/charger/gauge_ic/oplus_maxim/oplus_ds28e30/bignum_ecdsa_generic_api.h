// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#ifndef _BIGNUM_ECDSA_GENERIC_API_H
#define _BIGNUM_ECDSA_GENERIC_API_H

#include "ucl_config.h"
#include "ucl_types.h"

#define MAX_DIGIT	0xFFFFFFFF
#define HALF_DIGIT	0xFFFF
#define MAX_DIGITS	65
#define DIGIT_BITS	32

/* Length of digit in bytes */
#define DIGIT_LEN	(DIGIT_BITS / 8)

void bignum_modsquare(u32 *r, u32 *a, u32 *m, u32 k);
int bignum_modmult(u32 *r, u32 *a, u32 *b, u32 *m, u32 k);
void bignum_d2us(u8 *a, u32  len, u32 *b, u32 digits);
void bignum_us2d(u32 *a, u32 digits, u8 *b, u32 len);
int bignum_cmp(u32 *a, u32 *b, u32 s);
int bignum_isnul(u32 *a, u32 ta);
u32 bignum_digits(u32 *n, u32 tn);
u32 bignum_digitbits(u32 a);
void bignum_copydigit(u32 *e, u32 f, u32 te);
void bignum_copyzero(u32 *e, u32 te);
void bignum_copy(u32 *e, u32 *f, u32 te);

u32 bignum_add(u32 *w, u32 *x, u32 *y, u32 digits);
u32 bignum_sub(u32 *w, u32 *x, u32 *y, u32 digits);
void bignum_mult(u32 *t, u32 *a, u32 *b, u32 n);
void bignum_mult_operand_scanning_form(u32 *t, u32 *a, u32 *b, u32 n);
void bignum_multopt(u32 *t, u32 *a, u32 *b, u32 n);
void bignum_multlimited(u32 *t, u32 *a, u32 *b, u32 n);
void bignum_multscalar(u32 *t, u32 a, u32 *b, u32 n);
void bignum_mult2sizes(u32 *t, u32 *a, u32 na, u32 *b, u32 nb);
void bignum_square(u32 *a, u32 *b, u32 digits);
u32 bignum_leftshift(u32 *a, u32 *b, u32 c, u32 digits);
u32 bignum_rightshift(u32 *a, u32 *b, u32 c, u32 digits);

void bignum_modinv(u32 *x, u32 *a0, u32 *b0, u32 digits);
void bignum_modinv_subs(u32 *x, u32 *a0, u32 *b0, u32 digits);
void bignum_modinv3(u32 *x, u32 *a0, u32 *b0, u32 digits);
void bignum_modinv4(u32 *x, u32 *a0, u32 *b0, u32 digits);
void bignum_modadd(u32 *r, u32 *a, u32 *b, u32 *m, u32 k);
void bignum_modexp(u32 *r, u32 *a, u32 *b, u32 *m, u32 k);
void bignum_mod(u32 *b, u32 *c, u32 cdigits, u32 *d, u32 ddigits);
void bignum_div(u32 *rmd, u32 *b, u32 *c, u32 cdigits, u32 *d, u32 ddigits);

#endif /* _BIGNUM_ECDSA_GENERIC_API_H */
