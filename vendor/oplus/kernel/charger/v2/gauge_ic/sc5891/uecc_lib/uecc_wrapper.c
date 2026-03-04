// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2026 Oplus. All rights reserved.
 */
#include <linux/types.h>
#include <linux/string.h>
#include <linux/module.h>
#include "micro_ecc/uecc.h"
#include "micro_ecc/types.h"
#include "micro_ecc/uecc_vli.h"
#include "sha256.h"

#if U_ECC_WORD_SIZE == 1
	#define PRINT_FORMAT "%02X"
#elif U_ECC_WORD_SIZE == 4
	#define PRINT_FORMAT "%08X"
#elif U_ECC_WORD_SIZE == 8
	#define PRINT_FORMAT "%016lX"
#else
	#error "Unsupported U_ECC_WORD_SIZE"
#endif

#ifndef DEBUG_PRINT
#define DEBUG_PRINT 0
#endif

#define KBLZ_MESSAGE_SIZE		12
#define KBLZ_ROMID_SIZE			8
#define SECP256R1_PRIVATE_KEY_SIZE	32
#define SECP256R1_PUBLIC_KEY_SIZE	64

void hex_string_to_bytes(const char *hex_string, uint8_t *byte_array, size_t byte_array_size)
{
	size_t hex_length = strlen(hex_string);
	size_t i;
	if (hex_length % 2 != 0 || hex_length / 2 > byte_array_size)
		return;

	memset(byte_array, 0, byte_array_size);

	for (i = 0; i < hex_length / 2; ++i)
		sscanf(hex_string + 2 * i, "%2hhx", &byte_array[byte_array_size - (hex_length / 2) + i]);
}

void print_hex(const uint8_t *data, size_t size)
{
	/* Skip leading zeros */
	size_t start = 0;
	size_t i;

	while (start < size && data[start] == 0)
		++start;

	if (start == size) {
		printk("0");
	} else {
		for (i = start; i < size; ++i)
			printk("%02x", data[i]);
	}
	printk("\n");
}

int make_key_pair(uint8_t *public_key, uint8_t *private_key)
{
	u_ecc_curve curve = u_ecc_secp256r1();

	return (u_ecc_make_key(public_key, private_key, curve));
}

int gen_sign(const uint8_t *pri_key, uint8_t *romid, uint8_t *message, uint8_t *signature)
{
	u_ecc_curve curve = u_ecc_secp256r1();

	uint8_t combined_data[KBLZ_ROMID_SIZE + KBLZ_MESSAGE_SIZE];
	uint8_t hash[SHA256_DIGESTLEN];

	memmove(combined_data, romid, KBLZ_ROMID_SIZE);
	memmove(combined_data + KBLZ_ROMID_SIZE, message, KBLZ_MESSAGE_SIZE);
	calculate_sha256(combined_data, sizeof(combined_data), hash);

	return u_ecc_sign(pri_key, hash, sizeof(hash), signature, curve);
}

int verify_public_key(const uint8_t *pub_key, const uint8_t *pri_key)
{
	u_ecc_curve curve = u_ecc_secp256r1();

	uint8_t sw_computed_pub_key_bytes[SECP256R1_PUBLIC_KEY_SIZE];

	u_ecc_compute_public_key(pri_key, sw_computed_pub_key_bytes, curve);

#if DEBUG_PRINT
	printk("------------verify_public_key------------\n");
	printk("input Public key: ");
	print_hex(pub_key, 64);
	printk("input Private key: ");
	print_hex(pri_key, 32);
	printk("computed Public key: ");
	print_hex(sw_computed_pub_key_bytes, 64);
	printk("-----------------------------------\n");
#endif

	return !memcmp(sw_computed_pub_key_bytes, pub_key, sizeof(sw_computed_pub_key_bytes));
}

int verify_sign(const uint8_t *pub_key, uint8_t *romid, uint8_t *message, const uint8_t *sig)
{
	u_ecc_curve curve = u_ecc_secp256r1();

	uint8_t combined_message[KBLZ_ROMID_SIZE + KBLZ_MESSAGE_SIZE];
	uint8_t hash[SHA256_DIGESTLEN];

	memmove(combined_message, romid, KBLZ_ROMID_SIZE);
	memmove(combined_message + KBLZ_ROMID_SIZE, message, KBLZ_MESSAGE_SIZE);

	calculate_sha256(combined_message, sizeof(combined_message), hash);

#if DEBUG_PRINT
	printk("------------verify_sign------------\n");
	printk("input Public key: ");
	print_hex(pub_key, 64);
	printk("input Signature: ");
	print_hex(sig, 64);
	printk("input hash: ");
	print_hex(hash, 32);
	printk("-----------------------------------\n");
#endif

	if (u_ecc_verify(pub_key, hash, sizeof(hash), sig, curve))
		return 1;
	else
		return 0;
}

int verify_cert(const uint8_t *pub_key, uint8_t *chip_pub, const uint8_t *certificate)
{
	u_ecc_curve curve = u_ecc_secp256r1();
	uint8_t hash[32];
	/* Calculate hash directly */
	calculate_sha256(chip_pub, 64, hash);
	if (u_ecc_verify(pub_key, hash, sizeof(hash), certificate, curve))
		return 1;
	else
		return 0;
}
