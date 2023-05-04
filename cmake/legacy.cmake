
# ######################################################################################################################

set_target_properties(
  carla-bridge
  PROPERTIES AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON
             FOLDER plugins
             PREFIX ""
             PROJECT_LABEL "Carla Bridge")

set_target_properties(
  carla-patchbay
  PROPERTIES FOLDER plugins
             PREFIX ""
             PROJECT_LABEL "Carla Patchbay")

# ######################################################################################################################

setup_plugin_target(carla-bridge)
setup_plugin_target(carla-patchbay)

# ######################################################################################################################
