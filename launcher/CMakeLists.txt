cmake_minimum_required(VERSION 3.16)

project(KotonohaUnifiedLauncher VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

if(NOT KOTONOHA_BUILD_ALL AND NOT KOTONOHA_BUILD_LAUNCHER)
    message(FATAL_ERROR
        "[KotonohaLauncher] Enable KOTONOHA_BUILD_ALL=ON or KOTONOHA_BUILD_LAUNCHER=ON to build the unified launcher")
endif()

add_library(kotonoha_unified_launcher STATIC
    src/UnifiedLauncher.cpp
    src/GameFactories.cpp
)
if(NOT ANDROID)
    target_sources(kotonoha_unified_launcher PRIVATE src/LauncherBridge.cpp)
endif()
target_include_directories(kotonoha_unified_launcher PUBLIC include)
target_link_libraries(kotonoha_unified_launcher PUBLIC KotonohaEngine)
target_compile_features(kotonoha_unified_launcher PUBLIC cxx_std_17)
set_target_properties(kotonoha_unified_launcher PROPERTIES CXX_EXTENSIONS OFF)

# Link every real game runtime, but never the Example demo.
# The hub is only available when KOTONOHA_BUILD_ALL=ON.
set(_kotonoha_launcher_libraries
    "AMAGAMI|amagami_port"
    "ZERO|zero_no_tsukaima"
    "SNOW|snow_radish_vacation"
    "SUMMER|summer_radish_vacation"
    "MEO|summer_radish_vacation"
    "DDLC|doki_doki_literature_club"
    "KATAWA|doki_doki_literature_club"
    "SCHOOL_DAYS|school_days"
    "REALLIVE|reallive_port"
    "KIRIKIRI|kirikiri_formats"
    "LUCA_SYSTEM|luca_system_formats"
    "PMAIL|PmailPort"
    "N2SYSTEM|n2system_formats"
    "PONSCRIPTER|ponscripter_port"
    "ARTEMIS|artemis_port"
)
foreach(_linked IN LISTS _kotonoha_launcher_libraries)
    string(REPLACE "|" ";" _fields "${_linked}")
    list(GET _fields 0 _game_option)
    list(GET _fields 1 _runtime_library)
    if(NOT KOTONOHA_BUILD_${_game_option} OR NOT TARGET ${_runtime_library})
        message(FATAL_ERROR
            "[KotonohaLauncher] ALL build is missing the ${_game_option} runtime library ${_runtime_library}")
    endif()
    target_link_libraries(kotonoha_unified_launcher PUBLIC ${_runtime_library})
    target_compile_definitions(kotonoha_unified_launcher
        PUBLIC KOTONOHA_LAUNCH_${_game_option}=1)
    message(STATUS "[KotonohaLauncher] Linked game runtime: ${_game_option}")
endforeach()
unset(_kotonoha_launcher_libraries)

kotonoha_add_app(KotonohaLauncher src/main.cpp)
if(ANDROID)
    # JNI entry points are called from Java, so a static linker cannot see
    # references to them when LauncherBridge.cpp lives only in an archive.
    target_sources(KotonohaLauncher PRIVATE src/LauncherBridge.cpp)
endif()
if(APPLE AND (IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS"))
    target_sources(KotonohaLauncher PRIVATE src/AppleDocuments.mm)
    target_link_libraries(KotonohaLauncher PRIVATE "-framework Foundation")
endif()
target_link_libraries(KotonohaLauncher PRIVATE kotonoha_unified_launcher)
set_target_properties(KotonohaLauncher PROPERTIES CXX_EXTENSIONS OFF)
# Keep the global engine resources, but do not copy the Example demo data.
# An empty game-assets directory preserves the shared bundle layout contract.
set(_kotonoha_launcher_empty_assets "${CMAKE_CURRENT_BINARY_DIR}/empty-game-assets")
file(MAKE_DIRECTORY "${_kotonoha_launcher_empty_assets}")
# The unified hub links DDLC as a library. A macOS .app must therefore
# bundle the Python standard library just as the standalone DDLC app does.
# Without this, a packaged hub may fail during startup or when DDLC loads.
if(APPLE AND KOTONOHA_BUILD_MACOS_BUNDLES AND KOTONOHA_BUILD_DDLC
   AND NOT KOTONOHA_USE_PYTHON_STUBS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
    kotonoha_configure_app(KotonohaLauncher
        BUNDLE_ID me.hirameki.kotonoha
        GAME launcher
        GAME_ASSET_SOURCE "${_kotonoha_launcher_empty_assets}"
        PYTHON_STDLIB "${KOTONOHA_RENPY_PYTHON_STDLIB}"
        PYTHON_VERSION "${KOTONOHA_RENPY_PYTHON_VERSION}")
else()
    kotonoha_configure_app(KotonohaLauncher
        BUNDLE_ID me.hirameki.kotonoha
        GAME launcher
        GAME_ASSET_SOURCE "${_kotonoha_launcher_empty_assets}")
endif()

# School Days uses a tracked, non-commercial route definition. Ship this
# together with the in-process hub instead of assuming the individual game's
# standalone output directory is available at runtime.
if(KOTONOHA_BUILD_SCHOOL_DAYS)
    kotonoha_preload_asset(KotonohaLauncher
        "${KOTONOHA_PROJECT_ROOT}/games/school-days/game-assets/school-days-hq.ktnroute"
        "/game-assets/school-days-hq.ktnroute")
endif()

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
