# Copyright (c) 2021 HPMicro
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.13)

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

# BOOT mailbox config is now set in ecat_options.h (MBXSIZEBOOT=0x1000, 4096 bytes)

generate_ide_projects()
