project(carla)

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
