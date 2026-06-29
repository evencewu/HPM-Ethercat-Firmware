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

#include "middleware/logger.h"
#include <string.h>

/* Minimal RAM-safe UART TX using direct register writes.
 * ATTR_RAMFUNC places these in ILM so they survive flash erase.
 * We CANNOT call SDK UART functions (uart_send_byte, etc.) here
 * because they live in flash and get erased before we program. */

/* Place a string literal in the .fast section (ILM) so it survives
 * flash erase.  The linker script's *(.fast.*) rule catches these. */
#define RAM_STR(msg) ({ \
    static const char _s[] __attribute__((section(".fast.rodata"), used)) = msg; \
    _s; \
})

/* RAM-safe memcpy / memset — we CANNOT use the standard library versions
 * after erasing flash because they reside in the erased region. */

static void ATTR_RAMFUNC ram_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
}

static void ATTR_RAMFUNC ram_memset(void *dst, uint8_t val, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--) {
        *d++ = val;
    }
}

static void ATTR_RAMFUNC ram_putc(char c)
{
    /* HPM_UART0 base = 0xF0040000 (console UART), LSR at +0x34, THR at +0x20.
     * Hardcoded to eliminate any struct/pointer dependency.
     * MUST use UART0 — that's where the debug serial is connected. */
    volatile uint32_t *uart_lsr = (volatile uint32_t *)0xF0040034UL;
    volatile uint32_t *uart_thr = (volatile uint32_t *)0xF0040020UL;
    while ((*uart_lsr & 0x20U) == 0) {
    }
    *uart_thr = (uint8_t)c;
}

/* Minimal puts using stack-based string (no flash .rodata access!) */
static void ATTR_RAMFUNC ram_puts(const char *s)
{
    while (*s) {
        ram_putc(*s++);
    }
}

/* Put hex without static const array (would be in flash .rodata) */
static void ATTR_RAMFUNC ram_puthex(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nib = (uint8_t)((v >> i) & 0xFU);
        ram_putc((char)(nib < 10U ? '0' + nib : 'A' + nib - 10U));
    }
}

#define BOOT_IDLE        0
#define BOOT_START       1
#define BOOT_UPDATING    2
#define BOOT_SWAP        3
#define BOOT_RESET       4
#define BOOT_FAILED      99

/* NOR flash device offsets (for ROM API erase/program destination).
 * These are offsets within the NOR flash chip, NOT CPU addresses. */
#define FOE_FLASH_STAGING_ADDR  0x84000UL
#define FOE_FLASH_MAX_SIZE      (256U * 1024U)
#define FOE_FLASH_SECTOR_SIZE   4096U

#define FOE_APP_BASE_ADDR       0x00000000UL
#define FOE_IMAGE_OFFSET        0x400UL     /* .bin starts at nor_cfg_option (FLASH + 0x400) */
#define FOE_METADATA_ADDR       0x83000UL

/* XIP-mapped CPU addresses for direct flash reads.
 * BOARD_FLASH_BASE_ADDRESS is defined in board.h (0x80000000UL for HPM5E00). */
