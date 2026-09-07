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
  src/sim/dig_plan.cpp
  src/sim/grid.cpp
  src/sim/navigation.cpp
  src/sim/pheromones.cpp
  src/sim/rng.cpp
  src/sim/tasks.cpp
  src/sim/terrain_generation.cpp
  src/sim/world.cpp
)
target_include_directories(ant_sim PUBLIC src)
target_link_libraries(ant_sim PUBLIC EnTT::EnTT)
ant_configure_target(ant_sim)

add_library(ant_game
  src/game/policy.cpp
  src/game/prestige.cpp
  src/game/progression.cpp
  src/game/session.cpp
  src/game/view.cpp
)
target_include_directories(ant_game PUBLIC src)
target_link_libraries(ant_game PUBLIC ant_sim)
ant_configure_target(ant_game)

add_library(ant_persistence
  src/persistence/content_loader.cpp
  src/persistence/profile_codec.cpp
  src/persistence/profile_format.cpp
  src/persistence/platform_paths.cpp
  src/persistence/save_service.cpp
)
target_include_directories(ant_persistence PUBLIC src)
target_link_libraries(ant_persistence PUBLIC ant_game nlohmann_json::nlohmann_json)
ant_configure_target(ant_persistence)

add_executable(ant_headless src/headless/main.cpp)
target_link_libraries(ant_headless PRIVATE ant_game ant_persistence)
target_compile_definitions(ant_headless PRIVATE ANT_SOURCE_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
ant_configure_target(ant_headless)

if(ANT_BUILD_DESKTOP)
  add_library(ant_presentation
    src/presentation/camera.cpp
    src/presentation/desktop_input.cpp
    src/presentation/icons.cpp
    src/presentation/renderer.cpp
    src/presentation/ui_state.cpp
    src/presentation/interaction.cpp
  )
  target_include_directories(ant_presentation PUBLIC src)
  target_link_libraries(ant_presentation PUBLIC ant_game raylib PRIVATE glfw)
  target_compile_definitions(ant_presentation PRIVATE
    ANT_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
  )
  ant_configure_target(ant_presentation)

  add_executable(ant_farm
    src/app/asset_paths.cpp
    src/app/main.cpp
    src/app/save_coordinator.cpp
    src/app/window_size.cpp
  )
  target_link_libraries(ant_farm PRIVATE ant_presentation ant_persistence)
  target_compile_definitions(ant_farm PRIVATE ANT_SOURCE_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
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
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ant_farm>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/config/progression.json"
      "$<TARGET_FILE_DIR:ant_farm>/config/progression.json"
    VERBATIM
  )
endif()

if(BUILD_TESTING)
  add_executable(ant_tests
    src/app/asset_paths.cpp
    src/app/save_coordinator.cpp
    src/app/window_size.cpp
    src/presentation/ui_state.cpp
    src/presentation/interaction.cpp
    tests/unit/asset_paths_tests.cpp
    tests/unit/dig_plan_tests.cpp
    tests/unit/grid_tests.cpp
    tests/unit/maturity_tests.cpp
    tests/unit/policy_tests.cpp
    tests/unit/prestige_tests.cpp
    tests/unit/navigation_tests.cpp
    tests/unit/progression_tests.cpp
    tests/unit/rng_tests.cpp
    tests/unit/save_coordinator_tests.cpp
    tests/unit/save_service_tests.cpp
    tests/unit/snapshot_tests.cpp
    tests/unit/ui_state_tests.cpp
    tests/unit/interaction_tests.cpp
    tests/unit/task_system_tests.cpp
    tests/unit/window_size_tests.cpp
    tests/unit/world_tests.cpp
  )
  target_link_libraries(ant_tests PRIVATE ant_game ant_persistence Catch2::Catch2WithMain)
  target_compile_definitions(ant_tests PRIVATE
    ANT_SOURCE_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
  ant_configure_target(ant_tests)

  include(Catch)
  catch_discover_tests(ant_tests)
endif()
