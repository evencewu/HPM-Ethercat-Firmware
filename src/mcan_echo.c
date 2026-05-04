/*
 * Copyright (c) 2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * CAN 2.0 Echo: 1Mbps, 29-bit extended frame, 1Hz TX/RX
 * ID: 0x0000007F
 * Data: 8 bytes of zeros
 */

#include <stdio.h>
#include <string.h>
#include "board.h"
#include "hpm_mcan_drv.h"

#define CAN_APP_BAUDRATE          (1000000U)
#define CAN_APP_EXT_ID            (0x0001FD7FU)
#define CAN_APP_TX_PERIOD_MS      (1000U)

static volatile bool has_new_rx_msg;
static volatile bool has_tx_complete;
static volatile mcan_rx_message_t s_rx_msg;

ATTR_PLACE_AT(".ahb_sram") uint32_t mcan_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];

SDK_DECLARE_EXT_ISR_M(BOARD_APP_CAN_IRQn, board_can_isr)
void board_can_isr(void)
{
    MCAN_Type *base = BOARD_APP_CAN_BASE;
    uint32_t flags = mcan_get_interrupt_flags(base);

    if ((flags & MCAN_INT_RXFIFO0_NEW_MSG) != 0) {
        mcan_read_rxfifo(base, 0, (mcan_rx_message_t *)&s_rx_msg);
        has_new_rx_msg = true;
    }

    if ((flags & MCAN_EVENT_TRANSMIT) != 0) {
        has_tx_complete = true;
    }

    mcan_clear_interrupt_flags(base, flags);
}

static void print_rx_message(const mcan_rx_message_t *rx_msg)
{
    uint32_t msg_len = mcan_get_message_size_from_dlc(rx_msg->dlc);

    printf("--- CAN RX ---\n");
    if (rx_msg->use_ext_id) {
        printf("  Ext ID:    0x%08X\n", rx_msg->ext_id);
    } else {
        printf("  Std ID:    0x%03X\n", rx_msg->std_id);
    }
    printf("  DLC:       %lu\n", rx_msg->dlc);
    printf("  Data Len:  %lu\n", msg_len);
    printf("  Data:      ");
    for (uint32_t i = 0; i < msg_len; i++) {
        printf("%02X ", rx_msg->data_8[i]);
        if ((i + 1) % 32 == 0 && (i + 1) < msg_len) {
            printf("\n             ");
        }
    }
    printf("\n");
}

static void can_config_timing(mcan_config_t *cfg, uint32_t src_clk)
{
    uint16_t prescaler, seg1, seg2, sjw;

    if (src_clk == 40000000UL) {
        prescaler = 1;
    } else if (src_clk == 80000000UL) {
        prescaler = 2;
    } else {
        return;
    }

    /* 1Mbps, sample point 75% */
    seg1 = 29;
    seg2 = 10;
    sjw = 2;

    cfg->use_lowlevel_timing_setting = true;
    cfg->can_timing.prescaler = prescaler;
    cfg->can_timing.num_seg1 = seg1;
    cfg->can_timing.num_seg2 = seg2;
    cfg->can_timing.num_sjw = sjw;
}

int main(void)
{
    hpm_stat_t status;

    board_init();
    board_init_led_pins();
    board_init_can(BOARD_APP_CAN_BASE);

    uint32_t can_src_clk = board_init_can_clock(BOARD_APP_CAN_BASE);
    printf("CAN source clock: %lu Hz\n", can_src_clk);

    mcan_msg_buf_attr_t attr = { (uint32_t)mcan_msg_buf, sizeof(mcan_msg_buf) };
    mcan_set_msg_buf_attr(BOARD_APP_CAN_BASE, &attr);

    printf("CAN Config\n");
    mcan_config_t can_config;
    mcan_get_default_config(BOARD_APP_CAN_BASE, &can_config);

    can_config.mode = mcan_mode_normal;
    can_config.baudrate = CAN_APP_BAUDRATE;

    printf("Init can clock\n");
    can_config_timing(&can_config, can_src_clk);

    /* Accept all non-matching frames into RXFIFO0 */
    can_config.all_filters_config.global_filter_config.accept_non_matching_std_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
    can_config.all_filters_config.global_filter_config.accept_non_matching_ext_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;

    mcan_get_default_ram_config(BOARD_APP_CAN_BASE, &can_config.ram_config, false);

    can_config.interrupt_mask = MCAN_EVENT_RECEIVE | MCAN_EVENT_TRANSMIT;
    can_config.txbuf_trans_interrupt_mask = ~0UL;

    status = mcan_init(BOARD_APP_CAN_BASE, &can_config, can_src_clk);
    if (status != status_success) {
        printf("CAN init failed, error: %d\n", status);
        return -1;
    }

    /* Enable CAN transceiver */
    board_can_transceiver_phy_set(BOARD_APP_CAN_BASE, true);

    mcan_enable_interrupts(BOARD_APP_CAN_BASE, MCAN_EVENT_RECEIVE | MCAN_EVENT_TRANSMIT);
    mcan_enable_txbuf_transmission_interrupt(BOARD_APP_CAN_BASE, ~0UL);
    intc_m_enable_irq_with_priority(BOARD_APP_CAN_IRQn, 1);

    printf("CAN 2.0 Echo started: 1Mbps, Ext ID=0x%08X, 8 bytes, 1Hz\n\n", CAN_APP_EXT_ID);

    mcan_tx_frame_t tx_buf;
    memset(&tx_buf, 0, sizeof(tx_buf));
    tx_buf.use_ext_id = 1;
    tx_buf.ext_id = CAN_APP_EXT_ID;
    tx_buf.dlc = MCAN_MSG_DLC_8_BYTES;
    memset(tx_buf.data_8, 0, mcan_get_message_size_from_dlc(tx_buf.dlc));

    uint32_t tx_count = 0;

    while (1) {
        /* Send CAN 2.0 message */
        status = mcan_transmit_blocking(BOARD_APP_CAN_BASE, &tx_buf);
        if (status == status_success) {
            tx_count++;
            printf("[TX #%lu] Ext ID=0x%08X, CAN 2.0, 8 bytes (all zero)\n",
                   tx_count, CAN_APP_EXT_ID);
        } else {
            printf("[TX #%lu] Send failed, error: %d\n", tx_count + 1, status);
        }

        board_led_toggle();

        /* Check for received messages */
        if (has_new_rx_msg) {
            has_new_rx_msg = false;
            print_rx_message((const mcan_rx_message_t *)&s_rx_msg);
        }

        board_delay_ms(CAN_APP_TX_PERIOD_MS);
    }

    return 0;
}
