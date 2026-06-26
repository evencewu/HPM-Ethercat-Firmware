/*
 * Copyright (c) 2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "board.h"
#include "hpm_esc_drv.h"
#include "hpm_gpio_drv.h"
#include "esc_eep.h"

/* -------------------------------------------------------------------------- */
/* JL1111 PHY constants — copied from SDK's hpm_ecat_phy.h                   */
/* -------------------------------------------------------------------------- */
#define JL11X1_PAGESEL_REG_OFFSET    (31)
#define JL11X1_WOLEN_REG_OFFSET      (0xF3) /* PHY LED mode register */
#define JL11X1_WOLEN_REG_VALUE       (0)
#define JL11X1_RESET_HOLD_TIME_MS    (1)
#define JL11X1_RESET_WAIT_TIME_MS    (5)
#define JL11X1_RESET_LEVEL           (0)    /* active-low reset */

/* -------------------------------------------------------------------------- */
/* JL1111 PHY helpers — copied from SDK's hpm_ecat_phy.c (pure MDIO, no SSC) */
/* -------------------------------------------------------------------------- */

/** Configure JL1111 LED mode: select page 7, write WOLEN register, page back to 0 */
static hpm_stat_t ecat_jl1111_phy_config_led_mode(ESC_Type *ptr, uint8_t phy_addr)
{
    hpm_stat_t stat;

    stat = esc_mdio_write(ptr, phy_addr, JL11X1_PAGESEL_REG_OFFSET,
                          JL11X1_WOLEN_REG_OFFSET / 32);
    if (stat != status_success) {
        return stat;
    }

    stat = esc_mdio_write(ptr, phy_addr, JL11X1_WOLEN_REG_OFFSET % 32,
                          JL11X1_WOLEN_REG_VALUE);
    if (stat != status_success) {
        return stat;
    }

    stat = esc_mdio_write(ptr, phy_addr, JL11X1_PAGESEL_REG_OFFSET, 0);
    return stat;
}

/** Disable broadcast MDIO response for non-broadcast PHY addresses.
 *  Prevents MDIO bus contention when multiple PHYs share the same bus. */
static hpm_stat_t ecat_jl1111_phy_disable_broadcast_response(ESC_Type *ptr,
                                                              uint8_t phy_addr,
                                                              uint8_t phy_offset)
{
    hpm_stat_t stat;

    /* Broadcast address is 0; only disable for non-zero actual addresses */
    if (phy_addr + phy_offset > 0) {
        stat = esc_mdio_write(ptr, phy_addr, JL11X1_PAGESEL_REG_OFFSET, 128);
        if (stat != status_success) {
            return stat;
        }

        uint16_t value = (uint16_t)(phy_addr + phy_offset) << 5 | 0x1F;
        stat = esc_mdio_write(ptr, phy_addr, 19, value);
        if (stat != status_success) {
            return stat;
        }

        stat = esc_mdio_write(ptr, phy_addr, JL11X1_PAGESEL_REG_OFFSET, 0);
        if (stat != status_success) {
            return stat;
        }
    }

    return status_success;
}

/** Configure all JL1111 PHYs: LED mode + disable broadcast response.
 *  Matches SDK's ecat_phy_config() but without any SSC dependency. */
static hpm_stat_t ecat_phy_config(ESC_Type *ptr)
{
    hpm_stat_t stat;

    /* Port 0 */
    stat = ecat_jl1111_phy_config_led_mode(ptr, BOARD_ECAT_PORT0_PHY_ADDR);
    if (stat != status_success) return stat;
    stat = ecat_jl1111_phy_disable_broadcast_response(ptr, BOARD_ECAT_PORT0_PHY_ADDR,
                                                       BOARD_ECAT_PHY_ADDR_OFFSET);
    if (stat != status_success) return stat;

#if BOARD_ECAT_SUPPORT_PORT1
    stat = ecat_jl1111_phy_config_led_mode(ptr, BOARD_ECAT_PORT1_PHY_ADDR);
    if (stat != status_success) return stat;
    stat = ecat_jl1111_phy_disable_broadcast_response(ptr, BOARD_ECAT_PORT1_PHY_ADDR,
                                                       BOARD_ECAT_PHY_ADDR_OFFSET);
    if (stat != status_success) return stat;
#endif

#if BOARD_ECAT_SUPPORT_PORT2
    stat = ecat_jl1111_phy_config_led_mode(ptr, BOARD_ECAT_PORT2_PHY_ADDR);
    if (stat != status_success) return stat;
    stat = ecat_jl1111_phy_disable_broadcast_response(ptr, BOARD_ECAT_PORT2_PHY_ADDR,
                                                       BOARD_ECAT_PHY_ADDR_OFFSET);
    if (stat != status_success) return stat;
#endif

    return status_success;
}

/* CRC-8 for SII config checksum, poly 0x07, init 0xFF */
static uint8_t sii_crc8(const uint8_t *data, size_t len)
{
    uint8_t rem = 0xFF;
    for (size_t i = 0; i < len; i++) {
        rem ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (rem & 0x80) rem = ((rem << 1) ^ 0x07) & 0xFF;
            else            rem = (rem << 1) & 0xFF;
        }
    }
    return rem;
}

