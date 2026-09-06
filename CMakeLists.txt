cmake_minimum_required(VERSION 3.25)

option(ANT_BUILD_DESKTOP "Build the raylib desktop application" ON)
option(ANT_ENABLE_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)

project(ant_farm_sim VERSION 0.1.0 LANGUAGES CXX)

if(APPLE AND ANT_BUILD_DESKTOP)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(cmake/Dependencies.cmake)

function(ant_configure_target target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()
  if(ANT_ENABLE_WARNINGS_AS_ERRORS)
    if(MSVC)
      target_compile_options(${target} PRIVATE /WX)
    else()
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

add_library(ant_sim
  src/sim/grid.cpp
  src/sim/navigation.cpp
  src/sim/rng.cpp
  src/sim/terrain_generation.cpp
  src/sim/world.cpp
)
target_include_directories(ant_sim PUBLIC src)
target_link_libraries(ant_sim PUBLIC EnTT::EnTT)
ant_configure_target(ant_sim)

add_library(ant_game
  src/game/session.cpp
  src/game/view.cpp
)
target_include_directories(ant_game PUBLIC src)
target_link_libraries(ant_game PUBLIC ant_sim)
ant_configure_target(ant_game)

add_library(ant_persistence src/persistence/profile_format.cpp)
target_include_directories(ant_persistence PUBLIC src)
target_link_libraries(ant_persistence PUBLIC ant_game nlohmann_json::nlohmann_json)
ant_configure_target(ant_persistence)

add_executable(ant_headless src/headless/main.cpp)
target_link_libraries(ant_headless PRIVATE ant_game ant_persistence)
ant_configure_target(ant_headless)

if(ANT_BUILD_DESKTOP)
  add_library(ant_presentation
    src/presentation/camera.cpp
    src/presentation/renderer.cpp
  )
  target_include_directories(ant_presentation PUBLIC src)
  target_link_libraries(ant_presentation PUBLIC ant_game raylib)
  target_compile_definitions(ant_presentation PRIVATE
    ANT_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
  )
  ant_configure_target(ant_presentation)

  add_executable(ant_farm
    src/app/main.cpp
    src/app/window_size.cpp
  )
  target_link_libraries(ant_farm PRIVATE ant_presentation ant_persistence)
  if(APPLE)
    target_sources(ant_farm PRIVATE src/app/macos_window.mm)
    target_link_libraries(ant_farm PRIVATE "-framework AppKit")
  endif()
  ant_configure_target(ant_farm)
  add_custom_command(TARGET ant_farm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ant_farm>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Nunito-SemiBold.ttf"
      "$<TARGET_FILE_DIR:ant_farm>/assets/fonts/Nunito-SemiBold.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL.txt"
      "$<TARGET_FILE_DIR:ant_farm>/assets/fonts/OFL.txt"
    VERBATIM
  )
endif()

if(BUILD_TESTING)
  add_executable(ant_tests
    src/app/window_size.cpp
    tests/unit/grid_tests.cpp
    tests/unit/navigation_tests.cpp
    tests/unit/rng_tests.cpp
    tests/unit/window_size_tests.cpp
    tests/unit/world_tests.cpp
  )
  target_link_libraries(ant_tests PRIVATE ant_game Catch2::Catch2WithMain)
  ant_configure_target(ant_tests)

  include(Catch)
  catch_discover_tests(ant_tests)
endif()
