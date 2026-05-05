#ifndef _RS_MOTOR_H_
#define _RS_MOTOR_H_

#include <stdbool.h>
#include <stdint.h>
#include "board.h"
#include "hpm_mcan_drv.h"

/**
 * @brief 将三字段拼接为 29-bit CAN 扩展 ID
 *
 * CAN 扩展帧 ID 共 29 位 (bit28 ~ bit0)，按 RS Motor 协议拆分为：
 *   bit28~24 (5位)  : 通信类型 (type)
 *   bit23~8  (16位) : 数据域   (data)
 *   bit7~0   (8位)  : 目标地址 (target)
 *
 * @param type    通信类型，取值见 RSMotorType_t 枚举
 * @param data    数据域，  取值范围 0x0000 ~ 0xFFFF (16 bit)
 * @param target  目标地址，取值范围 0x00 ~ 0xFF (8 bit)
 * @return        拼接后的 29-bit CAN 扩展 ID
 */
#define RSMOTOR_PACK_EXT_ID(type, data, target)              \
    ((((uint32_t)(type)   & 0x1FU)   << 24) |                \
     (((uint32_t)(data)   & 0xFFFFU) << 8)  |                \
     (((uint32_t)(target) & 0xFFU)   << 0))

/** @brief 从扩展 ID 中提取 bit28~24（通信类型） */
#define RSMOTOR_EXTRACT_TYPE(ext_id)   (((uint32_t)(ext_id) >> 24) & 0x1FU)

/** @brief 从扩展 ID 中提取 bit23~8（数据域） */
#define RSMOTOR_EXTRACT_DATA(ext_id)   (((uint32_t)(ext_id) >> 8)  & 0xFFFFU)

/** @brief 从扩展 ID 中提取 bit7~0（目标地址） */
#define RSMOTOR_EXTRACT_TARGET(ext_id) (((uint32_t)(ext_id) >> 0)  & 0xFFU)

#define DEFAULT_MASTER_ID 0xFE

/** @brief RS Motor 通信类型枚举 (CAN ID bit28~24) */
typedef enum {
    RSMOTOR_TYPE_GET_MOTOR_ID     = 0x00,  /* 获取电机 ID */
    RSMOTOR_TYPE_CMD              = 0x01,  /* 控制指令 */
    RSMOTOR_TYPE_STATE            = 0x02,  /* 查询状态 */
    RSMOTOR_TYPE_ENABLE           = 0x03,  /* 使能电机 */
    RSMOTOR_TYPE_DISABLE          = 0x04,  /* 禁用电机 */
    RSMOTOR_TYPE_GET_VERSION      = 0x05,  /* 获取版本号 */
    RSMOTOR_TYPE_SET_MOTOR_ZERO   = 0x06,  /* 设置零点 */
    RSMOTOR_TYPE_SET_MOTOR_CANID  = 0x07,  /* 设置电机 CAN ID */
    RSMOTOR_TYPE_GET_MOTOR_PARAM  = 0x11,  /* 读取参数 */
    RSMOTOR_TYPE_WRITE_MOTOR_PARAM = 0x12, /* 写入参数 */
    RSMOTOR_TYPE_ERROR            = 0x15,  /* 错误信息 */
    RSMOTOR_TYPE_SAVE_MOTOR_PARAM = 0x16,  /* 保存参数到 Flash */
} RSMotorType_t;

typedef enum {
    RSMOTOR_TARGET_GET_MOTOR_ID = 0x7F,
    RSMOTOR_TARGET_MASTER_ID = DEFAULT_MASTER_ID,
} RSMotorTarget_t;

/**
 * @brief 构造"获取设备 ID"请求帧
 *
 * @return  填充好的 CAN 发送帧
 */
mcan_tx_frame_t GetMsgRSMotorGetDeviceID(void);

/**
 * @brief 解析"获取设备 ID"应答帧
 *
 * 校验规则：
 *   - 扩展帧 ID bit28~24 (type)   必须等于 0
 *   - 扩展帧 ID bit7~0   (target) 必须等于 DEFAULT_MASTER_ID (0xFE)
 *
 * 提取内容：
 *   - CAN ID bit23~8 → 电机 CAN ID (16 bit)
 *   - 数据区 Byte0~Byte7 → 64 位 MCU 唯一标识符 (小端序)
 *
 * @param[in]  rx_msg       接收到的 CAN 帧
 * @param[out] motor_can_id 解析出的电机 CAN ID
 * @param[out] uuid         解析出的 64 位 MCU 唯一标识符
 * @return    true = 校验通过并成功提取，false = 校验失败
 */
bool ParseMsgRSMotorGetDeviceID(const mcan_rx_message_t *rx_msg,
                                uint16_t *motor_can_id,
                                uint64_t *uuid);

mcan_tx_frame_t GetMsgRSMotorEnable(uint8_t motor_id);

// mcan_tx_frame_t GetMsgRSMotorDisable(uint8_t target);

#endif
