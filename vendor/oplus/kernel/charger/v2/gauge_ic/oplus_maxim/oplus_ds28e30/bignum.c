// SPDX-License-Identifier :  GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */
#define pr_fmt(fmt) "[BIGNUM]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include "bignum_ecdsa_generic_api.h"
#include "ucl_retdefs.h"

#define MAJVER	1
#define MINVER	0
#define ZVER	0

/* for all u32* numbers, digit[0] is the least significent digit */
void bignum_d2us(u8 *a, u32 len, u32 *b, u32 digits)
{
	u32 t;
	int j;
	int i;
	u32 u;

	for (i = 0, j = (int)len - 1; (i < (int)digits) && (j >= 0); i++) {
		t = b[i];
		for (u = 0; j >= 0 && u < (int)DIGIT_BITS; j--, u += 8)
			a[j] = (u8)(t >> u);
	}
	for (; j >= 0; j--)
		a[j] = 0;
}

void bignum_us2d(u32 *a, u32 digits, u8 *b, u32 len)
{
	u32 t;
	int j, i;
	int u;

	for (i = 0, j = (int)len - 1; (i < (int)digits) && (j >= 0); i++) {
		t = 0;
		for (u = 0; j >= 0 && u < (int)DIGIT_BITS; j--, u += 8)
			t |=  ((u32)b[j]) << (u32)u;
		a[i] = t;
	}
	for (; i < (int)digits; i++)
		a[i] = 0;
}

u32 bignum_digits(u32 *n, u32 tn)
{
	int i;

	for (i = (int)(tn - 1); i >= 0; i--) {
		if (n[i])
			break;
	}
	return((u32)(i + 1));
}

/* e = 0 except e[0] = f */
void bignum_copydigit(u32 *e, u32 f, u32 te)
{
	int i;

	for (i = (int)(te - 1); i != 0; i--)
		e[i] = 0;
	e[0] = f;
}

/* e = 0 */
void bignum_copyzero(u32 *e, u32 te)
{
	int i;

	for (i = (int)(te - 1); i != 0; i--)
		e[i] = 0;
	e[0] = 0;
}

/* e = f */
void bignum_copy(u32 *e, u32 *f, u32 te)
{
	int i;

	for (i = (int)(te - 1); i != 0; i--)
		e[i] = f[i];
	e[0] = f[0];
}

u32 bignum_digitbits(u32 a)
{
	int i;

	for (i = 0; i < (int)DIGIT_BITS; i++, a >>= 1) {
		if (a == 0)
			break;
	}
	return((u32)i);
}

int bignum_cmp(u32 *a, u32 *b, u32 s)
{
	int i;

	for (i = (int)(s - 1); i >= 0; i--) {
		if (a[i] > b[i])
			return(1);
		if (a[i] < b[i])
			return(-1);
	}
	return(0);
}

void bignum_scalarmult(u32 *c, u32 a, u32 b)
{
	union u {
		u64 result;
		u32 t[2];
	} n;
	n.result = (u64)a * (u64)b;

	c[0] = n.t[0];
	c[1] = n.t[1];
}

void bignum_scalardiv(u32 *a, u32 b[2], u32 c)
{
	u64 t;

	t  = (((u64)b[1]) << DIGIT_BITS) ^ ((u64)b[0]);
	*a = (u32)(t / c);
}

/* w = x + y, ret(carry) */

u32 bignum_sub(u32 *w, u32 *x, u32 *y, u32 digits)
{
	u32 wi, borrow = 0;
	int i;

	for (i = 0; i < (int)digits; i++) {
		if ((wi = x[i] - borrow) > (MAX_DIGIT - borrow))
			wi = MAX_DIGIT - y[i];
		else
			borrow = ((wi -= y[i]) > (MAX_DIGIT - y[i])) ? 1 : 0;
		w[i] = wi;
	}
	return(borrow);
}

u32 bignum_add(u32 *w, u32 *x, u32 *y, u32 digits)
{
	u32 wi, carry;
	int i;

	wi = x[0];
	carry = ((wi += y[0]) < y[0]) ? 1 : 0;
	w[0] = wi;
	for (i = 1; i < (int)digits; i++) {
		if ((wi = x[i] + carry) < carry)
			wi = y[i];
		else
			carry = ((wi += y[i]) < y[i]) ? 1 : 0;
		w[i] = wi;
	}
	return(carry);
}

