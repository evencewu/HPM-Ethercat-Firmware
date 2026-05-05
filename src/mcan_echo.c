/*
 * Copyright (c) 2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    mcan_echo.c
 * @brief   CAN 2.0 回环/收发示例 —— 1Mbps, 29-bit 扩展帧, 1Hz 收发
 *
 * 应用逻辑：
 *   1. 板级初始化（时钟、LED）
 *   2. CAN 外设初始化（通过 can.h 抽象层）
 *   3. 准备固定的 CAN 2.0 发送帧（扩展 ID: CAN_EXT_ID, 8 字节全零）
 *   4. 进入主循环：
 *      - 检查上一次发送是否完成（非阻塞方式，通过中断标志）
 *      - 将下一帧推入硬件发送队列（非阻塞）
 *      - 检查是否收到新的 CAN 帧并打印
 *      - 切换 LED 状态
 *      - 延时 1 秒
 *
 * 数据流向：
 *   发送路径:  主循环准备帧 → mcan_transmit() → 硬件 TX buffer
 *             → CAN 总线 → ISR 置 can_has_tx_complete
 *   接收路径:  CAN 总线 → RXFIFO0 → ISR 读入 can_rx_msg
 *             → 置 can_has_new_rx → 主循环打印
 */

#include <stdio.h>
#include <string.h>
#include "can/can.h"
#include "motor/RSMotorBase.h"

/** @brief 主循环发送/接收间隔 (ms) */
#define CAN_APP_TX_PERIOD_MS      (1000U)

int main(void)
{
    hpm_stat_t status;

    /*
     * 板级初始化：
     *   board_init()         —— 系统时钟、电源等基础初始化
     *   board_init_led_pins() —— LED 引脚配置为 GPIO 输出
     */
    board_init();
    board_init_led_pins();

    /*
     * CAN 外设初始化（can.h 抽象层）：
     *   内部完成引脚、时钟、波特率 (1Mbps)、中断、收发器的全部配置
     */
    can_init(BOARD_APP_CAN_BASE);

    printf("CAN 2.0 Echo started: 1Mbps, Ext ID=0x%08X, 8 bytes, 1Hz\n\n", CAN_EXT_ID);

    uint32_t tx_count = 0;       /* 已入队的发送帧计数 */
    mcan_tx_frame_t tx_buf;      /* CAN 发送帧缓冲区 */

    /*=========================================================================
     * 主循环 —— 非阻塞发送 + 轮询接收
     *=========================================================================*/
    while (1) {
        /*
         * [非阻塞] 检查上一次发送是否完成
         * can_has_tx_complete 由 ISR 在硬件发送完成后置为 true，
         * 主循环在此轮询并清除标志，打印发送确认信息。
         */
        if (can_has_tx_complete) {
            can_has_tx_complete = false;
            printf("[TX #%lu] Ext ID=0x%08X, 8 bytes\n", tx_count, tx_buf.ext_id);
        }

        /*
         * [非阻塞] 构造帧并写入专用 TX buffer 0，立即触发发送
         * mcan_transmit_via_txbuf_nonblocking() 立即返回，不等待总线发送完成。
         * 若 TX buffer 忙则返回错误码（1Hz 低频发送通常不会冲突）。
         */
        //tx_buf = GetMsgRSMotorEnable();

        tx_buf = GetMsgRSMotorGetDeviceID();
        status = mcan_transmit_via_txbuf_nonblocking(BOARD_APP_CAN_BASE, 0, &tx_buf);
        if (status != status_success) {
            printf("[TX ] Queue failed, error: %d\n", status);
        }

        /* 每轮循环切换一次 LED，用于指示系统运行状态 */
        board_led_toggle();

        /*
         * [轮询接收] 检查是否有新收到的 CAN 帧
         * can_has_new_rx 由 ISR 在 RXFIFO0 收到新帧时置为 true，
         * 数据已存入 can_rx_msg，直接打印即可。
         */
        
        // if (can_has_new_rx) {
        //     can_has_new_rx = false;

        //     uint16_t motor_can_id;
        //     uint64_t mcu_uuid;

        //     if (ParseMsgRSMotorGetDeviceID((const mcan_rx_message_t *)&can_rx_msg,
        //                                    &motor_can_id, &mcu_uuid)) {
        //         printf("Motor CAN ID: 0x%04X, MCU UUID: 0x%016llX\n",
        //                motor_can_id, mcu_uuid);
        //     }
        // }

        /* 延时 1 秒，控制收发频率为 1Hz */
        board_delay_ms(CAN_APP_TX_PERIOD_MS);
    }

    return 0;
}
