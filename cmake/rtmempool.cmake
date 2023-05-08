# base config
set(carla_rtmempool_basedir carla/source/modules/rtmempool)
set(carla_rtmempool_extra_libs ${carla_pthread_libs})

# static lib
add_library(carla-rtmempool STATIC)
mark_as_advanced(carla-rtmempool)

if(NOT OS_WINDOWS)
  set_property(TARGET carla-rtmempool PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_include_directories(carla-rtmempool PRIVATE carla/source/includes)

target_link_libraries(carla-rtmempool PUBLIC ${carla_rtmempool_extra_libs})

target_sources(carla-rtmempool PRIVATE ${carla_rtmempool_basedir}/rtmempool.c)