#define FLASH_XIP_STAGING       (BOARD_FLASH_BASE_ADDRESS + FOE_FLASH_STAGING_ADDR)
#define FLASH_XIP_METADATA      (BOARD_FLASH_BASE_ADDRESS + FOE_METADATA_ADDR)

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
        LOG_ERROR("FLASH", "erase failed at 0x%08lx", (uint32_t)sector_addr);
        return -1;
    }

    status = flash_program(sector_addr, (uint32_t *)sector_buffer, FOE_FLASH_SECTOR_SIZE);
    if (status != 0) {
        LOG_ERROR("FLASH", "program failed at 0x%08lx", (uint32_t)sector_addr);
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
        LOG_WARN("FOE", "write would overflow staging area (%lu + %u > %lu)",
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
                /* sector_addr is a NOR offset; add XIP base for CPU read */
                memcpy(sector_buffer, (void *)(BOARD_FLASH_BASE_ADDRESS + sector_addr),
                       FOE_FLASH_SECTOR_SIZE);
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
        memcpy(sector_buffer, (void *)(FLASH_XIP_STAGING + offset), chunk);
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

    /* DEBUG: prove we entered this function */
    ram_putc('A');
    for (volatile uint32_t _d = 0; _d < 100000U; _d++) {
    }

    /* Disable interrupts; we are about to overwrite the running application. */
    disable_global_irq(CSR_MSTATUS_MIE_MASK);

    /* Read and verify metadata (pre-erase — safe to use memcpy). */
    memcpy(&metadata, (void *)FLASH_XIP_METADATA, sizeof(metadata));
    ram_puts(RAM_STR("BOOT: meta sz=")); ram_puthex(metadata.size);
    ram_puts(RAM_STR(" crc=")); ram_puthex(metadata.crc32); ram_putc('\n');
    if (metadata.magic != FOE_METADATA_MAGIC) {
        ram_puts(RAM_STR("BOOT: bad meta magic\n"));
        goto do_reset;
    }

    size = metadata.size;
    if (size == 0 || size > FOE_FLASH_MAX_SIZE) {
        goto do_reset;
    }

    /* Sanity check the staged image header.
     * 0xFFFFFFFF = erased flash (no valid firmware staged). */
    uint32_t first_word = *((volatile uint32_t *)FLASH_XIP_STAGING);
    if (first_word == 0xFFFFFFFFUL) {
        goto do_reset;
    }

    /* Verify CRC32 of staged firmware (pre-erase — uses standard memcpy). */
    expected_crc = metadata.crc32;
    actual_crc = 0xFFFFFFFFUL;
    {
        uint32_t offset = 0;
        while (offset < size) {
            uint32_t chunk = size - offset;
            if (chunk > FOE_FLASH_SECTOR_SIZE) {
                chunk = FOE_FLASH_SECTOR_SIZE;
            }
            memcpy(sector_buffer, (void *)(FLASH_XIP_STAGING + offset), chunk);
            actual_crc = crc32_update(actual_crc, sector_buffer, chunk);
            offset += chunk;
        }
    }
    actual_crc = ~actual_crc;

    if (actual_crc != expected_crc) {
        ram_puts(RAM_STR("BOOT: CRC mismatch\n"));
        goto do_reset;
    }

    /* Bulk-erase the entire application area from sector-aligned NOR 0x000.
     *
     * Per-sector erase was FATALLY BROKEN: because the .bin starts at
     * NOR 0x400 (not sector-aligned to 4KB), each sector's erase would
     * overwrite the last 0x400 bytes of the PREVIOUS sector's program.
     *
     * Bulk erase avoids this overlap.  The .bin is then programmed sector
     * by sector without any further erasing.
     *
     * We CANNOT use standard memcpy after the erase (it lives in flash),
     * so ram_memcpy / ram_memset are used throughout the programming loop. */
    erase_size = (size + FOE_IMAGE_OFFSET + FOE_FLASH_SECTOR_SIZE - 1U)
               & ~(FOE_FLASH_SECTOR_SIZE - 1U);

    ram_puts(RAM_STR("BOOT: erasing app area\n"));
    status = rom_xpi_nor_erase(BOARD_APP_XPI_NOR_XPI_BASE,
                               xpi_xfer_channel_auto,
                               &s_nor_config,
                               FOE_APP_BASE_ADDR, erase_size);
    if (status != 0) {
        ram_puts(RAM_STR("BOOT: erase failed\n"));
        goto do_reset;
    }
    ram_puts(RAM_STR("BOOT: erase OK\n"));

    /* Program page-by-page (256 bytes = NOR page size).
     *
     * After bulk erase, rom_xpi_nor_program with 4096-byte chunks was
     * unreliable.  256-byte pages match the NOR flash hardware page size
     * and are guaranteed safe. */
    {
        #define FOE_PAGE_SIZE  256U
        uint32_t offset = 0;
        uint32_t page_count = 0;
        ram_putc('P');
        while (offset < size) {
            uint32_t chunk = size - offset;
            if (chunk > FOE_PAGE_SIZE) {
                chunk = FOE_PAGE_SIZE;
            }

            uint32_t dest = FOE_APP_BASE_ADDR + FOE_IMAGE_OFFSET + offset;

            /* Copy one page from staging to RAM buffer */
            ram_memcpy(sector_buffer, (void *)(FLASH_XIP_STAGING + offset), chunk);

            /* Pad to 4-byte alignment */
            uint32_t prog_chunk = (chunk + 3U) & ~3U;
            if (prog_chunk > chunk) {
                ram_memset(sector_buffer + chunk, 0xFF, prog_chunk - chunk);
            }

            status = rom_xpi_nor_program(BOARD_APP_XPI_NOR_XPI_BASE,
                                         xpi_xfer_channel_auto,
                                         &s_nor_config,
                                         (uint32_t *)sector_buffer,
                                         dest, prog_chunk);
            if (status != 0) {
                ram_puts(RAM_STR("BOOT: prog fail at ")); ram_puthex(dest);
                ram_putc('\n');
                goto do_reset;
            }

            /* Progress: hex digit per 16 pages (= 1 sector) */
            if ((page_count & 0xFU) == 0U) {
                uint32_t sec = page_count >> 4U;
                ram_putc((char)(sec < 10U ? '0' + sec : 'A' + sec - 10U));
            }

            /* Longer delay between pages for flash recovery */
            for (volatile uint32_t _d = 0; _d < 100000U; _d++) {
            }

            offset += chunk;
            page_count++;
        }
        ram_putc('\n');
        ram_puts(RAM_STR("BOOT: prog OK pages=")); ram_puthex(page_count); ram_putc('\n');
        #undef FOE_PAGE_SIZE
    }

do_reset:
    ram_puts(RAM_STR("BOOT: resetting...\n"));
    /* Flush UART TX FIFO: wait for TEMT (bit 6) before reset */
    {
        volatile uint32_t *uart_lsr = (volatile uint32_t *)0xF0040034UL;
        while ((*uart_lsr & 0x40U) == 0) {
        }
    }
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
        LOG_ERROR("FLASH", "init failed");
        boot_failed = 1;
        return;
    }

    FOE_config(&config);
}

