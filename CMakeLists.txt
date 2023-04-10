project(carla)

find_qt(COMPONENTS Widgets)

add_library(carla MODULE)
add_library(OBS::carla ALIAS carla)

target_compile_definitions(carla PRIVATE
  CARLA_LIB_EXT=".so"
  CARLA_PLUGIN_BUILD
  REAL_BUILD
)

target_compile_options(carla PRIVATE
  -Wno-error
)

target_include_directories(carla PRIVATE
  carla/source
  carla/source/backend
  carla/source/backend/engine
  carla/source/includes
  carla/source/modules
  carla/source/utils
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
# ysfx

add_library(carla_ysfx STATIC)
add_library(OBS::carla_ysfx ALIAS carla_ysfx)

target_compile_definitions(carla_ysfx PRIVATE
  EEL_TARGET_PORTABLE= # FIXME
  EELSCRIPT_NO_NET=
  EELSCRIPT_NO_LICE=
  NSEEL_ATOF=ysfx_wdl_atof
  WDL_FFT_REALSIZE=8
  WDL_LINEPARSE_ATOF=ysfx_wdl_atof
  YSFX_API=
  YSFX_NO_GFX=
  YSFX_NO_STANDARD_MUTEX=
)

target_compile_options(carla_ysfx PRIVATE
  -fPIC
  -fsigned-char
  -Wno-error
  # FIXME
  -Wno-sign-compare
  -Wno-unused-function
  -Wno-unused-parameter
)

target_include_directories(carla_ysfx PRIVATE
  carla/source/includes
  carla/source/modules/ysfx/include
  carla/source/modules/ysfx/sources
  carla/source/modules/ysfx/thirdparty/WDL/source
  carla/source/modules/ysfx/thirdparty/dr_libs
  carla/source/modules/ysfx/thirdparty/stb
)

target_sources(carla_ysfx PRIVATE
  carla/source/modules/ysfx/sources/ysfx.cpp
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
  #carla/source/modules/ysfx/thirdparty/WDL/source/WDL/win32_utf8.c
)

###############################################################################

target_sources(carla PRIVATE
  carla.cpp
  carla/source/backend/engine/CarlaEngine.cpp
  carla/source/backend/engine/CarlaEngineClient.cpp
  carla/source/backend/engine/CarlaEngineData.cpp
  carla/source/backend/engine/CarlaEngineDummy.cpp
  carla/source/backend/engine/CarlaEngineGraph.cpp
  carla/source/backend/engine/CarlaEngineInternal.cpp
  carla/source/backend/engine/CarlaEngineNative.cpp
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
  carla/source/plugin/carla-host-plugin.cpp
)

#HAVE_PYQT
#bigmeter.cpp
#carla/source/native-plugins/midi-pattern.cpp
#notes.cpp
#xycontroller.cpp

target_link_libraries(carla PRIVATE
  OBS::carla_lilv
  OBS::carla_ysfx
  OBS::libobs
  Qt::Widgets)

set_target_properties(image-source PROPERTIES
  FOLDER "plugins"
  PROJECT_LABEL "Carla Plugin Host"
)

setup_plugin_target(carla)
