// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2025 Oplus. All rights reserved.
 */

#ifndef _UECC_VLI_H_
#define _UECC_VLI_H_

#include "uecc.h"
#include "types.h"

/* Functions for raw large-integer manipulation. These are only available
   if uecc.c is compiled with UECC_ENABLE_VLI_API defined to 1. */
#ifndef UECC_ENABLE_VLI_API
#define UECC_ENABLE_VLI_API 1
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#if UECC_ENABLE_VLI_API

void u_ecc_vli_clear(u_ecc_word_t *vli, wordcount_t num_words);

/* Constant-time comparison to zero - secure way to compare long integers */
/* Returns 1 if vli == 0, 0 otherwise. */
u_ecc_word_t u_ecc_vli_is_zero(const u_ecc_word_t *vli, wordcount_t num_words);

/* Returns nonzero if bit 'bit' of vli is set. */
u_ecc_word_t u_ecc_vli_test_bit(const u_ecc_word_t *vli, bitcount_t bit);

/* Counts the number of bits required to represent vli. */
bitcount_t u_ecc_vli_num_bits(const u_ecc_word_t *vli, const wordcount_t max_words);

/* Sets dest = src. */
void u_ecc_vli_set(u_ecc_word_t *dest, const u_ecc_word_t *src, wordcount_t num_words);

/* Constant-time comparison function - secure way to compare long integers */
/* Returns one if left == right, zero otherwise */
u_ecc_word_t u_ecc_vli_equal(const u_ecc_word_t *left,
			   const u_ecc_word_t *right,
			   wordcount_t num_words);

/* Constant-time comparison function - secure way to compare long integers */
/* Returns sign of left - right, in constant time. */
cmpresult_t u_ecc_vli_cmp(const u_ecc_word_t *left, const u_ecc_word_t *right, wordcount_t num_words);

/* Computes vli = vli >> 1. */
void u_ecc_vli_rshift1(u_ecc_word_t *vli, wordcount_t num_words);

/* Computes result = left + right, returning carry. Can modify in place. */
u_ecc_word_t u_ecc_vli_add(u_ecc_word_t *result,
			 const u_ecc_word_t *left,
			 const u_ecc_word_t *right,
			 wordcount_t num_words);

/* Computes result = left - right, returning borrow. Can modify in place. */
u_ecc_word_t u_ecc_vli_sub(u_ecc_word_t *result,
			 const u_ecc_word_t *left,
			 const u_ecc_word_t *right,
			 wordcount_t num_words);

/* Computes result = left * right. Result must be 2 * num_words long. */
void u_ecc_vli_mult(u_ecc_word_t *result,
		   const u_ecc_word_t *left,
		   const u_ecc_word_t *right,
		   wordcount_t num_words);

/* Computes result = left^2. Result must be 2 * num_words long. */
void u_ecc_vli_square(u_ecc_word_t *result, const u_ecc_word_t *left, wordcount_t num_words);

/* Computes result = (left + right) % mod.
   Assumes that left < mod and right < mod, and that result does not overlap mod. */
void u_ecc_vli_mod_add(u_ecc_word_t *result,
		     const u_ecc_word_t *left,
		     const u_ecc_word_t *right,
		     const u_ecc_word_t *mod,
		     wordcount_t num_words);

/* Computes result = (left - right) % mod.
   Assumes that left < mod and right < mod, and that result does not overlap mod. */
void u_ecc_vli_mod_sub(u_ecc_word_t *result,
		     const u_ecc_word_t *left,
		     const u_ecc_word_t *right,
		     const u_ecc_word_t *mod,
		     wordcount_t num_words);

/* Computes result = product % mod, where product is 2N words long.
   Currently only designed to work for mod == curve->p or curve_n. */
void u_ecc_vli_mmod(u_ecc_word_t *result,
		   u_ecc_word_t *product,
		   const u_ecc_word_t *mod,
		   wordcount_t num_words);

/* Calculates result = product (mod curve->p), where product is up to
   2 * curve->num_words long. */
void u_ecc_vli_mmod_fast(u_ecc_word_t *result, u_ecc_word_t *product, u_ecc_curve curve);

/* Computes result = (left * right) % mod.
   Currently only designed to work for mod == curve->p or curve_n. */
void u_ecc_vli_mod_mult(u_ecc_word_t *result,
		      const u_ecc_word_t *left,
		      const u_ecc_word_t *right,
		      const u_ecc_word_t *mod,
		      wordcount_t num_words);

/* Computes result = (left * right) % curve->p. */
void u_ecc_vli_mod_mult_fast(u_ecc_word_t *result,
			   const u_ecc_word_t *left,
			   const u_ecc_word_t *right,
			   u_ecc_curve curve);

/* Computes result = left^2 % mod.
   Currently only designed to work for mod == curve->p or curve_n. */
void u_ecc_vli_mod_square(u_ecc_word_t *result,
			const u_ecc_word_t *left,
			const u_ecc_word_t *mod,
			wordcount_t num_words);

/* Computes result = left^2 % curve->p. */
void u_ecc_vli_mod_square_fast(u_ecc_word_t *result, const u_ecc_word_t *left, u_ecc_curve curve);

/* Computes result = (1 / input) % mod.*/
void u_ecc_vli_mod_inv(u_ecc_word_t *result,
		     const u_ecc_word_t *input,
		     const u_ecc_word_t *mod,
		     wordcount_t num_words);

#if U_ECC_SUPPORT_COMPRESSED_POINT
/* Calculates a = sqrt(a) (mod curve->p) */
void u_ecc_vli_mod_sqrt(u_ecc_word_t *a, u_ecc_curve curve);
#endif

/* Converts an integer in u_ecc native format to big-endian bytes. */
void u_ecc_vli_native_to_bytes(uint8_t *bytes, int num_bytes, const u_ecc_word_t *native);
/* Converts big-endian bytes to an integer in u_ecc native format. */
void u_ecc_vli_bytes_to_native(u_ecc_word_t *native, const uint8_t *bytes, int num_bytes);

unsigned u_ecc_curve_num_words(u_ecc_curve curve);
unsigned u_ecc_curve_num_bytes(u_ecc_curve curve);
unsigned u_ecc_curve_num_bits(u_ecc_curve curve);
unsigned u_ecc_curve_num_n_words(u_ecc_curve curve);
unsigned u_ecc_curve_num_n_bytes(u_ecc_curve curve);
unsigned u_ecc_curve_num_n_bits(u_ecc_curve curve);

const u_ecc_word_t *u_ecc_curve_p(u_ecc_curve curve);
const u_ecc_word_t *u_ecc_curve_n(u_ecc_curve curve);
const u_ecc_word_t *u_ecc_curve_G(u_ecc_curve curve);
const u_ecc_word_t *u_ecc_curve_b(u_ecc_curve curve);

int u_ecc_valid_point(const u_ecc_word_t *point, u_ecc_curve curve);

/* Multiplies a point by a scalar. Points are represented by the X coordinate followed by
   the Y coordinate in the same array, both coordinates are curve->num_words long. Note
   that scalar must be curve->num_n_words long (NOT curve->num_words). */
void u_ecc_point_mult(u_ecc_word_t *result,
		     const u_ecc_word_t *point,
		     const u_ecc_word_t *scalar,
		     u_ecc_curve curve);

/* Generates a random integer in the range 0 < random < top.
   Both random and top have num_words words. */
int u_ecc_generate_random_int(u_ecc_word_t *random,
			     const u_ecc_word_t *top,
			     wordcount_t num_words);

#endif /* UECC_ENABLE_VLI_API */

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* _UECC_VLI_H_ */
