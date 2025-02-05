// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#ifndef _UCL_CONFIG_H_
#define _UCL_CONFIG_H_


#ifdef __MINGW32__

#ifdef BUILD_SHARED_LIB
#define __API__ __declspec(dllexport)
#else
#define __API__ __declspec(dllimport)
#endif

#elif defined __GCC__

#ifdef BUILD_SHARED_LIB
#if __GNUC__ >= 4
#define __API__ __attribute__((visibility ("default")))
#else
#define __API__
#endif
#else
#define __API__
#endif

#else
#define __API__
#endif

/* JIBE_LINUX_CRYPTO_HW */
#define JIBE_LINUX_CRYPTO_HW

#if defined (__jibe) && defined (__linux) && defined(JIBE_LINUX_CRYPTO_HW)
#warning JIBE target will use the userland API to the kernel crypto drivers
#define JIBE_LINUX_HW
#endif

/** <b>UCL Stack default size</b>.
 * 8 Ko.
 * @ingroup UCL_CONFIG */
#define UCL_STACK_SIZE	(8 * 1024)

/** <b>UCL RSA key max size</b>.
 * 512 bytes: 4096 bits.
 * @ingroup UCL_CONFIG
 */
/* 1024 is ok on mingw for rsa encrypt up to 3072 */
/* but seems to be too large for jibe stack */
#define UCL_RSA_KEY_MAXSIZE	512

/** <b>UCL RSA public exponent max size</b>.
 * 4 bytes: 32 bits.
 * @ingroup UCL_CONFIG */
#define UCL_RSA_PUBLIC_EXPONENT_MAXSIZE	4

/** <b>UCL ECC Precision</b>.
 * @ingroup UCL_CONFIG */
#define UCL_ECC_PRECISION	17

#endif /* _UCL_CONFIG_H_ */
