/*
 * Copyright (c) 2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __UTYPES_H__
#define __UTYPES_H__

#include <stdint.h>

/* Inputs: slave -> master (TxPDO) */
typedef struct
{
   uint8_t status;
} _Rbuffer;

/* Outputs: master -> slave (RxPDO) */
typedef struct
{
   uint8_t LED;
} _Wbuffer;

typedef struct
{
   uint32_t reset_counter;
} _Cbuffer;

#endif /* __UTYPES_H__ */
