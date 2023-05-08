add_executable(carla-bridge-native)
mark_as_advanced(carla-bridge-native)

target_compile_definitions(
  carla-bridge-native
  PRIVATE BUILDING_CARLA
          BUILD_BRIDGE
          BUILD_BRIDGE_ALTERNATIVE_ARCH
          CARLA_BACKEND_NAMESPACE=CarlaOBS
          CARLA_LIB_EXT="${CMAKE_SHARED_LIBRARY_SUFFIX}"
          $<$<BOOL:${LIBMAGIC_FOUND}>:HAVE_LIBMAGIC>
          $<$<BOOL:${X11_FOUND}>:HAVE_X11>)

target_compile_options(
  carla-bridge-native PRIVATE $<$<BOOL:${OS_MACOS}>:-ObjC++> $<$<NOT:$<BOOL:${MSVC}>>:-Wno-error>)

target_include_directories(
  carla-bridge-native
  PRIVATE carla/source
          carla/source/backend
          carla/source/backend/engine
          carla/source/backend/plugin
          carla/source/includes
          carla/source/modules
          carla/source/utils
          ${LIBMAGIC_INCLUDE_DIRS}
          ${X11_INCLUDE_DIRS})

# TODO -mwindows
target_link_libraries(carla-bridge-native PRIVATE carla::jackbridge carla::lilv carla::rtmempool carla::water
                                                  ${LIBMAGIC_LIBRARIES} ${X11_LIBRARIES})

target_sources(
  carla-bridge-native
  PRIVATE carla/source/bridges-plugin/CarlaBridgePlugin.cpp
          carla/source/backend/CarlaStandalone.cpp
          carla/source/backend/engine/CarlaEngine.cpp
          carla/source/backend/engine/CarlaEngineBridge.cpp
          carla/source/backend/engine/CarlaEngineClient.cpp
          carla/source/backend/engine/CarlaEngineData.cpp
          carla/source/backend/engine/CarlaEngineInternal.cpp
          carla/source/backend/engine/CarlaEnginePorts.cpp
          carla/source/backend/engine/CarlaEngineRunner.cpp
          carla/source/backend/plugin/CarlaPlugin.cpp
          carla/source/backend/plugin/CarlaPluginBridge.cpp
          carla/source/backend/plugin/CarlaPluginInternal.cpp
          carla/source/backend/plugin/CarlaPluginAU.cpp
          carla/source/backend/plugin/CarlaPluginCLAP.cpp
          carla/source/backend/plugin/CarlaPluginLADSPADSSI.cpp
          carla/source/backend/plugin/CarlaPluginLV2.cpp
          carla/source/backend/plugin/CarlaPluginVST2.cpp
          carla/source/backend/plugin/CarlaPluginVST3.cpp)

set_target_properties(carla-bridge-native PROPERTIES FOLDER "plugins/carla")

if(NOT OS_MACOS)
  setup_plugin_target(carla-bridge-native)
endif()