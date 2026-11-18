cmake_minimum_required(VERSION 3.16)

project(KotonohaUnifiedLauncher VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

add_library(kotonoha_unified_launcher STATIC
    src/UnifiedLauncher.cpp
    src/GameFactories.cpp
)
target_include_directories(kotonoha_unified_launcher PUBLIC include)
target_link_libraries(kotonoha_unified_launcher PUBLIC KotonohaEngine)
target_compile_features(kotonoha_unified_launcher PUBLIC cxx_std_17)
set_target_properties(kotonoha_unified_launcher PROPERTIES CXX_EXTENSIONS OFF)

# Link actual game runtime libraries into the hub: no external port binary
# lookup and no executable entrypoints. A partial build only exposes factories
# for the game options selected during CMake configuration.
set(_kotonoha_launcher_libraries
    "EXAMPLE|kotonoha_demo"
    "AMAGAMI|amagami_port"
    "ZERO|zero_no_tsukaima"
    "SNOW|snow_radish_vacation"
    "SUMMER|summer_radish_vacation"
    "DDLC|doki_doki_literature_club"
    "SCHOOL_DAYS|school_days"
    "REALLIVE|reallive_port"
    "KIRIKIRI|kirikiri_formats"
    "LUCA_SYSTEM|luca_system_formats"
    "PMAIL|PmailPort"
    "N2SYSTEM|n2system_formats"
    "PONSCRIPTER|ponscripter_port"
)
foreach(_linked IN LISTS _kotonoha_launcher_libraries)
    string(REPLACE "|" ";" _fields "${_linked}")
    list(GET _fields 0 _game_option)
    list(GET _fields 1 _runtime_library)
    if(KOTONOHA_BUILD_${_game_option})
        if(NOT TARGET ${_runtime_library})
            message(FATAL_ERROR
                "[KotonohaLauncher] Enabled ${_game_option} has no library ${_runtime_library}")
        endif()
        target_link_libraries(kotonoha_unified_launcher PUBLIC ${_runtime_library})
        target_compile_definitions(kotonoha_unified_launcher
            PRIVATE KOTONOHA_LAUNCH_${_game_option}=1)
        message(STATUS "[KotonohaLauncher] Linked game runtime: ${_game_option}")
    endif()
endforeach()
unset(_kotonoha_launcher_libraries)

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
