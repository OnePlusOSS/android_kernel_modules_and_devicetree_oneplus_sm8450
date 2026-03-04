// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2026 Oplus. All rights reserved.
 */

#ifndef _UECC_H_
#define _UECC_H_

#include <linux/types.h>

/* Platform selection options.
If U_ECC_PLATFORM is not defined, the code will try to guess it based on compiler macros.
Possible values for U_ECC_PLATFORM are defined below: */
#define U_ECC_ARCH_OTHER 0
#define U_ECC_X86	1
#define U_ECC_X86_64	2
#define U_ECC_ARM	3
#define U_ECC_ARM_THUMB	4
#define U_ECC_ARM_THUMB2 5
#define U_ECC_ARM64	6
#define U_ECC_AVR	7

/* If desired, you can define U_ECC_WORD_SIZE as appropriate for your platform (1, 4, or 8 bytes).
If U_ECC_WORD_SIZE is not explicitly defined then it will be automatically set based on your
platform. */

/* Optimization level; trade speed for code size.
   Larger values produce code that is faster but larger.
   Currently supported values are 0 - 4; 0 is unusably slow for most applications.
   Optimization level 4 currently only has an effect ARM platforms where more than one
   curve is enabled. */
#ifndef U_ECC_OPTIMIZATION_LEVEL
#define U_ECC_OPTIMIZATION_LEVEL 2
#endif

/* U_ECC_SQUARE_FUNC - If enabled (defined as nonzero), this will cause a specific function to be
used for (scalar) squaring instead of the generic multiplication function. This can make things
faster somewhat faster, but increases the code size. */
#ifndef U_ECC_SQUARE_FUNC
#define U_ECC_SQUARE_FUNC 0
#endif

/* U_ECC_VLI_NATIVE_LITTLE_ENDIAN - If enabled (defined as nonzero), this will switch to native
little-endian format for *all* arrays passed in and out of the public API. This includes public
and private keys, shared secrets, signatures and message hashes.
Using this switch reduces the amount of call stack memory used by u_ecc, since less intermediate
translations are required.
Note that this will *only* work on native little-endian processors and it will treat the uint8_t
arrays passed into the public API as word arrays, therefore requiring the provided byte arrays
to be word aligned on architectures that do not support unaligned accesses.
IMPORTANT: Keys and signatures generated with U_ECC_VLI_NATIVE_LITTLE_ENDIAN=1 are incompatible
with keys and signatures generated with U_ECC_VLI_NATIVE_LITTLE_ENDIAN=0; all parties must use
the same endianness. */
#ifndef U_ECC_VLI_NATIVE_LITTLE_ENDIAN
#define U_ECC_VLI_NATIVE_LITTLE_ENDIAN 1
#endif

/* Curve support selection. Set to 0 to remove that curve. */
#ifndef U_ECC_SUPPORTS_SECP160R1
#define U_ECC_SUPPORTS_SECP160R1 1
#endif
#ifndef U_ECC_SUPPORTS_SECP192R1
#define U_ECC_SUPPORTS_SECP192R1 1
#endif
#ifndef U_ECC_SUPPORTS_SECP224R1
#define U_ECC_SUPPORTS_SECP224R1 1
#endif
#ifndef U_ECC_SUPPORTS_SECP256R1
#define U_ECC_SUPPORTS_SECP256R1 1
#endif
#ifndef U_ECC_SUPPORTS_SECP256K1
#define U_ECC_SUPPORTS_SECP256K1 1
#endif

/* Specifies whether compressed point format is supported.
   Set to 0 to disable point compression/decompression functions. */
#ifndef U_ECC_SUPPORT_COMPRESSED_POINT
#define U_ECC_SUPPORT_COMPRESSED_POINT 1
#endif

struct u_ecc_curve_t;
typedef const struct u_ecc_curve_t * u_ecc_curve;

