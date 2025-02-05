// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

/** @file deep_cover_coproc.h
*   @brief  Include file for coprocessor functions to support
*    DS28C36/DS2476.  Implementation could be either software or
*    DS2476 hardware.
*/

#ifndef _DEEP_COVER_COPROC_H
#define _DEEP_COVER_COPROC_H

/* Keys */
#define ECDSA_KEY_A				0x00
#define ECDSA_KEY_B				0x01
#define ECDSA_KEY_C				0x02
#define ECDSA_KEY_S				0x03

#ifndef DEEP_COVER_COPROC

int deep_cover_verify_ecdsa_signature(u8 *message, int msg_len, u8 *pubkey_x,
					u8 *pubkey_y, u8 *sig_r, u8 *sig_s);
int deep_cover_compute_ecdsa_signature(u8 *message, int msg_len, u8 *priv_key, u8 *sig_r, u8 *sig_s);
int deep_cover_create_ecdsa_certificate(u8 *sig_r, u8 *sig_s,
					u8 *pub_x, u8 *pub_y,
					u8 *custom_cert_fields, int cert_len,
					u8 *priv_key);
int deep_cover_verify_ecdsa_certificate(u8 *sig_r, u8 *sig_s,
					u8 *pub_x, u8 *pub_y,
					u8 *custom_cert_fields, int cert_len,
					u8 *ver_pubkey_x, u8 *ver_pubkey_y);
int deep_cover_coproc_setup(int write_master_secret, int coproc_ecdh_key, int coproc_pauth_key, int verify_auth_key);

#endif
#endif /* _DEEP_COVER_COPROC_H */
