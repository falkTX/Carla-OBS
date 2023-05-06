# ######################################################################################################################
# base config

set(carla_jackbridge_basedir carla/source/jackbridge)

if(OS_WINDOWS)
  set(carla_jackbridge_extra_libs OBS::w32-pthreads kernel32)
elseif(NOT (OS_FREEBSD OR OS_MACOS))
  set(carla_jackbridge_extra_libs "dl" "rt")
endif()

# ######################################################################################################################
# static lib

add_library(carla-jackbridge STATIC)

if(NOT OS_WINDOWS)
  set_property(TARGET carla-jackbridge PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_definitions(carla-jackbridge PRIVATE REAL_BUILD)

target_include_directories(carla-jackbridge PRIVATE carla/source/includes carla/source/utils)

target_link_libraries(carla-jackbridge PRIVATE OBS::libobs ${carla_jackbridge_extra_libs})

# target_sources(carla-jackbridge PRIVATE ${carla_jackbridge_basedir}/JackBridge1.cpp
# ${carla_jackbridge_basedir}/JackBridge2.cpp)

target_sources(carla-jackbridge PRIVATE common.c ${carla_jackbridge_basedir}/JackBridge1.cpp)

mark_as_advanced(carla-jackbridge)
