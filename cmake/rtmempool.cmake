# Carla plugin for OBS Copyright (C) 2023 Filipe Coelho <falktx@falktx.com> SPDX-License-Identifier: GPL-2.0-or-later

# ######################################################################################################################
# base config

find_package(Threads REQUIRED)

set(carla_rtmempool_basedir carla/source/modules/rtmempool)

if(NOT (APPLE OR WIN32))
  set(carla_rtmempool_extra_libs "rt")
endif()

# ######################################################################################################################
# static lib

add_library(carla_rtmempool STATIC)

set_property(TARGET carla_rtmempool PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(carla_rtmempool PUBLIC REAL_BUILD)

target_include_directories(carla_rtmempool PUBLIC carla/source/includes carla/source/utils)

target_link_libraries(carla_rtmempool PUBLIC ${CMAKE_THREAD_LIBS_INIT} ${carla_rtmempool_extra_libs})

target_sources(carla_rtmempool PUBLIC ${carla_rtmempool_basedir}/rtmempool.c)
