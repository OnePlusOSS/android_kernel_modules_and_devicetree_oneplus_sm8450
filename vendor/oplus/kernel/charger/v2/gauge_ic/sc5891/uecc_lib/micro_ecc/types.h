// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2025 Oplus. All rights reserved.
 */

#ifndef _UECC_TYPES_H_
#define _UECC_TYPES_H_

#define U_ECC_PLATFORM U_ECC_ARM64
#define U_ECC_ARM_USE_UMAAL 0
#define U_ECC_WORD_SIZE 8

#if defined(__SIZEOF_INT128__) || ((__clang_major__ * 100 + __clang_minor__) >= 302)
	#define SUPPORTS_INT128 1
#else
	#define SUPPORTS_INT128 0
#endif

typedef int8_t wordcount_t;
typedef int16_t bitcount_t;
typedef int8_t cmpresult_t;

#if (U_ECC_WORD_SIZE == 1)

typedef uint8_t u_ecc_word_t;
typedef uint16_t u_ecc_dword_t;

#define HIGH_BIT_SET 0x80
#define UECC_WORD_BITS 8
#define UECC_WORD_BITS_SHIFT 3
#define UECC_WORD_BITS_MASK 0x07

#elif (U_ECC_WORD_SIZE == 4)

typedef uint32_t u_ecc_word_t;
typedef uint64_t u_ecc_dword_t;

#define HIGH_BIT_SET 0x80000000
#define UECC_WORD_BITS 32
#define UECC_WORD_BITS_SHIFT 5
#define UECC_WORD_BITS_MASK 0x01F

#elif (U_ECC_WORD_SIZE == 8)

typedef uint64_t u_ecc_word_t;
#if SUPPORTS_INT128
typedef unsigned __int128 u_ecc_dword_t;
#endif

#define HIGH_BIT_SET 0x8000000000000000ull
#define UECC_WORD_BITS 64
#define UECC_WORD_BITS_SHIFT 6
#define UECC_WORD_BITS_MASK 0x03F

#endif /* U_ECC_WORD_SIZE */

#endif /* _UECC_TYPES_H_ */
