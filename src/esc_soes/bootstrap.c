/*
 * Copyright (c) 2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "bootstrap.h"
#include "esc_foe.h"
#include "board.h"
#include "hpm_romapi.h"
#include "hpm_interrupt.h"

#include <stdio.h>
#include <string.h>

#define BOOT_IDLE        0
#define BOOT_START       1
#define BOOT_UPDATING    2
#define BOOT_SWAP        3
#define BOOT_RESET       4
#define BOOT_FAILED      99

#define FOE_FLASH_STAGING_ADDR  0x84000UL
#define FOE_FLASH_MAX_SIZE      (256U * 1024U)
#define FOE_FLASH_SECTOR_SIZE   4096U

#define FOE_APP_BASE_ADDR       0x00000000UL
#define FOE_IMAGE_OFFSET        0x400UL     /* .bin starts at nor_cfg_option (FLASH + 0x400) */
#define FOE_METADATA_ADDR       0x83000UL

#define FOE_METADATA_MAGIC      0x464F4501UL

/* Safety timeout in main-loop iterations (~30s at 1kHz cycle). */
#define BOOT_TIMEOUT_MAX        30000U

typedef struct
{
    uint32_t magic;
    uint32_t size;
    uint32_t crc32;
    uint32_t reserved;
} foe_metadata_t;

static volatile uint8_t boot_state;
static volatile uint8_t boot_failed;
static uint32_t foe_total_size;
static uint32_t boot_timeout;           /* safety timeout counter */

static xpi_nor_config_t s_nor_config;
static uint8_t sector_buffer[FOE_FLASH_SECTOR_SIZE];
static uint32_t sector_dirty;
static uint32_t sector_addr;

static int flash_init(void)
{
    xpi_nor_config_option_t option;
    option.header.U = BOARD_APP_XPI_NOR_CFG_OPT_HDR;
    option.option0.U = BOARD_APP_XPI_NOR_CFG_OPT_OPT0;
    option.option1.U = BOARD_APP_XPI_NOR_CFG_OPT_OPT1;

    return rom_xpi_nor_auto_config(BOARD_APP_XPI_NOR_XPI_BASE, &s_nor_config, &option);
}

static int flash_erase_range(uint32_t addr, uint32_t size)
{
    return rom_xpi_nor_erase(BOARD_APP_XPI_NOR_XPI_BASE,
                             xpi_xfer_channel_auto,
                             &s_nor_config,
                             addr, size);
}

static int flash_program(uint32_t addr, const uint32_t *data, uint32_t size)
{
    return rom_xpi_nor_program(BOARD_APP_XPI_NOR_XPI_BASE,
                               xpi_xfer_channel_auto,
                               &s_nor_config,
                               data,
                               addr, size);
}

static int flush_sector(void)
{
    int status;

    if (!sector_dirty) {
        return 0;
    }

    status = flash_erase_range(sector_addr, FOE_FLASH_SECTOR_SIZE);
    if (status != 0) {
        printf("Flash erase failed at 0x%08lx\n", (uint32_t)sector_addr);
        return -1;
    }

    status = flash_program(sector_addr, (uint32_t *)sector_buffer, FOE_FLASH_SECTOR_SIZE);
    if (status != 0) {
        printf("Flash program failed at 0x%08lx\n", (uint32_t)sector_addr);
        return -1;
    }

    sector_dirty = 0;
    return 0;
}

