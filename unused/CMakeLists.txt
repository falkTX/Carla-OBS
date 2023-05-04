project(carla)

find_qt(COMPONENTS Core Gui Widgets)

find_package(PkgConfig)

# optional: used for plugin UIs on freedesktop systems
if(UNIX AND NOT APPLE)
  pkg_check_modules(X11 "x11")
endif()

# optional: finds correct plugin binary type
if(NOT WIN32)
  pkg_check_modules(LIBMAGIC "libmagic")
endif()

# optional: used for DSSI plugin UIs
pkg_check_modules(LIBLO "liblo")

# optional: extra audio file support
pkg_check_modules(SNDFILE "sndfile")

add_library(carla MODULE)
add_library(OBS::carla ALIAS carla)

target_compile_definitions(
  carla
  PRIVATE CARLA_BACKEND_NAMESPACE=CarlaOBS
          CARLA_ENGINE_WITHOUT_UI
          CARLA_LIB_EXT=".so"
          CARLA_PLUGIN_BUILD
          REAL_BUILD
          STATIC_PLUGIN_TARGET
          $<${LIBLO_FOUND}:HAVE_LIBLO>
          $<${LIBMAGIC_FOUND}:HAVE_LIBMAGIC>
          $<${SNDFILE_FOUND}:HAVE_SNDFILE>
          $<${X11_FOUND}:HAVE_X11>)

target_compile_options(carla PRIVATE -Wno-error -Wno-vla)

target_include_directories(
  carla
  PRIVATE carla/source
          carla/source/backend
          carla/source/backend/engine
          carla/source/frontend
          carla/source/frontend/pluginlist
          carla/source/frontend/utils
          carla/source/includes
          carla/source/modules
          carla/source/utils
          ${LIBLO_INCLUDE_DIRS}
          ${LIBMAGIC_INCLUDE_DIRS}
          ${SNDFILE_INCLUDE_DIRS}
          ${X11_INCLUDE_DIRS})

target_link_options(carla PRIVATE -Wl,--no-undefined)

# ######################################################################################################################
# ysfx

add_library(carla_ysfx STATIC)
add_library(OBS::carla_ysfx ALIAS carla_ysfx)

target_compile_definitions(
  carla_ysfx
  PRIVATE EEL_TARGET_PORTABLE= # FIXME
          EELSCRIPT_NO_NET=
          EELSCRIPT_NO_LICE=
          NSEEL_ATOF=ysfx_wdl_atof
          WDL_FFT_REALSIZE=8
          WDL_LINEPARSE_ATOF=ysfx_wdl_atof
          YSFX_API=
          YSFX_NO_GFX=
          YSFX_NO_STANDARD_MUTEX=)

target_compile_options(
  carla_ysfx
  PRIVATE -fPIC -fsigned-char -Wno-error
          # FIXME
          -Wno-sign-compare -Wno-unused-function -Wno-unused-parameter)

target_include_directories(
  carla_ysfx
  PRIVATE carla/source/includes carla/source/modules/ysfx/include carla/source/modules/ysfx/sources
          carla/source/modules/ysfx/thirdparty/WDL/source carla/source/modules/ysfx/thirdparty/dr_libs
          carla/source/modules/ysfx/thirdparty/stb)

target_sources(
  carla_ysfx
  PRIVATE carla/source/modules/ysfx/sources/ysfx.cpp
          carla/source/modules/ysfx/sources/ysfx_api_eel.cpp
          carla/source/modules/ysfx/sources/ysfx_api_file.cpp
          carla/source/modules/ysfx/sources/ysfx_api_gfx.cpp
          carla/source/modules/ysfx/sources/ysfx_api_reaper.cpp
          carla/source/modules/ysfx/sources/ysfx_audio_flac.cpp
          carla/source/modules/ysfx/sources/ysfx_audio_wav.cpp
          carla/source/modules/ysfx/sources/ysfx_config.cpp
          carla/source/modules/ysfx/sources/ysfx_eel_utils.cpp
          carla/source/modules/ysfx/sources/ysfx_midi.cpp
          carla/source/modules/ysfx/sources/ysfx_parse.cpp
          carla/source/modules/ysfx/sources/ysfx_reader.cpp
          carla/source/modules/ysfx/sources/ysfx_utils.cpp
          carla/source/modules/ysfx/sources/ysfx_utils_fts.cpp
          carla/source/modules/ysfx/sources/eel2-gas/sources/asm-nseel-x64-sse.S
          carla/source/modules/ysfx/thirdparty/WDL/source/WDL/eel2/nseel-caltab.c
          carla/source/modules/ysfx/thirdparty/WDL/source/WDL/eel2/nseel-cfunc.c
          carla/source/modules/ysfx/thirdparty/WDL/source/WDL/eel2/nseel-compiler.c
          carla/source/modules/ysfx/thirdparty/WDL/source/WDL/eel2/nseel-eval.c
          carla/source/modules/ysfx/thirdparty/WDL/source/WDL/eel2/nseel-lextab.c
          carla/source/modules/ysfx/thirdparty/WDL/source/WDL/eel2/nseel-ram.c
          carla/source/modules/ysfx/thirdparty/WDL/source/WDL/eel2/nseel-yylex.c
          carla/source/modules/ysfx/thirdparty/WDL/source/WDL/fft.c
          # carla/source/modules/ysfx/thirdparty/WDL/source/WDL/win32_utf8.c
)

