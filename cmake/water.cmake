# ######################################################################################################################
# base config

set(carla_water_basedir carla/source/modules/water)

if(OS_WINDOWS)
  set(carla_water_extra_libs OBS::w32-pthreads)
else()
  find_package(Threads REQUIRED)
  set(carla_water_extra_libs ${CMAKE_THREAD_LIBS_INIT})
endif()

if(OS_MACOS)
  set(carla_water_extra_libs ${carla_jackbridge_extra_libs} "-framework AppKit")
elseif(OS_WINDOWS)
  set(carla_water_extra_libs
      ${carla_jackbridge_extra_libs}
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
  set(carla_water_extra_libs ${carla_jackbridge_extra_libs} dl rt)
endif()

# ######################################################################################################################
# static lib

add_library(carla-water STATIC)
mark_as_advanced(carla-water)

if(NOT OS_WINDOWS)
  set_property(TARGET carla-water PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

if(MSVC)

else()
  target_compile_options(carla-water PRIVATE -Wno-deprecated-copy)
endif()

target_include_directories(carla-water PRIVATE carla/source/includes carla/source/utils)

target_link_libraries(carla-water PRIVATE ${carla_water_extra_libs})

if(OS_MACOS)
  target_sources(carla-water PRIVATE ${carla_water_basedir}/water.obs.mm)
else()
  target_sources(carla-water PRIVATE ${carla_water_basedir}/water.obs.cpp)
endif()
