// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2026 Oplus. All rights reserved.
 */
#ifndef UECC_WRAPPER_H
#define UECC_WRAPPER_H


void trans_auth_to_le(uint8_t *des, const uint8_t *src, size_t size);

void trans_endian(uint8_t *des, const uint8_t *src, size_t size);

void hex_string_to_bytes(const char *hex_string, uint8_t *byte_array, size_t byte_array_size);

void print_hex(const uint8_t *data, size_t size);

int make_key_pair(uint8_t *public_key, uint8_t *private_key);

int gen_sign(const uint8_t *pri_key, uint8_t *romid, uint8_t *message, uint8_t *signature);

int verify_public_key(const uint8_t *pub_key, const uint8_t *pri_key);

int verify_sign(const uint8_t *pub_key, uint8_t *romid, uint8_t *message, const uint8_t *sig);

int verify_cert(const uint8_t *pub_key, uint8_t *chip_pub, const uint8_t *certificate);

#endif /* UECC_WRAPPER_H */
