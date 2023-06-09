# base config
if(MSVC)
  set(carla_lilv_compile_options /wd4005 /wd4090 /wd4133 /wd4244 /wd4267 /wd4273)
elseif(CLANG)
  set(carla_lilv_compile_options -Wno-error -Wno-deprecated-declarations -Wno-implicit-fallthrough
                                 -Wno-incompatible-pointer-types-discards-qualifiers -Wno-unused-parameter)
else()
  set(carla_lilv_compile_options
      -Wno-error
      -Wno-deprecated-declarations
      -Wno-discarded-qualifiers
      -Wno-format-overflow
      -Wno-implicit-fallthrough
      -Wno-maybe-uninitialized
      -Wno-unused-parameter)
endif()

set(carla_lilv_basedir carla/source/modules/lilv)

set(carla_lilv_include_directories carla/source/includes ${carla_lilv_basedir}/config)

if(NOT
   (OS_FREEBSD
    OR OS_MACOS
    OR OS_WINDOWS))
  set(carla_lilv_extra_libs dl m rt)
endif()

# serd
add_library(carla-lilv_serd STATIC)

set_target_properties(carla-lilv_serd PROPERTIES OSX_ARCHITECTURES "x86_64;arm64")

if(NOT OS_WINDOWS)
  set_property(TARGET carla-lilv_serd PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_options(carla-lilv_serd PRIVATE ${carla_lilv_compile_options})

target_include_directories(carla-lilv_serd PRIVATE ${carla_lilv_include_directories} ${carla_lilv_basedir}/serd-0.24.0)

target_sources(carla-lilv_serd PRIVATE ${carla_lilv_basedir}/serd.c)

# sord
add_library(carla-lilv_sord STATIC)

set_target_properties(carla-lilv_sord PROPERTIES OSX_ARCHITECTURES "x86_64;arm64")

if(NOT OS_WINDOWS)
  set_property(TARGET carla-lilv_sord PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_options(
  carla-lilv_sord
  PRIVATE ${carla_lilv_compile_options}
          # workaround compiler bug, see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=109585
          $<$<NOT:$<BOOL:${MSVC}>>:-fno-strict-aliasing>)

target_include_directories(carla-lilv_sord PRIVATE ${carla_lilv_include_directories} ${carla_lilv_basedir}/sord-0.16.0
                                                   ${carla_lilv_basedir}/sord-0.16.0/src)

target_link_libraries(carla-lilv_sord PRIVATE carla-lilv_serd)

target_sources(carla-lilv_sord PRIVATE ${carla_lilv_basedir}/sord.c)

# sratom
add_library(carla-lilv_sratom STATIC)

set_target_properties(carla-lilv_sratom PROPERTIES OSX_ARCHITECTURES "x86_64;arm64")

if(NOT OS_WINDOWS)
  set_property(TARGET carla-lilv_sratom PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_options(carla-lilv_sratom PRIVATE ${carla_lilv_compile_options})

target_include_directories(carla-lilv_sratom PRIVATE ${carla_lilv_include_directories}
                                                     ${carla_lilv_basedir}/sratom-0.6.0)

target_link_libraries(carla-lilv_sratom PRIVATE carla-lilv_serd)

target_sources(carla-lilv_sratom PRIVATE ${carla_lilv_basedir}/sratom.c)

# lilv
add_library(carla-lilv_lilv STATIC)

set_target_properties(carla-lilv_lilv PROPERTIES OSX_ARCHITECTURES "x86_64;arm64")

if(NOT OS_WINDOWS)
  set_property(TARGET carla-lilv_lilv PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_options(carla-lilv_lilv PRIVATE ${carla_lilv_compile_options})

target_include_directories(carla-lilv_lilv PRIVATE ${carla_lilv_include_directories} ${carla_lilv_basedir}/lilv-0.24.0
                                                   ${carla_lilv_basedir}/lilv-0.24.0/src)

target_link_libraries(carla-lilv_lilv PRIVATE carla-lilv_serd carla-lilv_sord carla-lilv_sratom)

target_sources(carla-lilv_lilv PRIVATE ${carla_lilv_basedir}/lilv.c)

# combined target
add_library(carla-lilv INTERFACE)
mark_as_advanced(carla-lilv)

target_link_libraries(carla-lilv INTERFACE carla-lilv_serd carla-lilv_sord carla-lilv_sratom carla-lilv_lilv
                                           ${carla_lilv_extra_libs})
