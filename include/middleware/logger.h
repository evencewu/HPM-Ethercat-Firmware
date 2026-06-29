/*
 * Lightweight logging system for HPM5E00 embedded applications.
 *
 * All functionality is implemented as preprocessor macros so that
 * disabled log levels compile to nothing — zero CPU overhead and
 * zero flash/rodata footprint for supressed messages.
 *
 * Output format:  [LEVEL][TAG] message\n
 *
 * Usage:
 *   LOG_INFO("MAIN", "board init done");
 *   LOG_ERROR("FLASH", "erase failed at 0x%08lx", addr);
 *
 * The TAG must be a C string literal, not a runtime variable.
 *
 * Compile-time filtering:
 *   Define LOG_LEVEL before including this header:
 *     #define LOG_LEVEL LOG_LVL_NONE   — silence everything
 *     #define LOG_LEVEL LOG_LVL_ERROR  — errors only
 *     #define LOG_LEVEL LOG_LVL_WARN   — errors + warnings
 *     #define LOG_LEVEL LOG_LVL_INFO   — normal messages (default)
 *     #define LOG_LEVEL LOG_LVL_DEBUG  — verbose diagnostic output
 *
 * Licensed under the GNU General Public License version 2 with exceptions.
 */

#ifndef __middleware_logger__
#define __middleware_logger__

#include <stdio.h>

/* ------------------------------------------------------------------ *
 * Log level definitions
 * ------------------------------------------------------------------ */

#define LOG_LVL_NONE   0
#define LOG_LVL_ERROR  1
#define LOG_LVL_WARN   2
#define LOG_LVL_INFO   3
#define LOG_LVL_DEBUG  4

/* Default: INFO if nothing defined externally */
#ifndef LOG_LEVEL
#define LOG_LEVEL  LOG_LVL_INFO
#endif

/* ------------------------------------------------------------------ *
 * Log macros — compile-time guarded by LOG_LEVEL
 * ------------------------------------------------------------------ */

/** Always-active printf passthrough. */
#define LOG_RAW(fmt, ...) \
    printf(fmt "\n", ##__VA_ARGS__)

#if LOG_LEVEL >= LOG_LVL_ERROR
#define LOG_ERROR(tag, fmt, ...) \
    printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
#define LOG_ERROR(tag, fmt, ...)  ((void)0)
#endif

#if LOG_LEVEL >= LOG_LVL_WARN
#define LOG_WARN(tag, fmt, ...) \
    printf("[WARN][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
#define LOG_WARN(tag, fmt, ...)  ((void)0)
#endif

#if LOG_LEVEL >= LOG_LVL_INFO
#define LOG_INFO(tag, fmt, ...) \
    printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
#define LOG_INFO(tag, fmt, ...)  ((void)0)
#endif

#if LOG_LEVEL >= LOG_LVL_DEBUG
#define LOG_DEBUG(tag, fmt, ...) \
    printf("[DEBUG][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
#define LOG_DEBUG(tag, fmt, ...)  ((void)0)
#endif

#endif /* __middleware_logger__ */
