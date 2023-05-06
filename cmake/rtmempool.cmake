# ######################################################################################################################
# base config

set(carla_rtmempool_basedir carla/source/modules/rtmempool)

if(OS_WINDOWS)
  set(carla_rtmempool_extra_libs OBS::w32-pthreads)
else()
  find_package(Threads REQUIRED)
  set(carla_rtmempool_extra_libs ${CMAKE_THREAD_LIBS_INIT})
endif()

# ######################################################################################################################
# static lib

add_library(carla-rtmempool STATIC)
mark_as_advanced(carla-rtmempool)

if(NOT OS_WINDOWS)
  set_property(TARGET carla-rtmempool PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_link_libraries(carla-rtmempool PRIVATE ${carla_rtmempool_extra_libs})

target_sources(carla-rtmempool PRIVATE ${carla_rtmempool_basedir}/rtmempool.c)
