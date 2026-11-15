# The candytron executable. Included from the top-level CMakeLists.txt when CANDYTRON_BUILD_PLAYER is ON.

include(CandytronSDL3)

candytron_find_sdl3(_have_sdl3)
if(NOT _have_sdl3)
  return()
endif()
set(CANDYTRON_PLAYER_AVAILABLE ON)

add_executable(candytron src/player/main.c)
target_include_directories(candytron PRIVATE ${PROJECT_BINARY_DIR}/generated)
target_link_libraries(candytron PRIVATE candytron::headers SDL3::SDL3)
candytron_configure_target(candytron)
if(NOT MSVC)
  # the player includes the whole of the single-header libraries as static functions and calls what it needs
  target_compile_options(candytron PRIVATE -Wno-unused-function)
endif()

if(EMSCRIPTEN)
  include(CandytronWeb)
  candytron_configure_web(candytron)
  return()
endif()

# the data the player looks for next to the executable
add_custom_command(TARGET candytron POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:candytron>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CANDYTRON_PLAYER_DATA} $<TARGET_FILE_DIR:candytron>/assets)

include(GNUInstallDirs)
install(TARGETS candytron RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CANDYTRON_PLAYER_DATA}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/candytron)
