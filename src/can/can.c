/*
 * Copyright (c) 2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    can.c
 * @brief   CAN 外设抽象层实现 —— ISR、初始化、接收打印
 *
 * 核心职责：
 *   - 定义 MCAN 消息 RAM 缓冲区（放置于 AHB SRAM 段）
 *   - 实现 CAN 中断服务例程（接收 + 发送完成）
 *   - 根据时钟源自动计算时序参数（波特率 1Mbps，采样点 75%）
 *   - 提供统一的 can_init() 完成所有硬件初始化
 */

#include <stdio.h>
#include "can/can.h"

/*===========================================================================
 * 硬件消息缓冲区
 *===========================================================================*/

/**
 * @brief MCAN 消息 RAM 缓冲区
 *
 * 必须放置在 AHB SRAM 区（.ahb_sram 段），因为 MCAN 外设通过 AHB 总线
 * 直接访问此区域进行消息收发，不可放置在通用 SRAM 中。
 */
ATTR_PLACE_AT(".ahb_sram") static uint32_t can_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];

/*===========================================================================
 * ISR 与主循环共享的全局状态变量
 *===========================================================================*/

/** @brief 收到新帧标志，ISR 写入，主循环读取并清除 */
volatile bool can_has_new_rx;

/** @brief 发送完成标志，ISR 写入，主循环读取并清除 */
volatile bool can_has_tx_complete;

/** @brief 接收帧数据缓存，ISR 从 RXFIFO0 读出后填入 */
volatile mcan_rx_message_t can_rx_msg;

/*===========================================================================
 * CAN 中断服务例程 (ISR)
 *===========================================================================*/

/**
 * @brief   CAN 中断服务例程
 *
 * 中断触发条件（由 can_init 配置）：
 *   - MCAN_EVENT_RECEIVE  ：RXFIFO0 收到新帧
 *   - MCAN_EVENT_TRANSMIT ：TX buffer 发送完成
 *
 * 处理流程：
 *   1. 读取当前挂起的中断标志
 *   2. 若为接收中断 → 从 RXFIFO0 读出帧到 can_rx_msg，置 can_has_new_rx
 *   3. 若为发送中断 → 置 can_has_tx_complete
 *   4. 清除已处理的中断标志
 *
 * 注意：ISR 中不做耗时操作（如 printf），仅标记事件，由主循环轮询处理。
 */
SDK_DECLARE_EXT_ISR_M(BOARD_APP_CAN_IRQn, board_can_isr)
void board_can_isr(void)
{
    MCAN_Type *base = BOARD_APP_CAN_BASE;
    uint32_t flags = mcan_get_interrupt_flags(base);

    /* 接收中断：RXFIFO0 中有新消息 */
    if ((flags & MCAN_INT_RXFIFO0_NEW_MSG) != 0) {
        mcan_read_rxfifo(base, 0, (mcan_rx_message_t *)&can_rx_msg);
        can_has_new_rx = true;
    }

    /* 发送中断：TX buffer 中的数据已成功发送到总线上 */
    if ((flags & MCAN_EVENT_TRANSMIT) != 0) {
        can_has_tx_complete = true;
    }

    /* 清除已处理的中断标志，避免重复进入 ISR */
    mcan_clear_interrupt_flags(base, flags);
}

/*===========================================================================
 * 波特率时序计算（内部函数）
 *===========================================================================*/

/**
 * @brief 根据 CAN 时钟源频率计算位时序参数
 *
 * 目标：1Mbps 波特率，采样点 75%
 *
 * 位时间 = (num_seg1 + num_seg2 + 1) 个时间量子 (TQ)
 * TQ     = prescaler / can_src_clk
 *
 * 计算公式：
 *   波特率 = can_src_clk / [prescaler × (seg1 + seg2 + 1)]
 *
 * 当前支持的时钟源：
 *   - 40MHz → prescaler=1, seg1=29, seg2=10 → 1,000,000 bps
 *   - 80MHz → prescaler=2, seg1=29, seg2=10 → 1,000,000 bps
 *
 * 采样点 = (seg1 + 1) / (seg1 + seg2 + 1) = 30/40 = 75%
 *
 * @param cfg      MCAN 配置结构体指针，直接写入时序参数
 * @param src_clk  CAN 外设源时钟频率 (Hz)
 */
