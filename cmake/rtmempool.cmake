# ######################################################################################################################
# base config

find_package(Threads REQUIRED)

set(carla_rtmempool_basedir carla/source/modules/rtmempool)

if(NOT (OS_MACOS OR OS_WINDOWS))
  set(carla_rtmempool_extra_libs "rt")
endif()

# ######################################################################################################################
# static lib

add_library(carla-rtmempool STATIC)

# if(NOT OS_WINDOWS) set_property(TARGET carla-rtmempool PROPERTY POSITION_INDEPENDENT_CODE ON) endif()

# target_compile_definitions(carla-rtmempool PRIVATE REAL_BUILD)

# target_include_directories(carla-rtmempool PRIVATE carla/source/includes carla/source/utils)

# target_link_libraries(carla-rtmempool PRIVATE ${CMAKE_THREAD_LIBS_INIT} ${carla_rtmempool_extra_libs})

# target_sources(carla-rtmempool PRIVATE ${carla_rtmempool_basedir}/rtmempool.c)

target_link_libraries(carla-rtmempool PRIVATE OBS::libobs)

target_sources(carla-rtmempool PRIVATE common.c)

mark_as_advanced(carla-rtmempool)
