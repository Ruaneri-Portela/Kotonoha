cmake_minimum_required(VERSION 3.16)

# Kotonoha: select target platforms and game ports in the project root.
# Platform/toolchain setup must run before project() for Android/vcpkg.
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaGames.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaAndroid.cmake")

project(Kotonoha VERSION 0.1.0 LANGUAGES CXX)

# CMake 3.29+ deduplicates repeated static archives where the linker
# supports it (notably Apple's ld64). Earlier versions retain compatibility.
if(POLICY CMP0156)
    # Each game/module invokes cmake_minimum_required(VERSION 3.16), which
    # resets newer policies unless their default is inherited explicitly.
    set(CMAKE_POLICY_DEFAULT_CMP0156 NEW)
    cmake_policy(SET CMP0156 NEW)
endif()
if(POLICY CMP0179)
    set(CMAKE_POLICY_DEFAULT_CMP0179 NEW)
    cmake_policy(SET CMP0179 NEW)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/BuildVersion.cmake")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Web builds use JS-based Emscripten C++ EH with Asyncify. Enabling
# exception catching only for the final executable leaves engine and
# provider object files compiled without catch/unwind support, which
# can surface as an uncaught CppException after emscripten_sleep().
# Keep this before add_subdirectory(lib) and all game port targets.
if(EMSCRIPTEN)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fexceptions>")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/MSVC.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaOptions.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaTestGroups.cmake")

# School Days Windows L4 compatibility: retain the fork's opt-in diagnostics,
# shipped-player build gates and retail-only testing profile. These must be
# declared before add_subdirectory(lib) so engine and game targets agree.
option(KOTONOHA_DEV_CONTROL "Enable local School Days automation bridge" OFF)
option(KOTONOHA_DISTRIBUTION_BUILD
       "Produce a player-facing build without developer overlay, MCP or QA controls" OFF)
option(KOTONOHA_ENABLE_PLAYER_DUBBING
       "Include the standalone dubbing window in a distribution build" OFF)
option(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY
       "Build School Days HQ against the 29 original GPKs without LxH or translations" OFF)
if(KOTONOHA_DISTRIBUTION_BUILD AND KOTONOHA_DEV_CONTROL)
    message(FATAL_ERROR "Distribution builds must disable KOTONOHA_DEV_CONTROL")
endif()

add_subdirectory(lib)

# Imported vcpkg targets found inside lib/ are directory-scoped. Game ports
# are siblings of lib/, so resolve their SDL targets in the root scope too.
if(KOTONOHA_SDL_FROM_VCPKG)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_ttf CONFIG REQUIRED)
endif()

# Each port is configured only when explicitly selected (or BUILD_ALL).
set(_KOTONOHA_ENABLED_GAME_DETAILS "")
foreach(game IN LISTS KOTONOHA_GAMES)
    string(REPLACE "|" ";" fields "${game}")
    list(GET fields 0 game_option)
    list(GET fields 1 game_id)
    list(GET fields 2 game_directory)

    if(KOTONOHA_BUILD_${game_option} AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${game_directory}/CMakeLists.txt")
        list(APPEND _KOTONOHA_ENABLED_GAME_DETAILS "${game_id}|${game_directory}")
        add_subdirectory("${game_directory}")
    endif()
endforeach()

# The unified hub exists only for a full native desktop build.
# Individual port builds, including the Example demo, do not build a launcher.
if(KOTONOHA_BUILD_ALL AND NOT ANDROID AND NOT EMSCRIPTEN AND
   NOT (APPLE AND CMAKE_SYSTEM_NAME MATCHES "iOS|tvOS|watchOS") AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/launcher/CMakeLists.txt")
    add_subdirectory(launcher)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaReport.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaCPack.cmake")
