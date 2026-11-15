cmake_minimum_required(VERSION 3.25)

project(candytron
  VERSION 0.1.0
  DESCRIPTION "Candytron (Farbrausch fr-030) native player: C99 libraries, SDL3 and OpenGL ES 3"
  LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(EMSCRIPTEN)
  set(CMAKE_C_EXTENSIONS ON) # EM_ASM, and SDL3's own use of it, do not compile in -std=c99
endif()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---- data --------------------------------------------------------------------------------------------------------
# assets/ holds the production data (default.pac, josie.v2m, babe.mesh.old; CC BY 3.0, see assets/ATTRIBUTION.md and doc/REFERENCE.md);
# tests/data holds the reference fixtures of the test suites (tests/data/candytron those derived from the production data).
set(CANDYTRON_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(CANDYTRON_TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/data)
set(CANDYTRON_FIXTURES_DIR ${CANDYTRON_TEST_DATA_DIR}/candytron)
# What the player loads: copied next to the executable, installed and embedded in the web build.
set(CANDYTRON_PLAYER_DATA ${CANDYTRON_ASSETS_DIR}/default.pac ${CANDYTRON_ASSETS_DIR}/josie.v2m ${CANDYTRON_ASSETS_DIR}/babe.mesh.old)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- options -----------------------------------------------------------------------------------------------------------------
set(_is_root OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_is_root ON)
endif()
# the tests are native programs; the browser build is the player only
set(_native_default ${_is_root})
if(EMSCRIPTEN)
  set(_native_default OFF)
endif()

option(CANDYTRON_BUILD_PLAYER "Build the SDL3 player (candytron)" ON)
option(CANDYTRON_BUILD_TESTS "Build the test runner (candytron_tests) and register it with CTest" ${_native_default})
option(CANDYTRON_FETCH_SDL3 "Fetch and build SDL3 when no installed SDL3 >= 3.2 is found" ON)
set(CANDYTRON_SDL3_TAG "release-3.4.8" CACHE STRING "SDL3 tag used when it is fetched")
set(CANDYTRON_WEB_MAX_MEMORY "1GB" CACHE STRING "Emscripten builds: the largest the WebAssembly memory may grow to")
option(CANDYTRON_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

include(CandytronCompile)

# ---- libraries -----------------------------------------------------------------------------------------------------------------
# The libraries are single headers in the style of stb: define <NAME>_IMPLEMENTATION in exactly one translation unit
# (see doc/ARCHITECTURE.md). They only carry usage requirements. The demo's results are compared against the original's, so every
# consumer is compiled without floating-point contraction.
add_library(candytron_headers INTERFACE)
add_library(candytron::headers ALIAS candytron_headers)
target_include_directories(candytron_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(candytron_headers INTERFACE c_std_99)
if(MSVC)
  target_compile_options(candytron_headers INTERFACE /fp:precise)
else()
  # the script's floating point runs in the original's rounding mode (round downward, see csl_natives.h): the compiler must not fold or reorder across it
  target_compile_options(candytron_headers INTERFACE -ffp-contract=off)
  if(NOT EMSCRIPTEN) # WebAssembly has only round-to-nearest, and the state test shows that the script does not need the other mode
    target_compile_options(candytron_headers INTERFACE -frounding-math)
  endif()
  target_link_libraries(candytron_headers INTERFACE m)
endif()

# ---- licenses ----------------------------------------------------------------------------------------------------------------
include(EmbedLicenses)
# The notices of everything the player is built from, embedded in the executable (candytron --licenses). The header is
# generated into the build tree; the player and its tests include it.
set(_licenses ${PROJECT_SOURCE_DIR}/third_party/licenses)
candytron_embed_licenses(${PROJECT_BINARY_DIR}/generated/licenses_data.h
  "GenThree engine (Farbrausch fr_public, genthree), BSD license" ${_licenses}/genthree-LICENSE.txt
  "fr-030 Candytron production data and music, Creative Commons Attribution 3.0" ${PROJECT_SOURCE_DIR}/assets/ATTRIBUTION.md
  "Liberation Sans (the baked coverage of the text of the textures), SIL Open Font License 1.1" ${_licenses}/liberation-fonts-LICENSE.txt
  "V2 synthesizer (kb's V2 as ported in v2redux), public domain, CC0 1.0" ${_licenses}/v2redux-LICENSE.txt
  "SDL3, zlib license" ${_licenses}/SDL3-NOTICE.txt)

# ---- player --------------------------------------------------------------------------------------------------------------------
set(CANDYTRON_PLAYER_AVAILABLE OFF)
if(CANDYTRON_BUILD_PLAYER)
  include(CandytronPlayer)
endif()

# ---- tests ---------------------------------------------------------------------------------------------------------------------
if(CANDYTRON_BUILD_TESTS)
  enable_testing()
  include(CandytronTests)
endif()

# ---- formatting ----------------------------------------------------------------------------------------------------------------
# `cmake --build <dir> --target format` applies .clang-format; `format-check` fails when a file does not conform.
find_program(CANDYTRON_CLANG_FORMAT clang-format)
if(CANDYTRON_CLANG_FORMAT AND _is_root)
  file(GLOB_RECURSE _formatted CONFIGURE_DEPENDS include/candytron/*.h src/*.c src/*.h tests/*.c tests/*.h)
  add_custom_target(format COMMAND ${CANDYTRON_CLANG_FORMAT} -i ${_formatted}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} COMMENT "Formatting the sources" VERBATIM)
  add_custom_target(format-check COMMAND ${CANDYTRON_CLANG_FORMAT} --dry-run -Werror ${_formatted}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} COMMENT "Checking the formatting of the sources" VERBATIM)
endif()

# ---- install -------------------------------------------------------------------------------------------------------------------
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h")
