/*
 * Copyright (c) 2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    can.h
 * @brief   CAN 外设抽象层 —— 配置、全局状态、初始化与接收打印接口
 *
 * 本模块封装了 MCAN 外设的底层初始化、中断处理和通用打印函数，
 * 上层应用（如 mcan_echo.c）只需包含此头文件即可使用 CAN 功能。
 */

#ifndef _CAN_H_
#define _CAN_H_

#include <stdbool.h>          /* volatile bool 类型 */
#include "board.h"            /* 板级宏定义：CAN 基地址、IRQ 编号等 */
#include "hpm_mcan_drv.h"     /* HPM MCAN 驱动 API、结构体与常量 */

/*===========================================================================
 * 用户可配置的 CAN 参数
 *===========================================================================*/

/** @brief CAN 总线波特率 (bps) */
#define CAN_BAUDRATE     (1000000U)

/** @brief CAN 发送帧使用的扩展 ID，29-bit 扩展帧格式 */
#define CAN_EXT_ID       (0x0001FD7FU)

/*===========================================================================
 * ISR 更新的全局状态变量（extern 声明，定义在 can.c 中）
 *===========================================================================*/

/**
 * @brief 接收标志 —— 当 RXFIFO0 收到新帧时由 ISR 置为 true，
 *        上层轮询并处理后可手动清除。
 */
extern volatile bool can_has_new_rx;

/**
 * @brief 发送完成标志 —— 当硬件 TX buffer 发送完成后由 ISR 置为 true，
 *        可用于非阻塞发送的完成确认。
 */
extern volatile bool can_has_tx_complete;

/**
 * @brief 接收消息缓冲区 —— ISR 将 RXFIFO0 中的帧数据拷贝到此结构体中，
 *        上层直接读取即可。
 */
extern volatile mcan_rx_message_t can_rx_msg;

/*===========================================================================
 * API 函数声明
 *===========================================================================*/

/**
 * @brief CAN 外设完整初始化
 *
 * 依次完成：引脚功能配置 → 时钟源配置 → 消息 RAM 分配 →
 *         波特率/时序参数设置 → 全局滤波配置（接收所有帧到 RXFIFO0）→
 *         中断使能（接收 + 发送）→ 收发器 PHY 上电。
 *
 * @param base  CAN 控制器外设基地址，如 BOARD_APP_CAN_BASE
 */
void can_init(MCAN_Type *base);

#endif