# ######################################################################################################################

target_sources(
  carla
  PRIVATE carla.c
          carla-wrapper.c
          qtutils.cpp
          carla/source/backend/engine/CarlaEngine.cpp
          carla/source/backend/engine/CarlaEngineClient.cpp
          carla/source/backend/engine/CarlaEngineData.cpp
          carla/source/backend/engine/CarlaEngineDummy.cpp
          carla/source/backend/engine/CarlaEngineGraph.cpp
          carla/source/backend/engine/CarlaEngineInternal.cpp
          carla/source/backend/engine/CarlaEngineNative.cpp
          carla/source/backend/engine/CarlaEngineOsc.cpp
          carla/source/backend/engine/CarlaEngineOscHandlers.cpp
          carla/source/backend/engine/CarlaEngineOscSend.cpp
          carla/source/backend/engine/CarlaEnginePorts.cpp
          carla/source/backend/engine/CarlaEngineRunner.cpp
          carla/source/backend/plugin/CarlaPlugin.cpp
          carla/source/backend/plugin/CarlaPluginAU.cpp
          carla/source/backend/plugin/CarlaPluginBridge.cpp
          carla/source/backend/plugin/CarlaPluginCLAP.cpp
          carla/source/backend/plugin/CarlaPluginFluidSynth.cpp
          carla/source/backend/plugin/CarlaPluginInternal.cpp
          carla/source/backend/plugin/CarlaPluginJack.cpp
          carla/source/backend/plugin/CarlaPluginJSFX.cpp
          carla/source/backend/plugin/CarlaPluginJuce.cpp
          carla/source/backend/plugin/CarlaPluginLADSPADSSI.cpp
          carla/source/backend/plugin/CarlaPluginLV2.cpp
          carla/source/backend/plugin/CarlaPluginNative.cpp
          carla/source/backend/plugin/CarlaPluginSFZero.cpp
          carla/source/backend/plugin/CarlaPluginVST2.cpp
          carla/source/backend/plugin/CarlaPluginVST3.cpp
          carla/source/frontend/carla_frontend.cpp
          carla/source/frontend/pluginlist/pluginlistdialog.cpp
          carla/source/frontend/pluginlist/pluginlistrefreshdialog.cpp
          carla/source/jackbridge/JackBridge1.cpp
          carla/source/jackbridge/JackBridge2.cpp
          carla/source/modules/audio_decoder/ad_dr_mp3.c
          carla/source/modules/audio_decoder/ad_ffmpeg.c
          carla/source/modules/audio_decoder/ad_plugin.c
          carla/source/modules/audio_decoder/ad_soundfile.c
          carla/source/modules/sfzero/SFZero.cpp
          carla/source/modules/rtmempool/rtmempool.c
          carla/source/modules/water/water.cpp
          carla/source/modules/zita-resampler/cresampler.cc
          carla/source/modules/zita-resampler/resampler-table.cc
          carla/source/modules/zita-resampler/resampler.cc
          carla/source/modules/zita-resampler/vresampler.cc
          carla/source/native-plugins/_all.c
          carla/source/native-plugins/_data.cpp
          carla/source/native-plugins/audio-file.cpp
          carla/source/native-plugins/audio-gain.c
          carla/source/native-plugins/bypass.c
          carla/source/native-plugins/cv-to-audio.c
          carla/source/native-plugins/lfo.c
          carla/source/native-plugins/midi-channel-ab.c
          carla/source/native-plugins/midi-channel-filter.c
          carla/source/native-plugins/midi-channelize.c
          carla/source/native-plugins/midi-file.cpp
          carla/source/native-plugins/midi-gain.c
          carla/source/native-plugins/midi-join.c
          carla/source/native-plugins/midi-split.c
          carla/source/native-plugins/midi-through.c
          carla/source/native-plugins/midi-to-cv.c
          carla/source/native-plugins/midi-transpose.c
          carla/source/plugin/carla-host-plugin.cpp)

# HAVE_PYQT bigmeter.cpp carla/source/native-plugins/midi-pattern.cpp notes.cpp xycontroller.cpp

target_link_directories(carla PRIVATE ${LIBLO_LIBRARY_DIRS} ${LIBMAGIC_LIBRARY_DIRS} ${SNDFILE_LIBRARY_DIRS}
                        ${X11_LIBRARY_DIRS})

target_link_libraries(
  carla
  PRIVATE OBS::carla_lilv
          OBS::carla_ysfx
          OBS::libobs
          OBS::frontend-api
          Qt::Core
          Qt::Gui
          Qt::Widgets
          ${LIBLO_LIBRARIES}
          ${LIBMAGIC_LIBRARIES}
          ${SNDFILE_LIBRARIES}
          ${X11_LIBRARIES})

set_target_properties(
  carla
  PROPERTIES AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON
             FOLDER "plugins"
             PROJECT_LABEL "Carla Plugin Host")

setup_plugin_target(carla)