static void can_config_timing(mcan_config_t *cfg, uint32_t src_clk)
{
    uint16_t prescaler, seg1, seg2, sjw;

    /* 根据时钟源选择预分频器 */
    if (src_clk == 40000000UL) {
        prescaler = 1;      /* 40MHz / 1 = 40MHz TQ 时钟 */
    } else if (src_clk == 80000000UL) {
        prescaler = 2;      /* 80MHz / 2 = 40MHz TQ 时钟 */
    } else {
        return;             /* 不支持的时钟频率，不修改配置 */
    }

    /*
     * 位时序段设置：
     *   seg1 = 29 TQ (传播段 + 相位缓冲段 1)
     *   seg2 = 10 TQ (相位缓冲段 2)
     *   sjw  =  2 TQ (同步跳转宽度，用于时钟同步补偿)
     *
     * 波特率 = 40MHz / (1 × (29 + 10 + 1)) = 1,000,000 bps
     */
    seg1 = 29;
    seg2 = 10;
    sjw = 2;

    /* 使能底层时序设置并写入参数 */
    cfg->use_lowlevel_timing_setting = true;
    cfg->can_timing.prescaler = prescaler;
    cfg->can_timing.num_seg1 = seg1;
    cfg->can_timing.num_seg2 = seg2;
    cfg->can_timing.num_sjw = sjw;
}

/*===========================================================================
 * CAN 外设初始化
 *===========================================================================*/

/**
 * @brief CAN 外设完整初始化
 *
 * 初始化步骤（按顺序）：
 *   1. 配置 CAN 引脚功能（TX/RX/STB）
 *   2. 配置并获取 CAN 时钟源频率
 *   3. 将消息 RAM 缓冲区注册到 MCAN 外设
 *   4. 获取默认配置并覆盖应用参数（模式、波特率、时序、滤波）
 *   5. 调用 mcan_init() 将配置写入硬件
 *   6. 使能 CAN 收发器 PHY
 *   7. 使能接收和发送中断，配置中断优先级
 *
 * @param base  CAN 控制器外设基地址
 */
void can_init(MCAN_Type *base)
{
    hpm_stat_t status;

    /* 1. 引脚功能初始化：TX、RX 引脚复用为 CAN 功能 */
    board_init_can(base);

    /* 2. CAN 时钟源配置，获取当前时钟频率用于波特率计算 */
    uint32_t can_src_clk = board_init_can_clock(base);
    printf("CAN source clock: %lu Hz\n", can_src_clk);

    /* 3. 注册消息 RAM 缓冲区 —— MCAN 外设通过 DMA 直接访问此区域 */
    mcan_msg_buf_attr_t attr = { (uint32_t)can_msg_buf, sizeof(can_msg_buf) };
    mcan_set_msg_buf_attr(base, &attr);

    /* 4. 获取默认配置并逐项覆盖 */
    printf("CAN Config\n");
    mcan_config_t can_config;
    mcan_get_default_config(base, &can_config);

    /* 4a. 工作模式：正常模式（非回环、非监听） */
    can_config.mode = mcan_mode_normal;
    can_config.baudrate = CAN_BAUDRATE;

    /* 4b. 波特率时序参数 */
    printf("Init can clock\n");
    can_config_timing(&can_config, can_src_clk);

    /*
     * 4c. 全局滤波配置：
     *     将所有不匹配任何标准/扩展帧滤波器的帧
     *     存入 RXFIFO0（即默认接收所有 CAN 帧）。
     */
    can_config.all_filters_config.global_filter_config.accept_non_matching_std_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
    can_config.all_filters_config.global_filter_config.accept_non_matching_ext_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;

    /* 4d. 使用默认 RAM 配置（RXFIFO0/TX buffer 大小等） */
    mcan_get_default_ram_config(base, &can_config.ram_config, false);

    /* 4e. 中断使能掩码：接收 + 发送 */
    can_config.interrupt_mask = MCAN_EVENT_RECEIVE | MCAN_EVENT_TRANSMIT;
    can_config.txbuf_trans_interrupt_mask = ~0UL;

    /* 5. 将配置写入 MCAN 硬件寄存器 */
    status = mcan_init(base, &can_config, can_src_clk);
    if (status != status_success) {
        printf("CAN init failed, error: %d\n", status);
        return;
    }

    /* 6. 使能 CAN 收发器 PHY（STB 引脚拉低，进入正常通信模式） */
    board_can_transceiver_phy_set(base, true);

    /* 7. 在 NVIC 级别使能 CAN 中断线并设置优先级 */
    mcan_enable_interrupts(base, MCAN_EVENT_RECEIVE | MCAN_EVENT_TRANSMIT);
    mcan_enable_txbuf_transmission_interrupt(base, ~0UL);
    intc_m_enable_irq_with_priority(BOARD_APP_CAN_IRQn, 1);
}
