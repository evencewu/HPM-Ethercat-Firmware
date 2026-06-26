/*
 * Copyright (c) 2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __BOOTSTRAP_H__
#define __BOOTSTRAP_H__

#include <stdint.h>

void bootstrap_foe_init(void);
void bootstrap_state(void);
void init_boothook(void);
void boot_inithook(void);

#endif /* __BOOTSTRAP_H__ */
