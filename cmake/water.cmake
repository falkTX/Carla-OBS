# Carla plugin for OBS
# Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(carla_water STATIC)

###############################################################################
# base config

set(carla_water_basedir carla/source/modules/water)

###############################################################################
# static lib

set_property(TARGET carla_water PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(carla_water PRIVATE
  REAL_BUILD
)

#target_compile_options(carla_water PRIVATE
  #-Wno-error
#)

target_include_directories(carla_water PRIVATE
  carla/source/includes
  carla/source/utils
)

target_sources(carla_water PRIVATE
  ${carla_water_basedir}/water.cpp
)
