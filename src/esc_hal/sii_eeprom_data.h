/*
 * SII EEPROM Data — Slave Information Interface definitions for HPM5E00
 *
 * All configurable fields are exposed as named constants.
 * To change the device identity or mailbox layout, edit the macros below
 * and rebuild — no need to decode a 2048-byte hex dump.
 *
 * Licensed under the GNU General Public License version 2 with exceptions.
 */

#ifndef __sii_eeprom_data__
#define __sii_eeprom_data__

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * SII EEPROM total size (bytes)
 * ========================================================================= */
#define EEP_EMU_SIZE                2048

/* =========================================================================
 * ESC Configuration Area  (Words 0x00 – 0x07, Bytes 0 – 15)
 * ========================================================================= */

/* Word 0x00 — PDI Control
 *   Bit  0- 7: PDI type         (0x80 = async 16-bit µC interface)
 *   Bit 12   : ELDAP            (1 = enable DLstatus latch on PDI)
 *   Bit 13   : ELP0             (1 = enable link detection port 0)
 *   Bit 14   : ELP1             (1 = enable link detection port 1)
 *   Bit 15   : ELP2             (1 = enable link detection port 2)           */
#define SII_PDI_CONTROL             0x2080

/* Word 0x01 — PDI Configuration */
#define SII_PDI_CONFIGURATION       0x0000

/* Word 0x02 — Sync Impulse Length (only relevant for synchronous PDI) */
#define SII_SYNC_IMPULSE_LEN        0x0000

/* Word 0x03 — PDI Configuration 2 */
#define SII_PDI_CONFIGURATION2      0x0000

/* Word 0x04 — Configured Station Alias (0 = use auto-increment address) */
#define SII_STATION_ALIAS           0x0000

/* Word 0x05 — Reserved */
#define SII_RESERVED_5              0x0000

/* Word 0x06 — Reserved */
#define SII_RESERVED_6              0x0000

/* Word 0x07 — CRC-8 (auto-calculated by sii_eeprom_build) */

/* =========================================================================
 * Identity Area  (Words 0x08 – 0x0F, Bytes 16 – 31)
 * ========================================================================= */

/* Vendor ID — assigned by ETG (0x1337 is a placeholder; apply for a real one) */
#define SII_VENDOR_ID               0x00001337UL

/* Product Code — user-defined, identifies the specific device model          */
#define SII_PRODUCT_CODE            0x12783456UL

/* Revision Number — 32-bit: major[31:16] . minor[15:0]                       */
#define SII_REVISION                0x00010000UL

/* Serial Number — 0 = no serial                                               */
#define SII_SERIAL_NUMBER           0x00000000UL

/* Word 0x0F — Execution delay (0 = no delay)                                  */
#define SII_EXECUTION_DELAY         0x0000

/* =========================================================================
 * Mailbox Configuration  (Words 0x10+, Bytes 32+)
 * ========================================================================= */

/* Supported mailbox protocols (bitmask).
 *   bit 0: AoE, bit 1: EoE, bit 2: CoE, bit 3: FoE, bit 4: SoE            */
#define SII_MBX_PROTOCOLS           0x000C  /* CoE + FoE */

/* Standard (non-BOOT) mailbox */
#define SII_STD_RX_MBX_OFFSET       0x1000
#define SII_STD_RX_MBX_LENGTH       0x0080  /* 128 bytes */
#define SII_STD_TX_MBX_OFFSET       0x1080
#define SII_STD_TX_MBX_LENGTH       0x0080  /* 128 bytes */

/* BOOT mailbox — MUST match standard offsets/lengths because HPM5E00
 * SyncManager registers are read-only after ESC initialization.              */
#define SII_BOOT_RX_MBX_OFFSET      0x1000
#define SII_BOOT_RX_MBX_LENGTH      0x0080
#define SII_BOOT_TX_MBX_OFFSET      0x1080
#define SII_BOOT_TX_MBX_LENGTH      0x0080

/* =========================================================================
 * Device Name  (String Category at Word 0x40 / Byte 128)
 * ========================================================================= */
#define SII_DEVICE_NAME             "HPM5E00"

/* =========================================================================
 * API
 * ========================================================================= */

/** Build a complete SII EEPROM image into caller-provided buffer.
 *
 * @param buf      pointer to 2048-byte buffer
 * @param buf_size buffer size (must be >= EEP_EMU_SIZE)
 */
void sii_eeprom_build(uint8_t *buf, uint16_t buf_size);

/** CRC-8 over SII config area, polynomial 0x07, initial value 0xFF.
 *
 * Used both by the builder (to fill the checksum word) and by the
 * ESC reload handler (to validate EEPROM content at startup).
 *
 * @param data  input bytes
 * @param len   number of bytes
 * @return CRC-8 value
 */
uint8_t sii_crc8(const uint8_t *data, size_t len);

#endif /* __sii_eeprom_data__ */
