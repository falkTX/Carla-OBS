cmake_minimum_required(VERSION 3.0)

legacy_check()

find_qt(COMPONENTS Widgets)

add_library(carla MODULE)
add_library(OBS::carla ALIAS carla)

target_sources(carla PRIVATE
  carla.cpp
  carla/source/backend/engine/CarlaEngine.cpp
)

target_link_libraries(carla PRIVATE OBS::libobs Qt::Widgets)

set_target_properties_obs(image-source PROPERTIES
  FOLDER plugins
  PREFIX ""
)
