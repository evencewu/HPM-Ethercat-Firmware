/**
 * @file    RSMotorBase.c
 * @brief   RS 系列电机 CAN 通信帧构造
 *
 * CAN 扩展帧 ID (29-bit) 字段划分：
 *   ┌──────────┬───────────────────┬────────────┐
 *   │ bit28~24 │     bit23~8       │   bit7~0   │
 *   │  (5bit)  │     (16bit)       │   (8bit)   │
 *   │   type   │       data        │   target   │
 *   │ 通信类型  │      数据域       │  目标地址  │
 *   └──────────┴───────────────────┴────────────┘
 *
 * type 字段取值见 RSMotorType_t 枚举（RSMotorBase.h）
 */

#include <stdio.h>
#include <string.h>
#include "motor/RSMotorBase.h"

/*===========================================================================
 * 帧构造函数
 *===========================================================================*/

/**
 * @brief 构造"获取设备 ID"CAN 帧
 *
 * type   = RSMOTOR_TYPE_GET_MOTOR_ID (0x00)
 * data   = 0x00FD (固定，设备 ID 查询数据区)
 * target = 参数指定目标电机地址
 */
mcan_tx_frame_t GetMsgRSMotorGetDeviceID(void)
{
    mcan_tx_frame_t tx_buf;
    memset(&tx_buf, 0, sizeof(tx_buf));

    tx_buf.ext_id = RSMOTOR_PACK_EXT_ID(RSMOTOR_TYPE_GET_MOTOR_ID, 0x00FD, RSMOTOR_TARGET_GET_MOTOR_ID);
    tx_buf.use_ext_id = 1;
    tx_buf.dlc = MCAN_MSG_DLC_8_BYTES;
    memset(tx_buf.data_8, 0, mcan_get_message_size_from_dlc(tx_buf.dlc));

    return tx_buf;
}

/**
 * @brief 解析"获取设备 ID"应答帧
 *
 * 应答帧格式：
 *   CAN ID: type(bit28~24)=0 | motor_can_id(bit23~8) | target(bit7~0)=0xFE
 *   数据区: Byte0~Byte7 = 64-bit MCU 唯一标识符（小端序，Byte0 为 LSB）
 *
 * @param[in]  rx_msg       接收到的 CAN 帧
 * @param[out] motor_can_id 解析出的电机 CAN ID
 * @param[out] uuid         解析出的 64 位 MCU 唯一标识符
 * @return    true = 校验通过并成功提取，false = 校验失败
 */
bool ParseMsgRSMotorGetDeviceID(const mcan_rx_message_t *rx_msg,
                                uint16_t *motor_can_id,
                                uint64_t *uuid)
{
    /* 参数有效性检查 */
    if (rx_msg == NULL || motor_can_id == NULL || uuid == NULL) {
        return false;
    }

    uint32_t ext_id = rx_msg->ext_id;

    /* 校验 bit28~24 (type) 必须为 0 —— 设备 ID 应答类型 */
    uint8_t type = RSMOTOR_EXTRACT_TYPE(ext_id);
    if (type != 0x00U) {
        return false;
    }

    /* 校验 bit7~0 (target) 必须为 0xFE —— 设备 ID 应答目标地址 */
    uint8_t target = RSMOTOR_EXTRACT_TARGET(ext_id);
    if (target != 0xFEU) {
        return false;
    }

    /* 提取 bit23~8 —— 电机 CAN ID */
    *motor_can_id = (uint16_t)RSMOTOR_EXTRACT_DATA(ext_id);

    /* 提取 Byte0~Byte7 —— 64 位 MCU 唯一标识符（小端序） */
    *uuid = 0ULL;
    for (uint8_t i = 0; i < 8U; i++) {
        *uuid |= ((uint64_t)rx_msg->data_8[i]) << (i * 8U);
    }

    return true;
}

/**
 * @brief 构造"使能电机"CAN 帧
 *
 * type   = RSMOTOR_TYPE_ENABLE (0x03)
 * data   = 0x0000 (保留)
 * target = 参数指定目标电机地址
 */
mcan_tx_frame_t GetMsgRSMotorEnable(uint8_t motor_id)
{
    mcan_tx_frame_t tx_buf;
    memset(&tx_buf, 0, sizeof(tx_buf));
    tx_buf.ext_id = RSMOTOR_PACK_EXT_ID(RSMOTOR_TYPE_ENABLE, 0x00FD, motor_id);
    tx_buf.use_ext_id = 1;
    tx_buf.dlc = MCAN_MSG_DLC_8_BYTES;
    memset(tx_buf.data_8, 0, mcan_get_message_size_from_dlc(tx_buf.dlc));

    return tx_buf;
}

/**
 * @brief 构造"禁用电机"CAN 帧
 *
 * type   = RSMOTOR_TYPE_DISABLE (0x04)
 * data   = 0x0000 (保留)
 * target = 参数指定目标电机地址
 */
// mcan_tx_frame_t GetMsgRSMotorDisable(uint8_t target)
// {
//     mcan_tx_frame_t tx_buf;
//     memset(&tx_buf, 0, sizeof(tx_buf));

//     tx_buf.use_ext_id = 1;
//     tx_buf.dlc = MCAN_MSG_DLC_8_BYTES;
//     memset(tx_buf.data_8, 0, mcan_get_message_size_from_dlc(tx_buf.dlc));

//     tx_buf.ext_id = RSMOTOR_PACK_EXT_ID(RSMOTOR_TYPE_DISABLE, 0x0000, target);

//     return tx_buf;
// }
