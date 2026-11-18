cmake_minimum_required(VERSION 3.16)

project(KotonohaUnifiedLauncher VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

add_library(kotonoha_unified_launcher STATIC
    src/UnifiedLauncher.cpp
)
target_include_directories(kotonoha_unified_launcher PUBLIC include)
target_link_libraries(kotonoha_unified_launcher PUBLIC KotonohaEngine)
target_compile_features(kotonoha_unified_launcher PUBLIC cxx_std_17)
set_target_properties(kotonoha_unified_launcher PROPERTIES CXX_EXTENSIONS OFF)

kotonoha_add_app(KotonohaLauncher src/main.cpp)
target_link_libraries(KotonohaLauncher PRIVATE kotonoha_unified_launcher)
set_target_properties(KotonohaLauncher PROPERTIES CXX_EXTENSIONS OFF)
kotonoha_configure_app(KotonohaLauncher
    BUNDLE_ID me.hirameki.kotonoha.launcher
    GAME example
    GAME_ASSET_SOURCE "${KOTONOHA_PROJECT_ROOT}/games/example/game-assets")

if(BUILD_TESTING)
    add_executable(KotonohaUnifiedLauncherTests tests/UnifiedLauncherTests.cpp)
    target_link_libraries(KotonohaUnifiedLauncherTests PRIVATE kotonoha_unified_launcher)
    target_compile_features(KotonohaUnifiedLauncherTests PRIVATE cxx_std_17)
    set_target_properties(KotonohaUnifiedLauncherTests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME Kotonoha.UnifiedLauncher COMMAND KotonohaUnifiedLauncherTests)
    set_tests_properties(Kotonoha.UnifiedLauncher PROPERTIES
        LABELS "synthetic;fast;host"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()
