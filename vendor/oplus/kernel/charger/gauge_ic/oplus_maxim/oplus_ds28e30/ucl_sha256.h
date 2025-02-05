// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#ifndef _UCL_SHA256_H_
#define _UCL_SHA256_H_

#include "ucl_types.h"

#define __API__
#include "ucl_hash.h"
#ifdef HASH_SHA256

/** @file ucl_sha256.h
 * @defgroup UCL_SHA256 SHA256
 * Secure Hash Algorithm 256, from FIPS 180-2.
 *
 * @par Header:
 * @link ucl_sha256.h ucl_sha256.h @endlink
 *
 * SHA-256 is a data-digest algorithm and a component of
 * the Standard FIPS 180-2.
 * The algorithm takes as input a data of arbitrary length and
 * produces as output a 256-bit "fingerprint" or "data digest"
 * of the input.@n
 * @n
 * Given a data of any length, a single "1" bit is appended at the end of the
 * data. This bit must always be added. Then, a set of "0" bits are also
 * appended so that the data length is 448 modulo 512. Finally, the length
 * of the data is appended. @n
 *@n
 *
 * <b>SHA256 Descriptor:</b> @n
 * @li Block length : 512 bits
 * @li Hash length : 256 bits
 *
 * @ingroup UCL_HASH
 */

/** <b>The SHA256 context</b>.
 * This structure is associated to the 'step by step' process.
 *
 * @ingroup UCL_SHA256
 */

struct ucl_sha256_ctx_t
{
	/** Intermediate and then final hash. */
	u32 state[8];
	/** Counter in bits. */
	u32 count[2];
	/** Buffer. */
	u8 buffer[64];
};

/** <b>The SHA256 context</b>.
 * @ingroup UCL_SHA256
 */

/** <b>Core block size</b>.
 * Byte size of a SHA256 core block.
 *
 * @ingroup UCL_SHA256
 */
#define UCL_SHA256_BLOCKSIZE	64
/** <b>Hash size</b>.
 * Byte size of the output of SHA256.
 *
 * @ingroup UCL_SHA256
 */
#define UCL_SHA256	1
#define UCL_SHA256_HASHSIZE	32

/** <b>Hash size</b>.
 * 32-bits word size of the output of SHA256.
 *
 * @ingroup UCL_SHA256
 */
#define UCL_SHA256_HASHW32SIZE	8

/** <b>Core block size</b>.
 * Byte size of a SIA256 core block.
 *
 * @ingroup UCL_SIA256
 */
#define UCL_SIA256_BLOCKSIZE	64
/** <b>Hash size</b>.
 * Byte size of the output of SIA256.
 *
 * @ingroup UCL_SIA256
 */
#define UCL_SIA256	6
#define UCL_SIA256_HASHSIZE	32
/** <b>Hash size</b>.
 * 32-bits word size of the output of SIA256.
 *
 * @ingroup UCL_SIA256
 */
#define UCL_SIA256_HASHW32SIZE	8

/** <b>SHA256</b>.
 * The complete process of SHA256.
 *
 * @pre Hash byte length is equal to 32
 *
 * @param[out] hash         Pointer to the digest
 * @param[in]  data         Pointer to the data
 * @param[in]  data_byteLen Data byte length
 *
 * @return Error code
 *
 * @retval #UCL_OK             if no error occurred
 * @retval #UCL_INVALID_OUTPUT if @p hash is #NULL
 *
 * @ingroup UCL_SHA256
 */
int __API__ ucl_sha256(u8 *hash, u8 *data, u32 data_bytelen);


/** <b>SHA256 Init</b>.
 * The initialisation of SHA256.
 *
 * @param[in,out] context Pointer to the context
 *
 * @return Error code
 *
 * @retval #UCL_OK            if no error occurred
 * @retval #UCL_INVALID_INPUT if @p context is #NULL
 *
 * @ingroup UCL_SHA256
 */
int __API__ ucl_sha256_init(struct ucl_sha256_ctx_t *context);

/** <b>SHA256 Core</b>.
 * The core of SHA256.
 *
 * @param[in,out] context      Pointer to the context
 * @param[in]     data         Pointer to the data
 * @param[in]     data_byteLen Data byte length
 *
 * @warning #ucl_sha256_init must be processed before, and
 *          #ucl_sha256_finish should be processed to get the final hash.
 *
 * @return Error code
 *
 * @retval #UCL_OK  if no error occurred
 * @retval #UCL_NOP if @p data_byteLen = 0 or if @p data is the pointer #NULL
 *
 * @ingroup UCL_SHA256
 */
int __API__ ucl_sha256_core(struct ucl_sha256_ctx_t *context, u8 *data,
                    u32 data_bytelen);

/** <b>SHA256 Finish</b>.
 * Finish the process of SHA256.
 *
 * @pre Hash byte length is equal to 32
 *
 * @param[out]    hash Pointer to the digest
 * @param[in,out] context Pointer to the context
 *
 * @warning #ucl_sha256_init and #ucl_sha256_core must be processed before.
 *
 * @return Error code
 *
 * @retval #UCL_OK             if no error occurred
 * @retval #UCL_INVALID_OUTPUT if @p hash is the pointer #NULL
 * @retval #UCL_INVALID_INPUT  if @p context is the pointer #NULL
 *
 * @ingroup UCL_SHA256
 */
int __API__ ucl_sha256_finish(u8 *hash, struct ucl_sha256_ctx_t *context);

#endif /* HASH_SHA256 */
#endif /* _UCL_SHA256_H_ */
