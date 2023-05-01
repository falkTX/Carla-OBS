# Carla plugin for OBS
# Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(carla_rtmempool STATIC)

###############################################################################
# base config

set(carla_rtmempool_basedir carla/source/modules/rtmempool)

###############################################################################
# static lib

set_property(TARGET carla_rtmempool PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(carla_rtmempool PRIVATE
  REAL_BUILD
)

#target_compile_options(carla_water PRIVATE
  #-Wno-error
#)

target_include_directories(carla_rtmempool PRIVATE
  carla/source/includes
  carla/source/utils
)

target_sources(carla_rtmempool PRIVATE
  ${carla_rtmempool_basedir}/rtmempool.c
)
