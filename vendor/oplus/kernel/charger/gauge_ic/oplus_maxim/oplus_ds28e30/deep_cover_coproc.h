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

/* misc constants */
#ifndef TRUE
#define TRUE    1
#endif
#ifndef FALSE
#define FALSE   0
#endif

#define MESSAGE_MAX_LEN 256
#define SUCCESS_FINISHED 0
#define ZERO_VALUE  0
#define BYTE_VALUE_1  1
#define BYTE_VALUE_3  3
#define BYTE_VALUE_6  6
#define BYTE_VALUE_8  8
#define BYTE_VALUE_12  12
#define BYTE_VALUE_55  0x55
#define BYTE_VALUE_3F  0x3F

#define BYTE_VALUE_7F  0x7F
#define BYTE_VALUE_80  0x80
#define BYTE_VALUE_E0  0xE0
#define BYTE_VALUE_AA  0xAA
#define BYTE_VALUE_03  0x03
#define BYTE_VALUE_04  0x04
#define BYTE_VALUE_0F  0x0F
#define BYTE_VALUE_F0  0xF0
#define BYTE_VALUE_FF  0xFF

#define SHORT_VALUE_C001  0xC001


#define BYTE_LENGTH_1  1
#define BYTE_LENGTH_2  2
#define BYTE_LENGTH_3  3
#define BYTE_LENGTH_4  4
#define BYTE_LENGTH_8  8
#define BYTE_LENGTH_11  11
#define BYTE_LENGTH_16  16
#define BYTE_LENGTH_24  24
#define BYTE_LENGTH_29  29
#define BYTE_LENGTH_32  32
#define BYTE_LENGTH_56  56
#define BYTE_LENGTH_63  63
#define BYTE_LENGTH_64  64
#define BYTE_LENGTH_120  120



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
