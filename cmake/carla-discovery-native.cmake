add_executable(carla-discovery-native)
mark_as_advanced(carla-discovery-native)

target_compile_definitions(carla-discovery-native PRIVATE CARLA_BACKEND_NAMESPACE=CarlaOBS)

target_compile_options(carla-discovery-native PRIVATE $<$<BOOL:${MSVC}>:/wd4267> $<$<BOOL:${OS_MACOS}>:-ObjC++>
                                                      $<$<NOT:$<BOOL:${MSVC}>>:-Wno-error>)

target_include_directories(carla-discovery-native PRIVATE carla/source/backend carla/source/includes
                                                          carla/source/modules carla/source/utils)

# TODO -mwindows
target_link_libraries(carla-discovery-native PRIVATE carla::lilv $<$<BOOL:${OS_MACOS}>:-framework AppKit>
                                                     $<$<BOOL:${OS_WINDOWS}>:ole32>)

target_sources(carla-discovery-native PRIVATE carla/source/discovery/carla-discovery.cpp
                                              carla/source/modules/water/water.files.cpp)

set_target_properties(carla-discovery-native PROPERTIES FOLDER "plugins/carla")

if(NOT OS_MACOS)
  setup_plugin_target(carla-discovery-native)
endif()
