# Carla plugin for OBS
# Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(carla_jackbridge STATIC)

###############################################################################
# base config

set(carla_jackbridge_basedir carla/source/jackbridge)

###############################################################################
# static lib

set_property(TARGET carla_jackbridge PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(carla_jackbridge PRIVATE
  REAL_BUILD
)

#target_compile_options(carla_jackbridge PRIVATE
  #-Wno-error
#)

target_include_directories(carla_jackbridge PRIVATE
  carla/source/includes
  carla/source/utils
)

target_sources(carla_jackbridge PRIVATE
  ${carla_jackbridge_basedir}/JackBridge1.cpp
  ${carla_jackbridge_basedir}/JackBridge2.cpp
)
