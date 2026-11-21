cmake_minimum_required(VERSION 3.16)

# Kotonoha: select target platforms and game ports in the project root.
# Platform/toolchain setup must run before project() for Android/vcpkg.
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/games/KotonohaGames.cmake")
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

include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaOptions.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaTestGroups.cmake")

# Player distributions exclude developer CLI controls in every game port.
# Also disable the MCP backend without changing the cached DEV preference.
option(KOTONOHA_DISTRIBUTION_BUILD
    "Build a player distribution without developer command-line controls" OFF)
if(KOTONOHA_DISTRIBUTION_BUILD)
    set(KOTONOHA_ENABLE_MCP OFF)
endif()

add_subdirectory(lib)

# CMake CONFIG imported targets are directory-scoped. Make them available
# to sibling game ports too. PkgConfig fallback targets are explicitly GLOBAL.
if(KOTONOHA_SDL_CONFIG_SDL3)
    find_package(SDL3 CONFIG REQUIRED)
endif()
if(KOTONOHA_SDL_CONFIG_TTF)
    find_package(SDL3_ttf CONFIG REQUIRED)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/games/KotonohaBuild.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaReport.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/packaging/KotonohaCPack.cmake")
