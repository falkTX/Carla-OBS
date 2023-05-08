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

# Optional: X11 support on freedesktop systems
if(PKGCONFIG_FOUND AND NOT (OS_MACOS OR OS_WINDOWS))
  pkg_check_modules(X11 "x11")
else()
  set(X11_FOUND FALSE)
endif()

# Import extra carla libs
include(cmake/jackbridge.cmake)
add_library(carla::jackbridge ALIAS carla-jackbridge)

include(cmake/lilv.cmake)
add_library(carla::lilv ALIAS carla-lilv)

include(cmake/rtmempool.cmake)
add_library(carla::rtmempool ALIAS carla-rtmempool)

include(cmake/water.cmake)
add_library(carla::water ALIAS carla-water)

# Setup binary tools
include(cmake/carla-discovery-native.cmake)
include(cmake/carla-bridge-native.cmake)

# Setup carla-bridge target
add_library(carla-bridge MODULE)
add_library(OBS::carla-bridge ALIAS carla-bridge)

target_compile_definitions(
  carla-bridge
  PRIVATE BUILDING_CARLA
          BUILDING_CARLA_OBS
          CARLA_BACKEND_NAMESPACE=CarlaBridgeOBS
          CARLA_MODULE_ID="carla-bridge"
          CARLA_MODULE_NAME="Carla Bridge"
          CARLA_PLUGIN_ONLY_BRIDGE
          STATIC_PLUGIN_TARGET
          $<$<BOOL:${LIBMAGIC_FOUND}>:HAVE_LIBMAGIC>)

target_include_directories(
  carla-bridge
  PRIVATE carla/source
          carla/source/backend
          carla/source/frontend
          carla/source/frontend/utils
          carla/source/includes
          carla/source/modules
          carla/source/utils
          ${LIBMAGIC_INCLUDE_DIRS})

target_link_libraries(carla-bridge PRIVATE carla::jackbridge carla::lilv OBS::libobs Qt::Core Qt::Widgets
                                           ${LIBMAGIC_LIBRARIES})

if(NOT (OS_MACOS OR OS_WINDOWS))
  target_link_options(carla-bridge PRIVATE -Wl,--no-undefined)
endif()

target_sources(
  carla-bridge
  PRIVATE carla.c
          carla-bridge.cpp
          carla-bridge-wrapper.cpp
          common.c
          qtutils.cpp
          carla/source/backend/utils/CachedPlugins.cpp
          carla/source/backend/utils/Information.cpp
          carla/source/frontend/carla_frontend.cpp
          carla/source/frontend/pluginlist/pluginlistdialog.cpp
          carla/source/frontend/pluginlist/pluginlistrefreshdialog.cpp
          carla/source/utils/CarlaBridgeUtils.cpp
          carla/source/utils/CarlaMacUtils.cpp)

if(OS_MACOS)
  set_source_files_properties(carla/source/utils/CarlaMacUtils.cpp PROPERTIES COMPILE_FLAGS -ObjC++)
endif()

set_target_properties(
  carla-bridge
  PROPERTIES AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON
             FOLDER plugins
             PREFIX "")

if(_QT_VERSION EQUAL 6 AND OS_WINDOWS)
  set_target_properties(carla-bridge PROPERTIES AUTORCC_OPTIONS "--format-version;1")
endif()

setup_plugin_target(carla-bridge)

# Setup carla-patchbay target (not available on Windows for now)
if(NOT OS_WINDOWS)

  add_library(carla-patchbay MODULE)
  add_library(OBS::carla-patchbay ALIAS carla-patchbay)

  target_compile_definitions(
    carla-patchbay
    PRIVATE BUILDING_CARLA
            CARLA_BACKEND_NAMESPACE=CarlaPatchbayOBS
            CARLA_MODULE_ID="carla-patchbay"
            CARLA_MODULE_NAME="Carla Patchbay"
            CARLA_PLUGIN_BUILD
            CARLA_PLUGIN_ONLY_BRIDGE
            STATIC_PLUGIN_TARGET
            $<$<BOOL:${LIBMAGIC_FOUND}>:HAVE_LIBMAGIC>
            $<$<BOOL:${X11_FOUND}>:HAVE_X11>)

  if(NOT MSVC)
    target_compile_options(carla-patchbay PRIVATE -Wno-error=vla)
  endif()

  target_include_directories(
    carla-patchbay
    PRIVATE carla/source
            carla/source/backend
            carla/source/includes
            carla/source/modules
            carla/source/utils
            ${LIBMAGIC_INCLUDE_DIRS}
            ${X11_INCLUDE_DIRS})

  target_link_directories(carla-patchbay PRIVATE ${LIBMAGIC_LIBRARY_DIRS})

  target_link_libraries(
    carla-patchbay
    PRIVATE carla::jackbridge
            carla::rtmempool
            carla::water
            OBS::libobs
            OBS::frontend-api
            Qt::Core
            Qt::Widgets
            ${LIBMAGIC_LIBRARIES}
            ${X11_LIBRARIES})

  if(NOT (OS_MACOS OR OS_WINDOWS))
    target_link_options(carla-patchbay PRIVATE -Wl,--no-undefined)
  endif()

  target_sources(
    carla-patchbay
    PRIVATE carla.c
            carla-patchbay-wrapper.c
            common.c
            qtutils.cpp
            carla/source/backend/engine/CarlaEngine.cpp
            carla/source/backend/engine/CarlaEngineClient.cpp
            carla/source/backend/engine/CarlaEngineData.cpp
            carla/source/backend/engine/CarlaEngineGraph.cpp
            carla/source/backend/engine/CarlaEngineInternal.cpp
            carla/source/backend/engine/CarlaEngineNative.cpp
            carla/source/backend/engine/CarlaEnginePorts.cpp
            carla/source/backend/engine/CarlaEngineRunner.cpp
            carla/source/backend/plugin/CarlaPlugin.cpp
            carla/source/backend/plugin/CarlaPluginBridge.cpp
            carla/source/backend/plugin/CarlaPluginInternal.cpp)

  if(OS_MACOS)
    set_source_files_properties(carla/source/backend/engine/CarlaEngineNative.cpp PROPERTIES COMPILE_FLAGS -ObjC++)
  endif()

  set_target_properties(carla-patchbay PROPERTIES FOLDER plugins PREFIX "")

  setup_plugin_target(carla-patchbay)

endif()
