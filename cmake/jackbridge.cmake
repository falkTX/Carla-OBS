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

if(NOT OS_WINDOWS)
  set_property(TARGET carla_jackbridge PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_definitions(carla_jackbridge PRIVATE REAL_BUILD)

target_include_directories(carla_jackbridge PRIVATE carla/source/includes carla/source/utils)

target_link_libraries(carla_jackbridge PRIVATE ${CMAKE_THREAD_LIBS_INIT} ${carla_jackbridge_extra_libs})

target_sources(carla_jackbridge PRIVATE ${carla_jackbridge_basedir}/JackBridge1.cpp
                                        ${carla_jackbridge_basedir}/JackBridge2.cpp)