void scalarmult(u32 *r0, u32 *r1, u32 *r2, u32 a, u32 b)
{
	u64 p = (u64)a * b;
	u64 r01 = ((u64)(*r1) << DIGIT_BITS) | *r0;

	r01 += p;
	*r2 += (r01 < p);
	*r1 = r01 >> DIGIT_BITS;
	*r0 = (u32)r01;
}

void bignum_mult_scfo(u32 *t, u32 *a, u32 *b, u32 n)
{
	u32 r0 = 0;
	u32 r1 = 0;
	u32 r2 = 0;
	int i, k;

	for (k = 0; k < (int)n; ++k) {
		for (i = 0; i <= k; ++i)
			scalarmult(&r0, &r1, &r2, a[i], b[k - i]);
		t[k] = r0;
		r0 = r1;
		r1 = r2;
		r2 = 0;
	}
	for (k = (int)n; k < (int)n * 2 - 1; ++k) {
		for (i = (k + 1) - (int)n; i < (int)n; ++i)
			scalarmult(&r0, &r1, &r2, a[i], b[k - i]);
		t[k] = r0;
		r0 = r1;
		r1 = r2;
		r2 = 0;
	}
	t[n * 2 - 1] = r0;
}

void bignum_mult(u32 *t, u32 *a, u32 *b, u32 n)
{
	bignum_mult_scfo(t, a, b, n);
}
void bignum_multscalar(u32 *t, u32 a, u32 *b, u32 n)
{
	int j;
	u32 bdigits;
	u32 carry;
	u64 de, re;

	bdigits = bignum_digits(b, n);
	bignum_copyzero(t, 2 * n);
	if (a != 0) {
		carry = 0;
		for (j = 0; j < (int)bdigits; j++) {
			re = (u64)a * (u64)b[j];
			de = (u64)t[j] + (u64)carry + re;
			carry = (u32)(de >> 32);
			t[j] = de & 0xffffffff;
		}
		t[bdigits] += carry;
	}
}

void bignum_square_opt(u32 *a, u32 *b, u32 digits)
{
	u32 t[2 * MAX_DIGITS], tmp[2], carry, carrynext, carryover;
	u32 bdigits, c0, c1;
	int i, j;

	bignum_copyzero(t, (u32)2 * digits);
	bdigits = bignum_digits(b, digits);
	for (i = 0; i < (int)bdigits; i++) {
		for (carry = carrynext = 0, j = 0; j < i; j++) {
			bignum_scalarmult(tmp, b[i], b[j]);
			c0 = tmp[0];
			c1 = tmp[1];
			t[i + j] += carry;
			if (t[i + j] < carry)
				carrynext++;
			t[i + j] += c0;
			if (t[i + j] < c0)
				carrynext++;
			t[i + j] += c0;
			if (t[i + j] < c0)
				carrynext++;
			carrynext += c1;
			if (carrynext < c1)
				carryover = 1;
			else
				carryover = 0;
			carrynext += c1;
			if (carrynext < c1)
				carryover++;
			carry = carrynext;
			carrynext = carryover;
		}
		bignum_scalarmult(tmp, b[i], b[i]);
		c0 = tmp[0];
		c1 = tmp[1];
		t[i + i] += carry;
		if (t[i + i] < carry)
			carrynext++;
		t[i + i] += c0;
		if (t[i + i] < c0)
			carrynext++;
		carrynext += c1;
		if (carrynext < c1)
			carryover = 1;
		else
			carryover = 0;
		t[i + i + 1] += carrynext;
		if (t[i + i + 1] < carrynext)
			carryover++;
		for (j = i + 2; (carryover) && (j < (int)bdigits); j++) {
			t[i + j] += carryover;
			if (t[i + j] < carryover)
				carryover = 1;
			else
				carryover = 0;
		}
		if (carryover) {
			t[i + (int)bdigits]++;
			if (t[i + (int)bdigits] < 1)
				t[i + (int)bdigits + 1]++;
		}
	}
	bignum_copy(a, t, (u32)2 * digits);
}