/** Handle initial EEPROM reload request before SOES takes over.
 *
 *  The HPM5E00 ESC uses a custom reload format. After enabling EEPROM
 *  emulation, the ESC asserts BUSY with a RELOAD command.  The PDI must
 *  build a 32-bit reload value from the config area and write it to
 *  EEDATA[31:0].  On ack, the ESC copies bits to internal registers:
 *
 *    EEDATA[15:0]  → Configured Station Alias (0x12-0x13)
 *    EEDATA[16]    → ELDAP (Enable Latch on DLstatus)
 *    EEDATA[17]    → ELP0 (Enable Link Port 0)
 *    EEDATA[18]    → ELP1 (Enable Link Port 1)
 *    EEDATA[19]    → ELP2 (Enable Link Port 2)
 *    EEDATA[20]    → ELP3 (Enable Link Port 3)
 *
 *  After this, the ESC de-asserts BUSY and DLstatus bit 0 becomes 1
 *  (PDI operational), unblocking SOES's ecat_slv_init().
 */
static void esc_eeprom_handle_reload(void)
{
    int timeout = 1000; /* ~1 second at 1ms per iteration */
    uint8_t cmd;

    printf("EEPROM: waiting for BUSY...\n"); fflush(stdout);
    /* Wait for ESC to become busy with EEPROM command */
    while (timeout-- > 0) {
        uint16_t ctrl = HPM_ESC->EEPROM_CTRL_STAT;
        if ((ctrl & ESC_EEPROM_CTRL_STAT_EE_EMU_MASK) &&
            (ctrl & ESC_EEPROM_CTRL_STAT_BUSY_MASK)) {
            break;
        }
        board_delay_ms(1);
    }
    if (timeout <= 0) {
        printf("EEPROM: timeout waiting for BUSY\n");
        return;
    }
    printf("EEPROM: BUSY detected, ctrl=0x%04X\n", HPM_ESC->EEPROM_CTRL_STAT);

    cmd = ESC_EEPROM_CTRL_STAT_CMD_GET(HPM_ESC->EEPROM_CTRL_STAT);
    printf("EEPROM: cmd=0x%02X\n", cmd);
    if (cmd != 0x04) {
        /* Not a reload - ack with error so ESC can proceed */
        esc_eeprom_emulation_ack(HPM_ESC, cmd, true, false);
        return;
    }

    /* Read SII config area (8 words / 16 bytes) */
    uint8_t cfg[16];
    if (EEP_read(0, cfg, sizeof(cfg)) != 0) {
        esc_eeprom_emulation_ack(HPM_ESC, cmd, true, false);
        return;
    }

    /* Validate CRC-8 over words 0-6 */
    uint8_t crc_calc = sii_crc8(cfg, 14);
    if (crc_calc != cfg[15]) {
        printf("EEPROM: CRC fail: calc=0x%02X expected=0x%02X\n", crc_calc, cfg[15]);
        esc_eeprom_emulation_ack(HPM_ESC, cmd, false, true);
        return;
    }

    /* Build HPM-specific reload value (matching SDK's ecat_eeprom_emulation_reload).
     * cfg is byte array. Words: w0@[0], w1@[2], ..., w4(station alias)@[8] */
    uint16_t w0 = cfg[0] | ((uint16_t)cfg[1] << 8);
    uint16_t w4 = cfg[8] | ((uint16_t)cfg[9] << 8); /* station alias */

    uint32_t reload = w4;                          /* bits [15:0] */
    reload |= (uint32_t)((w0 >> 12) & 1) << 16;   /* ELDAP  */
    reload |= (uint32_t)((w0 >> 13) & 1) << 17;   /* ELP0   */
    reload |= (uint32_t)((w0 >> 14) & 1) << 18;   /* ELP1   */
    reload |= (uint32_t)((w0 >> 15) & 1) << 19;   /* ELP2   */
    reload |= 0UL << 20;                           /* ELP3 (unused) */

    printf("EEPROM: reload=0x%08lX\n", (unsigned long)reload);
    HPM_ESC->EEPROM_DATA = reload;
    esc_eeprom_emulation_ack(HPM_ESC, cmd, false, false);

    /* Wait for DLstatus bit 0 to indicate PDI is ready */
    printf("EEPROM: waiting for DLstatus...\n");
    timeout = 1000;
    while (timeout-- > 0) {
        if (HPM_ESC->ESC_DL_STAT & 0x0001) break;
        board_delay_ms(1);
    }
    if (timeout > 0) {
        printf("EEPROM: DLstatus ready\n");
    } else {
        printf("EEPROM: DLstatus timeout, DL_STAT=0x%04X\n", HPM_ESC->ESC_DL_STAT);
    }
}

/**
 * @brief Full ESC + PHY initialization for EtherCAT operation.
 *
 * board_init_ethercat() handles the standard SDK initialization:
 *   - ESC pin/clock setup, EEPROM emulation enable, PHY reset release,
 *     NMII_LINK source & CTRL signal mapping, PHY MDIO offset.
 *
 * This function adds the additional steps needed for SOES FoE operation:
 *   1. Load SII data into EEPROM emulation and handle the ESC reload
 *   2. Configure JL1111 PHYs via MDIO (LED mode, broadcast response)
 */
void esc_full_init(void)
{
    printf("ESC: board_init_ethercat...\n"); fflush(stdout);
    board_init_ethercat(HPM_ESC);

    printf("ESC: EEP_init...\n"); fflush(stdout);
    EEP_init();

    printf("ESC: eeprom_handle_reload...\n"); fflush(stdout);
    esc_eeprom_handle_reload();

    printf("ESC: phy_config...\n"); fflush(stdout);
    ecat_phy_config(HPM_ESC);

    printf("ESC: done\n"); fflush(stdout);
}