static uint32_t flash_foe_buffer(foe_file_cfg_t *self, uint8_t *data, size_t length)
{
    (void)self;

    if (length == 0) {
        return 0;
    }

    uint32_t offset = self->address_offset;

    /* Reject writes that would overflow the staging area. */
    if (offset + length > FOE_FLASH_MAX_SIZE) {
        printf("FOE: write would overflow staging area (%lu + %u > %lu)\n",
               (unsigned long)offset, (unsigned)length, (unsigned long)FOE_FLASH_MAX_SIZE);
        return 1;
    }

    /* Reset safety timeout — data is flowing. */
    boot_timeout = 0;

    uint32_t dest = FOE_FLASH_STAGING_ADDR + offset;
    uint32_t written = 0;

    while (written < length) {
        uint32_t sec_addr = dest & ~(FOE_FLASH_SECTOR_SIZE - 1U);
        uint32_t sec_off = dest & (FOE_FLASH_SECTOR_SIZE - 1U);
        uint32_t chunk = FOE_FLASH_SECTOR_SIZE - sec_off;

        if (chunk > (length - written)) {
            chunk = length - written;
        }

        if (sec_addr != sector_addr) {
            if (flush_sector() != 0) {
                return 1;
            }

            /* Load existing sector content if not writing a full sector. */
            sector_addr = sec_addr;
            if (sec_off != 0 || chunk != FOE_FLASH_SECTOR_SIZE) {
                memcpy(sector_buffer, (void *)sector_addr, FOE_FLASH_SECTOR_SIZE);
            } else {
                memset(sector_buffer, 0xFF, FOE_FLASH_SECTOR_SIZE);
            }
        }

        memcpy(&sector_buffer[sec_off], data + written, chunk);
        sector_dirty = 1;

        dest += chunk;
        written += chunk;
    }

    foe_total_size = offset + length;
    return 0;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static uint32_t calculate_staged_crc32(uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t offset = 0;

    while (offset < size) {
        uint32_t chunk = size - offset;
        if (chunk > FOE_FLASH_SECTOR_SIZE) {
            chunk = FOE_FLASH_SECTOR_SIZE;
        }
        memcpy(sector_buffer, (void *)(FOE_FLASH_STAGING_ADDR + offset), chunk);
        crc = crc32_update(crc, sector_buffer, chunk);
        offset += chunk;
    }

    return ~crc;
}

static int write_metadata(uint32_t size, uint32_t crc)
{
    foe_metadata_t metadata;
    int status;

    metadata.magic = FOE_METADATA_MAGIC;
    metadata.size = size;
    metadata.crc32 = crc;
    metadata.reserved = 0;

    status = flash_erase_range(FOE_METADATA_ADDR, FOE_FLASH_SECTOR_SIZE);
    if (status != 0) {
        return -1;
    }

    status = flash_program(FOE_METADATA_ADDR, (uint32_t *)&metadata, sizeof(metadata));
    if (status != 0) {
        return -1;
    }

    return 0;
}

/** Copy staged firmware to application area and reset.
 *
 *  This function runs from ILM RAM so that it can safely erase and
 *  reprogram the application flash region (including the code that
 *  called it).
 */
ATTR_RAMFUNC static void copy_firmware_and_reset(void)
{
    uint32_t size;
    uint32_t erase_size;
    uint32_t expected_crc;
    uint32_t actual_crc;
    foe_metadata_t metadata;
    int status;

    /* Disable interrupts; we are about to overwrite the running application. */
    disable_global_irq(CSR_MSTATUS_MIE_MASK);

    /* Read and verify metadata. */
    memcpy(&metadata, (void *)FOE_METADATA_ADDR, sizeof(metadata));
    if (metadata.magic != FOE_METADATA_MAGIC) {
        goto do_reset;
    }

    size = metadata.size;
    if (size == 0 || size > FOE_FLASH_MAX_SIZE) {
        goto do_reset;
    }

    /* Sanity check the staged image header.
     * 0xFFFFFFFF = erased flash (no valid firmware staged).
     * 0x00000000 is a valid RISC-V instruction (c.unimp/nop depending
     * on ISA extension), so skip that check — the CRC32 guard above
     * already detects corrupted or blank images. */
    uint32_t first_word = *((volatile uint32_t *)FOE_FLASH_STAGING_ADDR);
    if (first_word == 0xFFFFFFFFUL) {
        goto do_reset;
    }

    /* Verify CRC32 of staged firmware. */
    expected_crc = metadata.crc32;
    actual_crc = 0xFFFFFFFFUL;
    {
        uint32_t offset = 0;
        while (offset < size) {
            uint32_t chunk = size - offset;
            if (chunk > FOE_FLASH_SECTOR_SIZE) {
                chunk = FOE_FLASH_SECTOR_SIZE;
            }
            memcpy(sector_buffer, (void *)(FOE_FLASH_STAGING_ADDR + offset), chunk);
            actual_crc = crc32_update(actual_crc, sector_buffer, chunk);
            offset += chunk;
        }
    }
    actual_crc = ~actual_crc;

    if (actual_crc != expected_crc) {
        goto do_reset;
    }

    /* Round up to sector size for erase, including the 0x400 offset gap
     * before the .bin image starts (nor_cfg_option is at flash + 0x400). */
    erase_size = FOE_IMAGE_OFFSET + ((size + FOE_FLASH_SECTOR_SIZE - 1U) & ~(FOE_FLASH_SECTOR_SIZE - 1U));

    /* Erase application area (from base, covering the offset gap). */
    status = rom_xpi_nor_erase(BOARD_APP_XPI_NOR_XPI_BASE,
                               xpi_xfer_channel_auto,
                               &s_nor_config,
                               FOE_APP_BASE_ADDR, erase_size);
    if (status != 0) {
        goto do_reset;
    }

    /* Program application area from staging area.
     * rom_xpi_nor_program() requires 4-byte alignment on size.
     * Destination is FOE_APP_BASE_ADDR + FOE_IMAGE_OFFSET because the .bin
     * image starts at nor_cfg_option (flash + 0x400), not at flash base. */
    {
        uint32_t prog_size = (size + 3U) & ~3U;
        status = rom_xpi_nor_program(BOARD_APP_XPI_NOR_XPI_BASE,
                                     xpi_xfer_channel_auto,
                                     &s_nor_config,
                                     (uint32_t *)FOE_FLASH_STAGING_ADDR,
                                     FOE_APP_BASE_ADDR + FOE_IMAGE_OFFSET, prog_size);
    }
    if (status != 0) {
        goto do_reset;
    }

do_reset:
    /* Trigger software reset. This inline write does not return. */
    HPM_PPOR->SOFTWARE_RESET = PPOR_SOFTWARE_RESET_COUNTER_SET(1000);
    while (1) {
    }
}

void bootstrap_foe_init(void)
{
    static foe_file_cfg_t files[] = {
        {
            .name = "app",
            .max_data = FOE_FLASH_MAX_SIZE,
            .dest_start_address = FOE_FLASH_STAGING_ADDR,
            .write_function = flash_foe_buffer,
            .write_only_in_boot = 1
        }
    };
    static uint8_t fbuf[FOE_FLASH_SECTOR_SIZE];
    static foe_cfg_t config = {
        .buffer_size = FOE_FLASH_SECTOR_SIZE,
        .fbuffer = fbuf,
        .n_files = 1,
        .files = files
    };

    boot_state = BOOT_IDLE;
    boot_failed = 0;
    foe_total_size = 0;
    sector_dirty = 0;
    sector_addr = 0;

    if (flash_init() != 0) {
        printf("Flash init failed\n");
        boot_failed = 1;
        return;
    }

    FOE_config(&config);
}

void init_boothook(void)
{
    boot_state = BOOT_START;
    foe_total_size = 0;
    printf("Entering BOOT state\n");
}

void boot_inithook(void)
{
    if (boot_failed) {
        boot_state = BOOT_FAILED;
    } else {
        boot_state = BOOT_SWAP;
    }
    printf("Leaving BOOT state\n");
}

void bootstrap_state(void)
{
    switch (boot_state) {
    case BOOT_IDLE:
        break;

    case BOOT_START:
        boot_state = BOOT_UPDATING;
        sector_dirty = 0;
        sector_addr = 0;
        boot_timeout = 0;
        /* Erase staging area so partial-sector writes don't merge with stale data */
        flash_erase_range(FOE_FLASH_STAGING_ADDR, FOE_FLASH_MAX_SIZE);
        printf("BOOT: start updating\n");
        break;

    case BOOT_UPDATING:
        /* Wait for BOOT->INIT transition.
         * If the master disconnects without requesting the transition,
         * self-recover after the safety timeout to avoid getting stuck. */
        board_delay_ms(1);
        boot_timeout++;
        if (boot_timeout > BOOT_TIMEOUT_MAX) {
            printf("BOOT: timeout waiting for state transition, aborting\n");
            boot_state = BOOT_FAILED;
        }
        break;

    case BOOT_SWAP:
        if (flush_sector() != 0) {
            boot_state = BOOT_FAILED;
            break;
        }
        printf("BOOT: flash complete, total %lu bytes\n", (unsigned long)foe_total_size);

        {
            uint32_t crc = calculate_staged_crc32(foe_total_size);
            printf("BOOT: staged CRC32 = 0x%08lx\n", (unsigned long)crc);
            if (write_metadata(foe_total_size, crc) != 0) {
                printf("BOOT: metadata write failed\n");
                boot_state = BOOT_FAILED;
                break;
            }
        }

        boot_state = BOOT_RESET;
        break;

    case BOOT_RESET:
        printf("BOOT: copying firmware to application area and reset\n");
        copy_firmware_and_reset();
        break;

    case BOOT_FAILED:
        printf("BOOT: failed\n");
        boot_state = BOOT_IDLE;
        break;

    default:
        break;
    }
}
