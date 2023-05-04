# ######################################################################################################################
# base config

find_package(Threads REQUIRED)

set(carla_jackbridge_basedir carla/source/jackbridge)

if(NOT (OS_MACOS OR OS_WINDOWS))
  set(carla_jackbridge_extra_libs "dl" "rt")
endif()

# ######################################################################################################################
# static lib

add_library(carla_jackbridge STATIC)

set_property(TARGET carla_jackbridge PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(carla_jackbridge PUBLIC REAL_BUILD)

target_include_directories(carla_jackbridge PUBLIC carla/source/includes carla/source/utils)

target_link_libraries(carla_jackbridge PUBLIC ${CMAKE_THREAD_LIBS_INIT} ${carla_jackbridge_extra_libs})

target_sources(carla_jackbridge PUBLIC ${carla_jackbridge_basedir}/JackBridge1.cpp
                                       ${carla_jackbridge_basedir}/JackBridge2.cpp)
