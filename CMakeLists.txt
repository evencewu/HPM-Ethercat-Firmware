# Copyright (c) 2021 HPMicro
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.13)

# Suppress the HPM banner — must be set BEFORE find_package so it
# applies to the SDK static library (board.c) too.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DBOARD_SHOW_BANNER=0")

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})

project(foe_slave)

set(SOES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../dependence/SOES")

sdk_inc(./include)
sdk_inc(./src/esc_soes)
sdk_inc(./src/esc_hal)
sdk_inc(${SOES_DIR}/soes)
sdk_inc(${SOES_DIR}/soes/include/sys/gcc)

# SOES core sources
sdk_app_src(${SOES_DIR}/soes/esc.c)
sdk_app_src(${SOES_DIR}/soes/ecat_slv.c)
sdk_app_src(${SOES_DIR}/soes/esc_coe.c)
sdk_app_src(${SOES_DIR}/soes/esc_foe.c)
sdk_app_src(${SOES_DIR}/soes/esc_eep.c)

# HPM5E00 HAL for SOES (local copy)
sdk_app_src(src/esc_hal/esc_hw.c)
sdk_app_src(src/esc_hal/esc_hw_eep.c)
sdk_app_src(src/esc_hal/sii_eeprom_data.c)

# Application sources
sdk_app_src(src/esc_soes/main.c)
sdk_app_src(src/esc_soes/objectlist.c)
sdk_app_src(src/esc_soes/bootstrap.c)
sdk_app_src(src/esc_soes/esc_init.c)

# Logger middleware
sdk_app_src(src/middleware/logger.c)

# Default log level: INFO for debug builds, ERROR for release.
if(NOT DEFINED LOG_LEVEL)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(LOG_LEVEL 1)
    else()
        set(LOG_LEVEL 3)
    endif()
endif()
add_compile_definitions(LOG_LEVEL=${LOG_LEVEL})

# BOOT mailbox config is now set in ecat_options.h (MBXSIZEBOOT=0x1000, 4096 bytes)

generate_ide_projects()
