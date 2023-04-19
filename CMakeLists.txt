project(carla)

find_qt(COMPONENTS Core Gui Widgets)

find_package(PkgConfig)

# optional: finds correct plugin binary type
if(NOT WIN32)
  pkg_check_modules(LIBMAGIC "libmagic")
endif()

add_library(carla MODULE)
add_library(OBS::carla ALIAS carla)

target_compile_definitions(carla PRIVATE
  CARLA_BACKEND_NAMESPACE=CarlaOBS
  #CARLA_ENGINE_WITHOUT_UI
  #CARLA_LIB_EXT=".so"
  #CARLA_PLUGIN_BUILD
  REAL_BUILD
  $<${LIBMAGIC_FOUND}:HAVE_LIBMAGIC>
)

target_compile_options(carla PRIVATE
  -Wno-error
)

target_include_directories(carla PRIVATE
  carla/source
  carla/source/backend
  carla/source/backend/engine
  carla/source/frontend
  carla/source/frontend/pluginlist
  carla/source/frontend/utils
  carla/source/includes
  carla/source/modules
  carla/source/utils
  ${LIBMAGIC_INCLUDE_DIRS}
)

target_link_options(carla PRIVATE
  -Wl,--no-undefined
)

###############################################################################
# lilv

add_library(carla_lilv_1 STATIC)
add_library(carla_lilv_2 STATIC)
add_library(carla_lilv_3 STATIC)
add_library(carla_lilv_4 STATIC)

add_library(carla_lilv INTERFACE)
add_library(OBS::carla_lilv ALIAS carla_lilv)

target_link_libraries(carla_lilv INTERFACE carla_lilv_1 carla_lilv_2 carla_lilv_3 carla_lilv_4)

target_compile_options(carla_lilv_1 PRIVATE
  -fPIC
  -Wno-error
)

target_include_directories(carla_lilv_1 PRIVATE
  carla/source/includes
  carla/source/modules/lilv/config
  carla/source/modules/lilv/lilv-0.24.0
  carla/source/modules/lilv/lilv-0.24.0/src
)

target_sources(carla_lilv_1 PRIVATE
  carla/source/modules/lilv/lilv.c
)

target_compile_options(carla_lilv_2 PRIVATE
  -fPIC
  -Wno-error
)

target_include_directories(carla_lilv_2 PRIVATE
  carla/source/modules/lilv/config
  carla/source/modules/lilv/serd-0.24.0
  carla/source/modules/lilv/serd-0.24.0/src
)

target_sources(carla_lilv_2 PRIVATE
  carla/source/modules/lilv/serd.c
)

target_compile_options(carla_lilv_3 PRIVATE
  -fPIC
  -Wno-error
)

target_include_directories(carla_lilv_3 PRIVATE
  carla/source/includes
  carla/source/modules/lilv/config
  carla/source/modules/lilv/sord-0.16.0
  carla/source/modules/lilv/sord-0.16.0/src
)

target_sources(carla_lilv_3 PRIVATE
  carla/source/modules/lilv/sord.c
)

target_compile_options(carla_lilv_4 PRIVATE
  -fPIC
  -Wno-error
)

target_include_directories(carla_lilv_4 PRIVATE
  carla/source/includes
  carla/source/modules/lilv/config
  carla/source/modules/lilv/sratom-0.6.0
  carla/source/modules/lilv/sratom-0.6.0/src
)

target_sources(carla_lilv_4 PRIVATE
  carla/source/modules/lilv/sratom.c
)

###############################################################################

target_sources(carla PRIVATE
  carla.c
  carla-bridge-wrapper.cpp
  qtutils.cpp
  carla/source/backend/utils/CachedPlugins.cpp
  carla/source/backend/utils/JUCE.cpp
  carla/source/backend/utils/Information.cpp
  carla/source/frontend/carla_frontend.cpp
  carla/source/frontend/pluginlist/pluginlistdialog.cpp
  carla/source/frontend/pluginlist/pluginlistrefreshdialog.cpp
  carla/source/jackbridge/JackBridge1.cpp
  carla/source/jackbridge/JackBridge2.cpp
  carla/source/modules/water/water.cpp
  carla/source/utils/CarlaBridgeUtils.cpp
)

target_link_directories(carla PRIVATE
  ${LIBMAGIC_LIBRARY_DIRS}
)

target_link_libraries(carla PRIVATE
  OBS::carla_lilv
  OBS::libobs
  OBS::frontend-api
  Qt::Core
  Qt::Gui
  Qt::Widgets
  ${LIBMAGIC_LIBRARIES}
)

set_target_properties(carla PROPERTIES
  AUTOMOC ON
  AUTOUIC ON
  AUTORCC ON
  FOLDER "plugins"
  PROJECT_LABEL "Carla Plugin Host"
)

setup_plugin_target(carla)
