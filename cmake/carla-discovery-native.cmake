add_executable(carla-discovery-native)
mark_as_advanced(carla-discovery-native)

set(carla_discovery_extra_libs ${carla_pthread_libs})

if(OS_MACOS)
  find_library(APPKIT AppKit)
  mark_as_advanced(APPKIT)
  set(carla_discovery_extra_libs ${carla_discovery_extra_libs} ${APPKIT})
elseif(OS_WINDOWS)
  set(carla_discovery_extra_libs ${carla_discovery_extra_libs} ole32)
elseif(NOT OS_FREEBSD)
  set(carla_discovery_extra_libs ${carla_discovery_extra_libs} dl)
endif()

target_compile_definitions(carla-discovery-native PRIVATE CARLA_BACKEND_NAMESPACE=CarlaOBS)

target_compile_options(carla-discovery-native PRIVATE $<$<BOOL:${MSVC}>:/wd4267> $<$<BOOL:${OS_MACOS}>:-ObjC++>
                                                      $<$<NOT:$<BOOL:${MSVC}>>:-Wno-error -Werror=vla>)

target_include_directories(carla-discovery-native PRIVATE carla/source/backend carla/source/includes
                                                          carla/source/modules carla/source/utils)

# TODO -mwindows
target_link_libraries(carla-discovery-native PRIVATE carla::lilv ${carla_discovery_extra_libs})

target_sources(carla-discovery-native PRIVATE carla/source/discovery/carla-discovery.cpp
                                              carla/source/modules/water/water.files.cpp)

set_target_properties(carla-discovery-native PROPERTIES FOLDER "plugins/carla")

if(NOT OS_MACOS)
  setup_plugin_target(carla-discovery-native)
endif()
