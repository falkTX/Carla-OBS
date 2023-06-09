# base config
set(carla_jackbridge_basedir carla/source/jackbridge)
set(carla_jackbridge_extra_libs ${carla_pthread_libs})

if(NOT
   (OS_FREEBSD
    OR OS_MACOS
    OR OS_WINDOWS))
  set(carla_jackbridge_extra_libs ${carla_jackbridge_extra_libs} dl rt)
endif()

# static lib
add_library(carla-jackbridge STATIC)
mark_as_advanced(carla-jackbridge)

set_target_properties(carla-jackbridge PROPERTIES OSX_ARCHITECTURES "x86_64;arm64" POSITION_INDEPENDENT_CODE ON)

target_include_directories(carla-jackbridge PRIVATE carla/source/includes carla/source/utils)

target_link_libraries(carla-jackbridge PUBLIC ${carla_jackbridge_extra_libs})

target_sources(carla-jackbridge PRIVATE ${carla_jackbridge_basedir}/JackBridge1.cpp
                                        ${carla_jackbridge_basedir}/JackBridge2.cpp)
