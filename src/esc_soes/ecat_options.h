/*
 * Copyright (c) 2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __ECAT_OPTIONS_H__
#define __ECAT_OPTIONS_H__

#define USE_FOE          1
#define USE_EOE          0

#define MBXSIZE          128
#define MBXSIZEBOOT      128       /* MUST match MBXSIZE — HPM5E00 ESC SM regs are read-only */
#define MBXBUFFERS       3

#define MBX0_sma         0x1000
#define MBX0_sml         MBXSIZE
#define MBX0_sme         MBX0_sma+MBX0_sml-1
#define MBX0_smc         0x26

#define MBX1_sma         MBX0_sma+MBX0_sml
#define MBX1_sml         MBXSIZE
#define MBX1_sme         MBX1_sma+MBX1_sml-1
#define MBX1_smc         0x22

/* BOOT mailbox config: same PSA/size as normal because SM regs are read-only on HPM5E00 */
#define MBX0_sma_b       MBX0_sma
#define MBX0_sml_b       MBX0_sml
#define MBX0_sme_b       MBX0_sme
#define MBX0_smc_b       MBX0_smc

#define MBX1_sma_b       MBX1_sma
#define MBX1_sml_b       MBX1_sml
#define MBX1_sme_b       MBX1_sme
#define MBX1_smc_b       MBX1_smc

#define SM2_sma          0x1100
#define SM2_smc          0x24
#define SM2_act          1

#define SM3_sma          0x1180
#define SM3_smc          0x20
#define SM3_act          1

#define MAX_RXPDO_SIZE   1
#define MAX_TXPDO_SIZE   1
#define MAX_MAPPINGS_SM2 1
#define MAX_MAPPINGS_SM3 1

#endif /* __ECAT_OPTIONS_H__ */