#ifdef __cplusplus
extern "C"
{
#endif

#if U_ECC_SUPPORTS_SECP160R1
u_ecc_curve u_ecc_secp160r1(void);
#endif
#if U_ECC_SUPPORTS_SECP192R1
u_ecc_curve u_ecc_secp192r1(void);
#endif
#ifdef U_ECC_SUPPORTS_SECP192K1
u_ecc_curve u_ecc_secp192k1(void);
#endif
#if U_ECC_SUPPORTS_SECP224R1
u_ecc_curve u_ecc_secp224r1(void);
#endif
#if U_ECC_SUPPORTS_SECP256R1
u_ecc_curve u_ecc_secp256r1(void);
#endif
#if U_ECC_SUPPORTS_SECP256K1
u_ecc_curve u_ecc_secp256k1(void);
#endif

/* u_ecc_rng_function type
The RNG function should fill 'size' random bytes into 'dest'. It should return 1 if
'dest' was filled with random data, or 0 if the random data could not be generated.
The filled-in values should be either truly random, or from a cryptographically-secure PRNG.

A correctly functioning RNG function must be set (using u_ecc_set_rng()) before calling
u_ecc_make_key() or u_ecc_sign().

Setting a correctly functioning RNG function improves the resistance to side-channel attacks
for u_ecc_shared_secret() and u_ecc_sign_deterministic().

A correct RNG function is set by default when building for Windows, Linux, or OS X.
If you are building on another POSIX-compliant system that supports /dev/random or /dev/urandom,
you can define U_ECC_POSIX to use the predefined RNG. For embedded platforms there is no predefined
RNG function; you must provide your own.
*/
typedef int (*u_ecc_rng_function)(uint8_t *dest, unsigned size);

/* u_ecc_set_rng() function.
Set the function that will be used to generate random bytes. The RNG function should
return 1 if the random data was generated, or 0 if the random data could not be generated.

On platforms where there is no predefined RNG function (eg embedded platforms), this must
be called before u_ecc_make_key() or u_ecc_sign() are used.

Inputs:
    rng_function - The function that will be used to generate random bytes.
*/
void u_ecc_set_rng(u_ecc_rng_function rng_function);

/* u_ecc_get_rng() function.

Returns the function that will be used to generate random bytes.
*/
u_ecc_rng_function u_ecc_get_rng(void);

/* u_ecc_curve_private_key_size() function.

Returns the size of a private key for the curve in bytes.
*/
int u_ecc_curve_private_key_size(u_ecc_curve curve);

/* u_ecc_curve_public_key_size() function.

Returns the size of a public key for the curve in bytes.
*/
int u_ecc_curve_public_key_size(u_ecc_curve curve);

/* u_ecc_make_key() function.
Create a public/private key pair.

Outputs:
    public_key  - Will be filled in with the public key. Must be at least 2 * the curve size
		  (in bytes) long. For example, if the curve is secp256r1, public_key must be 64
		  bytes long.
    private_key - Will be filled in with the private key. Must be as long as the curve order; this
		  is typically the same as the curve size, except for secp160r1. For example, if the
		  curve is secp256r1, private_key must be 32 bytes long.

		  For secp160r1, private_key must be 21 bytes long! Note that the first byte will
		  almost always be 0 (there is about a 1 in 2^80 chance of it being non-zero).

Returns 1 if the key pair was generated successfully, 0 if an error occurred.
*/
int u_ecc_make_key(uint8_t *public_key, uint8_t *private_key, u_ecc_curve curve);

/* u_ecc_shared_secret() function.
Compute a shared secret given your secret key and someone else's public key. If the public key
is not from a trusted source and has not been previously verified, you should verify it first
using u_ecc_valid_public_key().
Note: It is recommended that you hash the result of u_ecc_shared_secret() before using it for
symmetric encryption or HMAC.

Inputs:
    public_key  - The public key of the remote party.
    private_key - Your private key.

Outputs:
    secret - Will be filled in with the shared secret value. Must be the same size as the
	     curve size; for example, if the curve is secp256r1, secret must be 32 bytes long.

Returns 1 if the shared secret was generated successfully, 0 if an error occurred.
*/
int u_ecc_shared_secret(const uint8_t *public_key,
		       const uint8_t *private_key,
		       uint8_t *secret,
		       u_ecc_curve curve);

#if U_ECC_SUPPORT_COMPRESSED_POINT
/* u_ecc_compress() function.
Compress a public key.

Inputs:
    public_key - The public key to compress.

Outputs:
    compressed - Will be filled in with the compressed public key. Must be at least
		 (curve size + 1) bytes long; for example, if the curve is secp256r1,
		 compressed must be 33 bytes long.
*/
void u_ecc_compress(const uint8_t *public_key, uint8_t *compressed, u_ecc_curve curve);

/* u_ecc_decompress() function.
Decompress a compressed public key.

Inputs:
    compressed - The compressed public key.

Outputs:
    public_key - Will be filled in with the decompressed public key.
*/
void u_ecc_decompress(const uint8_t *compressed, uint8_t *public_key, u_ecc_curve curve);
#endif /* U_ECC_SUPPORT_COMPRESSED_POINT */

/* u_ecc_valid_public_key() function.
Check to see if a public key is valid.

Note that you are not required to check for a valid public key before using any other u_ecc
functions. However, you may wish to avoid spending CPU time computing a shared secret or
verifying a signature using an invalid public key.

Inputs:
    public_key - The public key to check.

Returns 1 if the public key is valid, 0 if it is invalid.
*/
int u_ecc_valid_public_key(const uint8_t *public_key, u_ecc_curve curve);

/* u_ecc_compute_public_key() function.
Compute the corresponding public key for a private key.

Inputs:
    private_key - The private key to compute the public key for

Outputs:
    public_key - Will be filled in with the corresponding public key

Returns 1 if the key was computed successfully, 0 if an error occurred.
*/
int u_ecc_compute_public_key(const uint8_t *private_key, uint8_t *public_key, u_ecc_curve curve);

/* u_ecc_sign() function.
Generate an ECDSA signature for a given hash value.

Usage: Compute a hash of the data you wish to sign (SHA-2 is recommended) and pass it in to
this function along with your private key.

Inputs:
    private_key  - Your private key.
    message_hash - The hash of the message to sign.
    hash_size    - The size of message_hash in bytes.

Outputs:
    signature - Will be filled in with the signature value. Must be at least 2 * curve size long.
		For example, if the curve is secp256r1, signature must be 64 bytes long.

Returns 1 if the signature generated successfully, 0 if an error occurred.
*/
int u_ecc_sign(const uint8_t *private_key,
	      const uint8_t *message_hash,
	      unsigned hash_size,
	      uint8_t *signature,
	      u_ecc_curve curve);

/* struct u_ecc_hash_context structure.
This is used to pass in an arbitrary hash function to u_ecc_sign_deterministic().
The structure will be used for multiple hash computations; each time a new hash
is computed, init_hash() will be called, followed by one or more calls to
update_hash(), and finally a call to finish_hash() to produce the resulting hash.

The intention is that you will create a structure that includes struct u_ecc_hash_context
followed by any hash-specific data. For example:

structsha256_hash_context {
    struct u_ecc_hash_context u_ecc;
    struct sha256_ctx_t ctx;
};

void init_sha256(struct u_ecc_hash_context *base) {
    struct sha256_hash_context *context = (struct sha256_hash_context *)base;
    sha256_Init(&context->ctx);
}

void update_sha256(struct u_ecc_hash_context *base,
		   const uint8_t *message,
		   unsigned message_size) {
    struct sha256_hash_context *context = (struct sha256_hash_context *)base;
    sha256_update(&context->ctx, message, message_size);
}

void finish_SHA256(struct u_ecc_hash_context *base, uint8_t *hash_result) {
    struct sha256_hash_context *context = (struct sha256_hash_context *)base;
    sha256_final(hash_result, &context->ctx);
}

... when signing ...
{
    uint8_t tmp[32 + 32 + 64];
    struct sha256_hash_context ctx = {{&init_sha256, &update_sha256, &finish_SHA256, 64, 32, tmp}};
    u_ecc_sign_deterministic(key, message_hash, &ctx.u_ecc, signature);
}
*/
struct u_ecc_hash_context {
	void (*init_hash)(const struct u_ecc_hash_context *context);
	void (*update_hash)(const struct u_ecc_hash_context *context,
			const uint8_t *message,
			unsigned message_size);
	void (*finish_hash)(const struct u_ecc_hash_context *context, uint8_t *hash_result);
	unsigned block_size; /* Hash function block size in bytes, eg 64 for SHA-256. */
	unsigned result_size; /* Hash function result size in bytes, eg 32 for SHA-256. */
	uint8_t *tmp; /* Must point to a buffer of at least (2 * result_size + block_size) bytes. */
};

/* u_ecc_sign_deterministic() function.
Generate an ECDSA signature for a given hash value, using a deterministic algorithm
(see RFC 6979). You do not need to set the RNG using u_ecc_set_rng() before calling
this function; however, if the RNG is defined it will improve resistance to side-channel
attacks.

Usage: Compute a hash of the data you wish to sign (SHA-2 is recommended) and pass it to
this function along with your private key and a hash context. Note that the message_hash
does not need to be computed with the same hash function used by hash_context.

Inputs:
    private_key  - Your private key.
    message_hash - The hash of the message to sign.
    hash_size    - The size of message_hash in bytes.
    hash_context - A hash context to use.

Outputs:
    signature - Will be filled in with the signature value.

Returns 1 if the signature generated successfully, 0 if an error occurred.
*/
int u_ecc_sign_deterministic(const uint8_t *private_key,
			    const uint8_t *message_hash,
			    unsigned hash_size,
			    const struct u_ecc_hash_context *hash_context,
			    uint8_t *signature,
			    u_ecc_curve curve);

/* u_ecc_verify() function.
Verify an ECDSA signature.

Usage: Compute the hash of the signed data using the same hash as the signer and
pass it to this function along with the signer's public key and the signature values (r and s).

Inputs:
    public_key   - The signer's public key.
    message_hash - The hash of the signed data.
    hash_size    - The size of message_hash in bytes.
    signature    - The signature value.

Returns 1 if the signature is valid, 0 if it is invalid.
*/
int u_ecc_verify(const uint8_t *public_key,
		const uint8_t *message_hash,
		unsigned hash_size,
		const uint8_t *signature,
		u_ecc_curve curve);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* _UECC_H_ */
