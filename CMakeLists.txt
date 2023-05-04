message("hello world")

# cmake_minimum_required(VERSION 3.16...3.25)

# find_qt(COMPONENTS Core Gui Widgets)

# find_package(PkgConfig)

# optional: finds correct plugin binary type if(NOT OS_WINDOWS)
# pkg_check_modules(LIBMAGIC "libmagic")
# else()
# set(LIBMAGIC_FOUND FALSE)
# endif()

# if(NOT (OS_MACOS OR OS_WINDOWS))
# pkg_check_modules(X11 "x11")
# else()
# set(X11_FOUND FALSE)
# endif()

# ######################################################################################################################

# include(cmake/jackbridge.cmake) include(cmake/lilv.cmake) include(cmake/rtmempool.cmake) include(cmake/water.cmake)

# add_library(OBS::carla_jackbridge ALIAS carla_jackbridge) add_library(OBS::carla_lilv ALIAS carla_lilv)
# add_library(OBS::carla_rtmempool ALIAS carla_rtmempool) add_library(OBS::carla_water ALIAS carla_water)

# ######################################################################################################################

# add_library(carla-bridge MODULE) add_library(OBS::carla-bridge ALIAS carla-bridge)

# add_library(carla-patchbay MODULE) add_library(OBS::carla-patchbay ALIAS carla-patchbay)

# ######################################################################################################################

# target_compile_definitions( carla-bridge PRIVATE BUILDING_CARLA CARLA_BACKEND_NAMESPACE=CarlaBridgeOBS
# CARLA_MODULE_ID="carla_bridge" CARLA_MODULE_NAME="Carla Bridge" CARLA_PLUGIN_ONLY_BRIDGE STATIC_PLUGIN_TARGET
# REAL_BUILD $<$<BOOL:${LIBMAGIC_FOUND}>:HAVE_LIBMAGIC> $<$<BOOL:${X11_FOUND}>:HAVE_X11>)

# target_compile_definitions( carla-patchbay PRIVATE BUILDING_CARLA CARLA_BACKEND_NAMESPACE=CarlaPatchbayOBS
# CARLA_MODULE_ID="carla_patchbay" CARLA_MODULE_NAME="Carla Patchbay" CARLA_PLUGIN_BUILD CARLA_PLUGIN_ONLY_BRIDGE
# STATIC_PLUGIN_TARGET REAL_BUILD $<$<BOOL:${LIBMAGIC_FOUND}>:HAVE_LIBMAGIC> $<$<BOOL:${X11_FOUND}>:HAVE_X11>)

# ######################################################################################################################

# target_compile_options(carla-bridge PRIVATE -Wno-error -Wno-vla)

# target_compile_options(carla-patchbay PRIVATE -Wno-error -Wno-vla)

# ######################################################################################################################

# target_include_directories( carla-bridge PRIVATE carla/source carla/source/backend carla/source/frontend
# carla/source/frontend/utils carla/source/includes carla/source/modules carla/source/utils ${LIBMAGIC_INCLUDE_DIRS} )

# target_include_directories( carla-patchbay PRIVATE carla/source carla/source/backend carla/source/includes
# carla/source/modules carla/source/utils ${LIBMAGIC_INCLUDE_DIRS} ${X11_INCLUDE_DIRS})

# ######################################################################################################################

# target_link_directories(carla PRIVATE ${LIBMAGIC_LIBRARY_DIRS} )

# target_link_libraries(
# carla-bridge PRIVATE # OBS::carla_jackbridge OBS::carla_lilv OBS::carla_water
# OBS::libobs OBS::frontend-api Qt::Core Qt::Gui Qt::Widgets ${LIBMAGIC_LIBRARIES}
# )

# target_link_libraries( carla-patchbay PRIVATE # OBS::carla_jackbridge OBS::carla_rtmempool OBS::carla_water
# OBS::libobs OBS::frontend-api Qt::Core Qt::Gui Qt::Widgets ${LIBMAGIC_LIBRARIES} ${X11_LIBRARIES})

# ######################################################################################################################

# if(NOT OS_MACOS)
# target_link_options(carla-bridge PRIVATE -Wl,--no-undefined) target_link_options(carla-patchbay PRIVATE
# -Wl,--no-undefined)
# endif()

# ######################################################################################################################

# target_sources(
# carla-bridge PRIVATE carla.c
# carla-bridge.cpp carla-bridge-wrapper.cpp common.c qtutils.cpp carla/source/backend/utils/CachedPlugins.cpp
# carla/source/backend/utils/JUCE.cpp carla/source/backend/utils/Information.cpp
# carla/source/frontend/carla_frontend.cpp carla/source/frontend/pluginlist/pluginlistdialog.cpp
# carla/source/frontend/pluginlist/pluginlistrefreshdialog.cpp carla/source/utils/CarlaBridgeUtils.cpp
# )

# target_sources( carla-patchbay PRIVATE carla.c carla-patchbay-wrapper.c common.c qtutils.cpp
# carla/source/backend/engine/CarlaEngine.cpp carla/source/backend/engine/CarlaEngineClient.cpp
# carla/source/backend/engine/CarlaEngineData.cpp carla/source/backend/engine/CarlaEngineGraph.cpp
# carla/source/backend/engine/CarlaEngineInternal.cpp carla/source/backend/engine/CarlaEngineNative.cpp
# carla/source/backend/engine/CarlaEnginePorts.cpp carla/source/backend/engine/CarlaEngineRunner.cpp
# carla/source/backend/plugin/CarlaPlugin.cpp carla/source/backend/plugin/CarlaPluginBridge.cpp
# carla/source/backend/plugin/CarlaPluginInternal.cpp )

# ######################################################################################################################

# legacy_check()

# ######################################################################################################################

# set_target_properties_obs( carla-bridge PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON FOLDER plugins PREFIX "")

# set_target_properties_obs(carla-patchbay PROPERTIES FOLDER plugins PREFIX "")

# set_property( TARGET carla-bridge APPEND PROPERTY AUTORCC_OPTIONS --format-version 1)

# ######################################################################################################################