void bignum_square(u32 *a, u32 *b, u32 digits)
{
	bignum_square_opt(a, b, digits);
}

u32 bignum_subscalarmult(u32 *a, u32 *b, u32 c, u32 *d, u32 digits)
{
	u32 borrow = 0, di, ai, bi, t[2], val;
	int i;

	if (c == 0)
		return(0);
	for (i = 0; i < (int)digits; i++) {
		di = d[i];
		bignum_scalarmult(t, c, di);
		ai = a[i];
		bi = b[i];
		ai = bi - borrow;
		val = MAX_DIGIT - borrow;
		if (ai > val)
			borrow = 1;
		else
			borrow = 0;
		ai -= t[0];
		val = MAX_DIGIT - t[0];
		if (ai > val)
			borrow++;
		borrow += t[1];
		a[i] = ai;
	}
	return(borrow);
}

u32 bignum_leftshift(u32 *a, u32 *b, u32 c, u32 digits)
{
	u32 bi, borrow, t, m, p;
	int i;

	if (c < DIGIT_BITS) {
		if (c == 0) {
			for (i = 0; i < (int)digits; i++)
				a[i] = b[i];
			return((u32)0);
		} else {
			t = DIGIT_BITS - c;
			borrow = 0;
			for (i = 0; i < (int)digits; i++) {
				bi = b[i];
				a[i] = ((bi << c)|borrow);
				borrow = c ? (bi >> t) : 0;
			}
		}
		return(borrow);
	}
	m = c / DIGIT_BITS;
	p = c & (DIGIT_BITS - 1);
	t = DIGIT_BITS - p;
	borrow = 0;
	for (i = 0; i < (int)m; i++)
		a[i] = 0;
	if (p == 0) {
		for (i = 0; i < (int)digits; i++) {
			a[i + (int)m] = b[i];
		}
	} else {
		for (i = 0; i < (int)digits; i++) {
			bi = b[i];
			a[i + (int)m] = (borrow|(bi << p));
			borrow = p ? (bi >> t) : 0;
		}
	}

	return(borrow);
}

u32 bignum_rightshift(u32 *a, u32 *b, u32 c, u32 digits)
{
	u32 bi, borrow, t, m, p;
	int i;
	if (c < DIGIT_BITS) {
		if (c == 0) {
			for (i = 0; i < (int)digits; i++)
				a[i] = b[i];
			return(0);
		} else {
			t = DIGIT_BITS - c;
			borrow = 0;
			for (i = (int)digits - 1; i >= 0; i--) {
				bi = b[i];
				a[i] = (borrow | (bi >> c));
				borrow = bi << t;
			}
		}
		return(borrow);
	}
	p = c & (DIGIT_BITS - 1);
	m = c / DIGIT_BITS;
	t = DIGIT_BITS - p;
	borrow = 0;
	if (p == 0) {
		for (i = (int)(digits - 1 - m); i >= 0; i--)
			a[i] = b[i + (int)m];
	} else {
		for (i = (int)(digits - 1 - m); i >= 0; i--) {
			bi = b[i + (int)m];
			a[i] = (borrow|(bi >> p));
			borrow = p ? (bi << t) : 0;
		}
	}
	return(borrow);
}

void bignum_divide(u32 *a, u32 *b, u32 *c, u32 cdigits, u32 *d, u32 ddigits)
{
	u32 ai, cc[2 * MAX_DIGITS + 1], dd[MAX_DIGITS], t;
	int i;
	u32 d_digits, shift;

	d_digits = bignum_digits(d, ddigits);
	if (d_digits == 0)
		return;
	shift = DIGIT_BITS - bignum_digitbits(d[d_digits - 1]);
	bignum_copyzero(cc, d_digits);
	cc[cdigits] = bignum_leftshift(cc, c, shift, cdigits);
	bignum_leftshift(dd, d, shift, d_digits);
	t = dd[d_digits - 1];
	if (NULL != a)
		bignum_copyzero(a, cdigits);
	for (i  = (int)(cdigits - d_digits); i >= 0; i--) {
		if (t == MAX_DIGIT) {
			ai = cc[i + (int)d_digits];
		} else {
			bignum_scalardiv(&ai, &cc[i + (int)d_digits - 1], t + 1);
		}
		cc[i + (int)d_digits] -= bignum_subscalarmult(&cc[i], &cc[i], ai, dd, d_digits);
		while (cc[i + (int)d_digits] || (bignum_cmp(&cc[i], dd, d_digits) >= 0)) {
			ai++;
			cc[i + (int)d_digits] -= bignum_sub(&cc[i], &cc[i], dd, d_digits);
		}
		if (NULL != a)
			a[i] = ai;
	}
	if (b != NULL) {
		bignum_copyzero(b, ddigits);
		bignum_rightshift(b, cc, shift, d_digits);
	}
}

