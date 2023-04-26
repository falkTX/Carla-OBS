# Carla plugin for OBS
# Copyright (C) 2023 Filipe Coelho <falktx@falktx.com>
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(carla_lilv INTERFACE)

###############################################################################
# base config

set(carla_lilv_compile_options
  -Wno-error
  -Wno-deprecated-declarations
  -Wno-discarded-qualifiers
  -Wno-format-overflow
  -Wno-implicit-fallthrough
  -Wno-maybe-uninitialized
  -Wno-unused-parameter
)

set(carla_lilv_basedir carla/source/modules/lilv)

set(carla_lilv_include_directories
  carla/source/includes
  ${carla_lilv_basedir}/config
)

###############################################################################
# serd

add_library(carla_lilv_serd STATIC)

set_property(TARGET carla_lilv_serd PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_options(carla_lilv_serd PRIVATE
  ${carla_lilv_compile_options}
)

target_include_directories(carla_lilv_serd PRIVATE
  ${carla_lilv_include_directories}
  ${carla_lilv_basedir}/serd-0.24.0
)

target_sources(carla_lilv_serd PRIVATE
  ${carla_lilv_basedir}/serd.c
)

###############################################################################
# sord

add_library(carla_lilv_sord STATIC)

set_property(TARGET carla_lilv_sord PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_options(carla_lilv_sord PRIVATE
  ${carla_lilv_compile_options}
)

target_include_directories(carla_lilv_sord PRIVATE
  ${carla_lilv_include_directories}
  ${carla_lilv_basedir}/sord-0.16.0
  ${carla_lilv_basedir}/sord-0.16.0/src
)

target_link_libraries(carla_lilv_sord PRIVATE
  carla_lilv_serd
)

target_sources(carla_lilv_sord PRIVATE
  ${carla_lilv_basedir}/sord.c
)

###############################################################################
# sratom

add_library(carla_lilv_sratom STATIC)

set_property(TARGET carla_lilv_sratom PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_options(carla_lilv_sratom PRIVATE
  ${carla_lilv_compile_options}
)

target_include_directories(carla_lilv_sratom PRIVATE
  ${carla_lilv_include_directories}
  ${carla_lilv_basedir}/sratom-0.6.0
)

target_link_libraries(carla_lilv_sratom PRIVATE
  carla_lilv_serd
)

target_sources(carla_lilv_sratom PRIVATE
  ${carla_lilv_basedir}/sratom.c
)

###############################################################################
# lilv

add_library(carla_lilv_lilv STATIC)

set_property(TARGET carla_lilv_lilv PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_options(carla_lilv_lilv PRIVATE
  ${carla_lilv_compile_options}
)

target_include_directories(carla_lilv_lilv PRIVATE
  ${carla_lilv_include_directories}
  ${carla_lilv_basedir}/lilv-0.24.0
  ${carla_lilv_basedir}/lilv-0.24.0/src
)

target_link_libraries(carla_lilv_lilv PRIVATE
  carla_lilv_serd
  carla_lilv_sord
  carla_lilv_sratom
)

target_sources(carla_lilv_lilv PRIVATE
  ${carla_lilv_basedir}/lilv.c
)

###############################################################################
# combined target

target_link_libraries(carla_lilv INTERFACE
  carla_lilv_serd
  carla_lilv_sord
  carla_lilv_sratom
  carla_lilv_lilv
)
