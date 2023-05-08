add_executable(carla-discovery-native)
mark_as_advanced(carla-discovery-native)

# TODO HAVE_FLUIDSYNTH HAVE_YSFX

# target_compile_definitions(carla-discovery-native PRIVATE HAVE_YSFX)

target_compile_options(carla-discovery-native PRIVATE $<$<BOOL:${OS_MACOS}>:-ObjC++> $<$<NOT:$<BOOL:${MSVC}>>:-Wno-vla>)

target_include_directories(carla-discovery-native PRIVATE carla/source/backend carla/source/includes
                                                          carla/source/modules carla/source/utils)

# TODO -mwindows
target_link_libraries(carla-discovery-native PRIVATE carla::lilv $<$<BOOL:${OS_WINDOWS}>:ole32>)

target_sources(carla-discovery-native PRIVATE carla/source/discovery/carla-discovery.cpp
                                              carla/source/modules/water/water.files.cpp)
