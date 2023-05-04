project(carla)

option(ENABLE_CARLA "Enable building OBS with carla plugin host" ON)
if(NOT ENABLE_CARLA)
  message(STATUS "OBS:  DISABLED   carla")
  return()
endif()

# Submodule deps check
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/carla/source/utils/CarlaUtils.hpp)
  obs_status(FATAL_ERROR "carla submodule deps not available.")
endif()

# Find Qt
find_qt(COMPONENTS Core Widgets)

# Use pkg-config to find optional deps
find_package(PkgConfig)

# Optional: finds correct plugin binary type
if(PKGCONFIG_FOUND AND NOT OS_WINDOWS)
  pkg_check_modules(LIBMAGIC "libmagic")
else()
  set(LIBMAGIC_FOUND FALSE)
endif()

# Optional: transient X11 window flags
if(PKGCONFIG_FOUND AND NOT (OS_MACOS OR OS_WINDOWS))
  pkg_check_modules(X11 "x11")
else()
  set(X11_FOUND FALSE)
endif()

# Import extra carla libs
include(cmake/jackbridge.cmake)
include(cmake/lilv.cmake)
include(cmake/rtmempool.cmake)
include(cmake/water.cmake)

# Import extra carla libs
include(cmake/jackbridge.cmake)
include(cmake/lilv.cmake)
include(cmake/rtmempool.cmake)
include(cmake/water.cmake)

add_library(OBS::carla_jackbridge ALIAS carla_jackbridge)
add_library(OBS::carla_lilv ALIAS carla_lilv)
add_library(OBS::carla_rtmempool ALIAS carla_rtmempool)
add_library(OBS::carla_water ALIAS carla_water)

# Setup carla-bridge target
add_library(carla-bridge MODULE)
add_library(OBS::carla-bridge ALIAS carla-bridge)

# Setup carla-patchbay target
add_library(carla-patchbay MODULE)
add_library(OBS::carla-patchbay ALIAS carla-patchbay)

target_link_libraries(carla-patchbay PRIVATE OBS::libobs)

target_sources(carla-patchbay PRIVATE carla.c)

set_target_properties(carla-patchbay PROPERTIES FOLDER "plugins/carla")

setup_plugin_target(carla-patchbay)

# ######################################################################################################################

# set_target_properties( carla-bridge PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON FOLDER plugins PREFIX "")

# set_target_properties(carla-patchbay PROPERTIES FOLDER plugins PREFIX "")

# if(_QT_VERSION EQUAL 6 AND OS_WINDOWS) set_target_properties(carla-bridge PROPERTIES AUTORCC_OPTIONS
# "--format-version;1") endif()

# ######################################################################################################################

# setup_plugin_target(carla-bridge) setup_plugin_target(carla-patchbay)

# ######################################################################################################################
