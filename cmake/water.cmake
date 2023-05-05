# ######################################################################################################################
# base config

find_package(Threads REQUIRED)

set(carla_water_basedir carla/source/modules/water)

if(OS_MACOS)
  set(carla_water_extra_libs "-framework AppKit")
elseif(OS_WINDOWS)
  set(carla_water_extra_libs
      "uuid"
      "wsock32"
      "wininet"
      "version"
      "ole32"
      "ws2_32"
      "oleaut32"
      "imm32"
      "comdlg32"
      "shlwapi"
      "rpcrt4"
      "winmm")
else()
  set(carla_water_extra_libs "dl" "rt")
endif()

# ######################################################################################################################
# static lib

add_library(carla_water STATIC)

if(NOT OS_WINDOWS)
  set_property(TARGET carla_water PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_definitions(carla_water PRIVATE REAL_BUILD)

target_compile_options(carla_water PRIVATE -Wno-deprecated-copy)

target_include_directories(carla_water PRIVATE carla/source/includes carla/source/utils)

target_link_libraries(carla_water PRIVATE ${CMAKE_THREAD_LIBS_INIT} ${carla_water_extra_libs})

if(OS_MACOS)
  target_sources(carla_water PRIVATE ${carla_water_basedir}/water.mm)
else()
  target_sources(carla_water PRIVATE ${carla_water_basedir}/water.cpp)
endif()
