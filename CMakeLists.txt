project(carla)

find_qt(COMPONENTS Core Gui Widgets)

find_package(PkgConfig)

# optional: finds correct plugin binary type
if(NOT WIN32)
  pkg_check_modules(LIBMAGIC "libmagic")
endif()

if(NOT (APPLE OR WIN32))
  set(CARLA_EXTRA_LIBS "dl" "rt")
endif()

add_library(carla MODULE)
add_library(OBS::carla ALIAS carla)

###############################################################################

include(cmake/jackbridge.cmake)
include(cmake/lilv.cmake)
include(cmake/water.cmake)

add_library(OBS::carla_jackbridge ALIAS carla_jackbridge)
add_library(OBS::carla_lilv ALIAS carla_lilv)
add_library(OBS::carla_water ALIAS carla_water)

###############################################################################

target_compile_definitions(carla PRIVATE
  CARLA_BACKEND_NAMESPACE=CarlaOBS
  #CARLA_ENGINE_WITHOUT_UI
  #CARLA_LIB_EXT=".so"
  STATIC_PLUGIN_TARGET
  REAL_BUILD
  # $<${LIBMAGIC_FOUND}:HAVE_LIBMAGIC>
)

target_compile_options(carla PRIVATE
  -Wno-error
  -Wno-vla
)

target_include_directories(carla PRIVATE
  carla/source
  carla/source/backend
  carla/source/frontend
  carla/source/frontend/utils
  carla/source/includes
  carla/source/modules
  carla/source/utils
  ${LIBMAGIC_INCLUDE_DIRS}
)

target_link_directories(carla PRIVATE
  ${LIBMAGIC_LIBRARY_DIRS}
)

target_link_libraries(carla PRIVATE
  OBS::carla_jackbridge
  OBS::carla_lilv
  OBS::carla_water
  OBS::libobs
  OBS::frontend-api
  Qt::Core
  Qt::Gui
  Qt::Widgets
  ${CARLA_EXTRA_LIBS}
  ${LIBMAGIC_LIBRARIES}
)

target_link_options(carla PRIVATE
  -Wl,--no-undefined
)

set_target_properties(carla PROPERTIES
  AUTOMOC ON
  AUTOUIC ON
  AUTORCC ON
  FOLDER "plugins"
  PROJECT_LABEL "Carla Plugin Host"
)

target_sources(carla PRIVATE
  carla.c
  carla-bridge.cpp
  carla-bridge-wrapper.cpp
  qtutils.cpp
  carla/source/backend/utils/CachedPlugins.cpp
  carla/source/backend/utils/JUCE.cpp
  carla/source/backend/utils/Information.cpp
  carla/source/frontend/carla_frontend.cpp
  carla/source/frontend/pluginlist/pluginlistdialog.cpp
  carla/source/frontend/pluginlist/pluginlistrefreshdialog.cpp
  carla/source/utils/CarlaBridgeUtils.cpp
)

setup_plugin_target(carla)
