/*
 * Copyright (c) 2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "board.h"
#include "ecat_slv.h"
#include "esc_hw.h"
#include "utypes.h"
#include "bootstrap.h"
#include "hpm_esc_drv.h"

/* Full ESC init, defined in esc_init.c */
void esc_full_init(void);

_Rbuffer Rb;
_Wbuffer Wb;
_Cbuffer Cb;

void cb_get_inputs(void)
{
    Rb.status = Cb.reset_counter & 0xFF;
}

void cb_set_outputs(void)
{
    /* LED is toggled by the main loop heartbeat; reflect RxPDO value if needed. */
    (void)Wb.LED;
}

void post_state_change_hook(uint8_t *as, uint8_t *an)
{

    if ((*as == BOOT_TO_INIT) && (*an == ESCinit)) {
        boot_inithook();
    } else if ((*as == INIT_TO_BOOT) && (*an & ESCerror) == 0) {
        init_boothook();
    }
}

int main(void)
{
    static esc_cfg_t config = {
        .user_arg = NULL,
        .use_interrupt = 0,
        .watchdog_cnt = 1000,
        .set_defaults_hook = NULL,
        .pre_state_change_hook = NULL,
        .post_state_change_hook = post_state_change_hook,
        .application_hook = NULL,
        .safeoutput_override = NULL,
        .pre_object_download_hook = NULL,
        .post_object_download_hook = NULL,
        .rxpdo_override = NULL,
        .txpdo_override = NULL,
        .esc_hw_interrupt_enable = NULL,
        .esc_hw_interrupt_disable = NULL,
        .esc_hw_eep_handler = ESC_eep_handler
    };

    board_init();
    board_init_led_pins();
    printf("[0] board init done\n"); fflush(stdout);

    printf("[1] SOES FoE demo starting\n"); fflush(stdout);

    printf("[2] esc_full_init...\n"); fflush(stdout);
    esc_full_init();
    printf("[2] esc_full_init done\n"); fflush(stdout);

    printf("[3] bootstrap_foe_init...\n"); fflush(stdout);
    bootstrap_foe_init();
    printf("[3] bootstrap done\n"); fflush(stdout);

    printf("[4] ecat_slv_init...\n"); fflush(stdout);
    ecat_slv_init(&config);
    printf("[4] ecat_slv_init done, entering loop\n"); fflush(stdout);

    uint32_t heartbeat = 0;
    while (1) {
        ecat_slv();
        bootstrap_state();

        heartbeat++;
        if ((heartbeat & 0x7FFF) == 0) {
            board_led_toggle();
        }
    }

    return 0;
}
