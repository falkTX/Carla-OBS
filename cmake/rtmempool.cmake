# ######################################################################################################################
# base config

# find_package(Threads REQUIRED)

# set(carla_rtmempool_basedir carla/source/modules/rtmempool)

# if(NOT (OS_MACOS OR OS_WINDOWS))
# set(carla_rtmempool_extra_libs "rt")
# endif()

# ######################################################################################################################
# static lib

add_library(carla_rtmempool INTERFACE)

# if(NOT OS_WINDOWS)
# set_property(TARGET carla_rtmempool PROPERTY POSITION_INDEPENDENT_CODE ON)
# endif()

# target_compile_definitions(carla_rtmempool PRIVATE REAL_BUILD)

# target_include_directories(carla_rtmempool PRIVATE carla/source/includes carla/source/utils)

# target_link_libraries(carla_rtmempool PRIVATE ${CMAKE_THREAD_LIBS_INIT} ${carla_rtmempool_extra_libs})
target_link_libraries(carla_rtmempool INTERFACE OBS::libobs)

target_sources(carla_rtmempool PRIVATE common.c)

# target_sources(carla_rtmempool PRIVATE ${carla_rtmempool_basedir}/rtmempool.c)