int bignum_modmult(u32 *r, u32 *a, u32 *b, u32 *m, u32 k)
{
	u32 mult[2 * MAX_DIGITS];

	bignum_mult(mult, a, b, k);
	bignum_mod(r, mult, 2 * k, m, k);
	return(UCL_OK);
}
void bignum_modadd(u32 *r, u32 *a, u32 *b, u32 *m, u32 k)
{
	u32 add[MAX_DIGITS + 1];

	add[k] = bignum_add(add, a, b, k);
	bignum_mod(r, add, k + 1, m, k);
}

void bignum_mod(u32 *b, u32 *c, u32 cdigits, u32 *d, u32 ddigits)
{
	u32 copy_a[2 * MAX_DIGITS], d_digits;

	d_digits = bignum_digits(d, ddigits);
	bignum_divide(NULL, copy_a, c, cdigits, d, d_digits);
	bignum_copyzero(b, ddigits);
	bignum_copy(b, copy_a, d_digits);
}

int bignum_isnul(u32 *a, u32 ta)
{
	int i;

	for (i = 0; i < (int)ta; i++)
		if (a[i])
			return(0);
	return(1);
}

void bignum_div(u32 *quot, u32 *b, u32 *c, u32 cdigits, u32 *d, u32 ddigits)
{
	u32 copy_a[2*MAX_DIGITS], d_digits;
	int i;

	d_digits = bignum_digits(d, ddigits);
	if (d_digits == 0)
		return;
	bignum_divide(quot, copy_a, c, cdigits, d, d_digits);
	if (b != NULL) {
		bignum_copy(b, copy_a, d_digits);
		for (i = (int)d_digits;i < (int)ddigits; i++)
			b[i] = 0;
	}
}

void bignum_modinv(u32 *x, u32 *a0, u32 *b0, u32 digits)
{
	u32 u[MAX_DIGITS], v[MAX_DIGITS];
	u32 a[MAX_DIGITS + 1], c[MAX_DIGITS + 1];

	bignum_copy(u, a0, digits);
	bignum_copy(v, b0, digits);
	bignum_copydigit(a, 1, digits);
	bignum_copyzero(c, digits);
	while (!bignum_isnul(u, digits)) {
	/* while u is even, so lsb is 0 */
		while ((u[0] & 1) == 0) {
			bignum_rightshift(u, u, 1, digits);
			/* if a is even */
			if ((a[0] & 1) == 0) {
				bignum_rightshift(a, a, 1, digits);
			} else {
				a[digits] = bignum_add(a, a, b0, digits);
				bignum_rightshift(a, a, 1, digits + 1);
			}
		}
		/* while v is even */
		while ((v[0] & 1) == 0) {
			bignum_rightshift(v, v, 1, digits);
			/* if c is even */
			if ((c[0] & 1) == 0) {
				bignum_rightshift(c, c, 1, digits);
			} else {
				c[digits] = bignum_add(c, c, b0, digits);
				bignum_rightshift(c, c, 1, digits + 1);
			}
		}
		if (bignum_cmp(u, v, digits) >= 0) {
			bignum_sub(u, u, v, digits);
			if (bignum_cmp(a, c, digits) < 0)
				bignum_add(a, a, b0, digits);
			bignum_sub(a, a, c, digits);
		} else {
			bignum_sub(v, v, u, digits);
			if (bignum_cmp(c, a, digits) < 0)
			bignum_add(c, c, b0, digits);
			bignum_sub(c, c, a, digits);
		}
	}
	bignum_copy(x, c, digits);
}
