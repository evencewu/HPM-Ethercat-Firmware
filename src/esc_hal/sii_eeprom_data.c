/*
 * SII EEPROM Data — builder that constructs a 2048-byte SII image from
 * named constants defined in sii_eeprom_data.h.
 *
 * Licensed under the GNU General Public License version 2 with exceptions.
 */

#include "sii_eeprom_data.h"
#include <string.h>

/* ------------------------------------------------------------------ *
 * Helpers — write 16-bit and 32-bit values in little-endian order
 * ------------------------------------------------------------------ */

static void put_word(uint8_t *buf, size_t offset, uint16_t val)
{
    buf[offset]     = (uint8_t)(val & 0xFF);
    buf[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
}

static void put_dword(uint8_t *buf, size_t offset, uint32_t val)
{
    put_word(buf, offset,     (uint16_t)(val & 0xFFFF));
    put_word(buf, offset + 2, (uint16_t)((val >> 16) & 0xFFFF));
}

/* ------------------------------------------------------------------ *
 * CRC-8  (polynomial 0x07, initial value 0xFF, no final XOR)
 * ------------------------------------------------------------------ */

uint8_t sii_crc8(const uint8_t *data, size_t len)
{
    uint8_t rem = 0xFF;
    for (size_t i = 0; i < len; i++) {
        rem ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (rem & 0x80) {
                rem = (uint8_t)(((uint16_t)rem << 1) ^ 0x07);
            } else {
                rem = (uint8_t)(rem << 1);
            }
        }
    }
    return rem;
}

/* ------------------------------------------------------------------ *
 * sii_eeprom_build — build the full 2048-byte SII image
 * ------------------------------------------------------------------ */

void sii_eeprom_build(uint8_t *buf, uint16_t buf_size)
{
    if (buf_size < EEP_EMU_SIZE) {
        return;
    }

    /* Clear everything first */
    memset(buf, 0, EEP_EMU_SIZE);

    size_t off = 0;

    /* ==============================================================
     * Configuration Area  (Words 0x00 – 0x07, Bytes 0 – 15)
     * ============================================================== */
    put_word(buf, off, SII_PDI_CONTROL);          off += 2;  /* Word 0 */
    put_word(buf, off, SII_PDI_CONFIGURATION);    off += 2;  /* Word 1 */
    put_word(buf, off, SII_SYNC_IMPULSE_LEN);     off += 2;  /* Word 2 */
    put_word(buf, off, SII_PDI_CONFIGURATION2);   off += 2;  /* Word 3 */
    put_word(buf, off, SII_STATION_ALIAS);        off += 2;  /* Word 4 */
    put_word(buf, off, SII_RESERVED_5);           off += 2;  /* Word 5 */
    put_word(buf, off, SII_RESERVED_6);           off += 2;  /* Word 6 */

    /* Word 7 — CRC-8 over words 0..6 (bytes 0..13).
     * NOTE: esc_eeprom_handle_reload() compares against cfg[15] (high byte),
     * so place the CRC in BOTH bytes of the checksum word. */
    {
        uint8_t crc = sii_crc8(buf, 14);
        put_word(buf, off, (uint16_t)crc | ((uint16_t)crc << 8));
        off += 2;
    }

    /* ==============================================================
     * Identity Area  (Words 0x08 – 0x0F, Bytes 16 – 31)
     * ============================================================== */
    put_dword(buf, off, SII_VENDOR_ID);           off += 4;  /* Words  8- 9 */
    put_dword(buf, off, SII_PRODUCT_CODE);        off += 4;  /* Words 10-11 */
    put_dword(buf, off, SII_REVISION);            off += 4;  /* Words 12-13 */
    put_dword(buf, off, SII_SERIAL_NUMBER);       off += 4;  /* Words 14-15 */

    /* ==============================================================
     * Mailbox Configuration  (Words 0x14+)
     *
     * ETG.1000 Table 26 layout — each mailbox entry is a 32-bit
     * packed pair: { offset[15:0], length[15:0] }.
     * SOEM reads them as uint32 and splits with LO_WORD/HI_WORD
     * (ec_config.c:246-259). Words 0x10-0x13 are reserved/zero.
     * ============================================================== */

    off = 40;  /* Word 0x14 = byte 40 */

    /* BOOT RX mailbox (words 0x14-0x15) */
    put_word(buf, off, SII_BOOT_RX_MBX_OFFSET);   off += 2;
    put_word(buf, off, SII_BOOT_RX_MBX_LENGTH);   off += 2;

    /* BOOT TX mailbox (words 0x16-0x17) */
    put_word(buf, off, SII_BOOT_TX_MBX_OFFSET);   off += 2;
    put_word(buf, off, SII_BOOT_TX_MBX_LENGTH);   off += 2;

    /* Standard RX mailbox (words 0x18-0x19) */
    put_word(buf, off, SII_STD_RX_MBX_OFFSET);    off += 2;
    put_word(buf, off, SII_STD_RX_MBX_LENGTH);    off += 2;

    /* Standard TX mailbox (words 0x1A-0x1B) */
    put_word(buf, off, SII_STD_TX_MBX_OFFSET);    off += 2;
    put_word(buf, off, SII_STD_TX_MBX_LENGTH);    off += 2;

    /* Supported Mailbox Protocols (word 0x1C) */
    put_word(buf, off, SII_MBX_PROTOCOLS);        off += 2;

    /* ==============================================================
     * String Category  (Word 0x40 = Byte 128)
     *
     * Layout (ETG.1000 §6.4.2):
     *   Word 0:  category type        = 0x000A (ECT_SII_STRING)
     *   Word 1:  section length       (words, excluding type+length itself)
     *   Word 2:  [ count | strlen ]   (1 byte each)
     *   Words 3+:  name bytes, padded to even length
     *   Last word: 0xFFFF             (end-of-section marker)
     * ============================================================== */
    {
        const char *name = SII_DEVICE_NAME;
        size_t name_len = strlen(name);

        off = 128;

        /* Category type */
        put_word(buf, off, 0x000A);   off += 2;

        /* Section length (in words, excluding type and length words) */
        uint16_t name_padded = (uint16_t)(name_len + (name_len & 1U));
        uint16_t data_words  = name_padded / 2U;
        uint16_t section_len = 1U + data_words;   /* 1 header word + data */
        put_word(buf, off, section_len);  off += 2;

        /* String count and string length */
        buf[off++] = 0x01;                    /* number of strings */
        buf[off++] = (uint8_t)name_len;       /* length of string 0 */

        /* String data */
        memcpy(buf + off, name, name_len);
        off += name_len;

        /* Pad to even byte boundary */
        if (name_len & 1U) {
            buf[off++] = 0x00;
        }

        /* End marker */
        put_word(buf, off, 0xFFFF);
    }
}
