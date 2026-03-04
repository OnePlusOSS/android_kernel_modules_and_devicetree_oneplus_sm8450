// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#ifndef _DEFS_H_
#define _DEFS_H_

#include "ucl_config.h"
#include "ucl_types.h"
#include "ucl_defs.h"
#include "ucl_retdefs.h"

/** Number probably prime.
 * @ingroup UCL_FPA
 */
#define UCL_IS_PRIME	UCL_TRUE
/** Number composite.
 * @ingroup UCL_FPA
 */
#define UCL_ISNOT_PRIME	UCL_FALSE

#if __mips16e
#define __nomips16__	__attribute__((nomips16))
#else
#define __nomips16__
#endif

#endif /* _DEFS_H_ */