void init_boothook(void)
{
    boot_state = BOOT_START;
    foe_total_size = 0;
    LOG_INFO("BOOT", "entering BOOT state");
}

void boot_inithook(void)
{
    if (boot_failed) {
        boot_state = BOOT_FAILED;
    } else {
        boot_state = BOOT_SWAP;
    }
    LOG_INFO("BOOT", "leaving BOOT state");
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
        LOG_INFO("BOOT", "start updating");
        break;

    case BOOT_UPDATING:
        /* Wait for BOOT->INIT transition.
         * If the master disconnects without requesting the transition,
         * self-recover after the safety timeout to avoid getting stuck. */
        board_delay_ms(1);
        boot_timeout++;
        if (boot_timeout > BOOT_TIMEOUT_MAX) {
            LOG_ERROR("BOOT", "timeout waiting for state transition, aborting");
            boot_state = BOOT_FAILED;
        }
        break;

    case BOOT_SWAP:
        if (flush_sector() != 0) {
            boot_state = BOOT_FAILED;
            break;
        }
        LOG_INFO("BOOT", "flash complete, total %lu bytes", (unsigned long)foe_total_size);

        {
            uint32_t crc = calculate_staged_crc32(foe_total_size);
            LOG_INFO("BOOT", "staged CRC32 = 0x%08lx", (unsigned long)crc);
            if (write_metadata(foe_total_size, crc) != 0) {
                LOG_ERROR("BOOT", "metadata write failed");
                boot_state = BOOT_FAILED;
                break;
            }
        }

        boot_state = BOOT_RESET;
        break;

    case BOOT_RESET:
        /* Test ram_putc BEFORE calling copy_firmware_and_reset */
        ram_putc('X');
        ram_putc('Y');
        /* Small delay so UART has time to transmit */
        for (volatile uint32_t _d = 0; _d < 100000U; _d++) {
        }
        LOG_INFO("BOOT", "copying firmware to application area and reset");
        copy_firmware_and_reset();
        break;

    case BOOT_FAILED:
        LOG_ERROR("BOOT", "update failed");
        boot_state = BOOT_IDLE;
        break;

    default:
        break;
    }
}
