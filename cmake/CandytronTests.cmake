# The test runner (tests/test_main.c + tests/suites/*.c) and its registration with CTest. Included from the top-level
# CMakeLists.txt when CANDYTRON_BUILD_TESTS is ON.
#
#   ./candytron_tests        runs every suite, a few at a time, and prints one line per suite
#   ctest                    the same suites, one CTest test each (tests/suites.def is the single list)
#   cmake --build . --target check
#
# The group "assets": the production data is the production's (size, SHA-256 by two independent implementations, structure), and the
# player refuses data that is not.

set(CANDYTRON_TEST_DIR ${PROJECT_SOURCE_DIR}/tests)
set(CANDYTRON_TEST_WORK_DIR ${PROJECT_BINARY_DIR}/tests/work)
file(MAKE_DIRECTORY ${CANDYTRON_TEST_WORK_DIR})

file(GLOB _suite_sources CONFIGURE_DEPENDS ${CANDYTRON_TEST_DIR}/suites/*.c)

add_executable(candytron_tests ${CANDYTRON_TEST_DIR}/test_main.c ${_suite_sources})
target_include_directories(candytron_tests PRIVATE ${CANDYTRON_TEST_DIR} ${PROJECT_BINARY_DIR}/generated)
target_link_libraries(candytron_tests PRIVATE candytron::headers)
candytron_configure_target(candytron_tests)
set_target_properties(candytron_tests PROPERTIES FOLDER tests)

# Every suite gets its own copy of the libraries (static functions), so that white-box suites can use the internals and no two
# suites share state.
target_compile_definitions(candytron_tests PRIVATE
  CANDYTRON_TEST_DATA_DIR="${CANDYTRON_TEST_DATA_DIR}"
  CANDYTRON_ASSETS_DIR="${CANDYTRON_ASSETS_DIR}"
  CANDYTRON_FIXTURES_DIR="${CANDYTRON_FIXTURES_DIR}"
  CANDYTRON_TEST_WORK_DIR="${CANDYTRON_TEST_WORK_DIR}"
  CT_SHA256_STATIC CT_ASSETS_STATIC CT_GL_STATIC
  PLAYER_ARGS_STATIC PLAYER_CREDITS_STATIC PLAYER_GLCHECK_STATIC PLAYER_VIEW_STATIC)
if(NOT MSVC)
  # a suite includes only what it needs of the libraries, so some static functions are unused in it
  target_compile_options(candytron_tests PRIVATE -Wno-unused-function)
  target_compile_definitions(candytron_tests PRIVATE _POSIX_C_SOURCE=200809L)
endif()
if(EMSCRIPTEN)
  # the preset web-tests: the runner is a WebAssembly module that Node runs (CTest starts it through CMAKE_CROSSCOMPILING_EMULATOR); NODERAWFS lets it read the production data
  # and the fixtures from the host's file system by the absolute paths above
  set_target_properties(candytron_tests PROPERTIES SUFFIX ".js")
  target_link_options(candytron_tests PRIVATE -sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -sSTACK_SIZE=4MB -sEXIT_RUNTIME=1)
endif()
if(CANDYTRON_SANITIZE)
  target_compile_definitions(candytron_tests PRIVATE CANDYTRON_TEST_LSAN_SUPP="${CANDYTRON_TEST_DIR}/lsan.supp" CANDYTRON_TEST_SLOW_BUILD)
endif()
if(CANDYTRON_PLAYER_AVAILABLE)
  target_compile_definitions(candytron_tests PRIVATE CANDYTRON_PLAYER_EXE="$<TARGET_FILE:candytron>")
  add_dependencies(candytron_tests candytron)
endif()

add_custom_target(check COMMAND candytron_tests DEPENDS candytron_tests USES_TERMINAL
  COMMENT "Running every test suite")

# ---- CTest: one test per entry of suites.def ---------------------------------------------------------------------------------------
file(STRINGS ${CANDYTRON_TEST_DIR}/suites.def _suite_lines REGEX "^SUITE\\(")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CANDYTRON_TEST_DIR}/suites.def)
set(_timeout_scale 1)
if(CANDYTRON_SANITIZE)
  set(_timeout_scale 4)
endif()
foreach(_line IN LISTS _suite_lines)
  if(NOT _line MATCHES "^SUITE\\(\"([a-z0-9_]+)\", *[a-z0-9_]+, *([0-9]+), *([A-Z_0-9]+)")
    message(FATAL_ERROR "tests/suites.def: cannot read the entry: ${_line}")
  endif()
  set(_name ${CMAKE_MATCH_1})
  set(_limit ${CMAKE_MATCH_2})
  set(_flags ${CMAKE_MATCH_3})
  math(EXPR _limit "${_limit} * ${_timeout_scale} + 60")
  add_test(NAME ${_name} COMMAND candytron_tests ${_name})
  set(_labels unit)
  if(_line MATCHES "suite_player_cli")
    set(_labels player)
  endif()
  if(_name MATCHES "^assets")
    set(_labels "unit;assets")
  endif()
  set_tests_properties(${_name} PROPERTIES
    TIMEOUT ${_limit} LABELS "${_labels}" SKIP_RETURN_CODE 77
    ENVIRONMENT "CANDYTRON_TEST_TIMEOUT_SCALE=${_timeout_scale}")
  if(_flags MATCHES "SUITE_SERIAL")
    set_tests_properties(${_name} PROPERTIES RUN_SERIAL TRUE)
  endif()
endforeach()

# ---- static checks on the sources and the data -------------------------------------------------------------------------------------
# no mutable global state in the headers (the libraries are reentrant: every instance owns its state)
file(GLOB_RECURSE _library_headers CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/include/candytron/core/*.h)
list(JOIN _library_headers "|" _library_headers_arg)
add_test(NAME headers_no_mutable_statics
  COMMAND ${CMAKE_COMMAND} "-DHEADERS=${_library_headers_arg}" -P ${PROJECT_SOURCE_DIR}/cmake/scripts/check_no_mutable_statics.cmake)
set_tests_properties(headers_no_mutable_statics PROPERTIES LABELS unit)

# the SHA-256 of the data by CMake's own implementation: an independent check of both the files and of sha256.h
add_test(NAME assets_sha256
  COMMAND ${CMAKE_COMMAND} -DDIR=${CANDYTRON_ASSETS_DIR} -DSUMS=${CANDYTRON_FIXTURES_DIR}/assets.sha256
          -P ${PROJECT_SOURCE_DIR}/cmake/scripts/check_assets_sha256.cmake)
set_tests_properties(assets_sha256 PROPERTIES LABELS "unit;assets")
