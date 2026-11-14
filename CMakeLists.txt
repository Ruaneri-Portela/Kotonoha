cmake_minimum_required(VERSION 3.16)

# Kotonoha: select target platforms and game ports in the project root.
# Platform/toolchain setup must run before project() for Android/vcpkg.
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaGames.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaAndroid.cmake")

# vcpkg's target triplet controls *ports*, not the consuming CMake project.
# When using the vcpkg toolchain with an Emscripten triplet, chainload its
# Emscripten wrapper BEFORE project() enables C++ and detects the platform.
# This works with Homebrew emcmake as well as with the EMSDK setup in CI.
if(VCPKG_TARGET_TRIPLET MATCHES "^wasm32-emscripten"
        AND CMAKE_TOOLCHAIN_FILE MATCHES "[/\\\\]scripts[/\\\\]buildsystems[/\\\\]vcpkg\\.cmake$"
        AND NOT VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    get_filename_component(_kotonoha_vcpkg_toolchain_dir
        "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    get_filename_component(_kotonoha_vcpkg_root
        "${_kotonoha_vcpkg_toolchain_dir}/../.." ABSOLUTE)
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE
        "${_kotonoha_vcpkg_root}/scripts/toolchains/emscripten.cmake"
        CACHE FILEPATH "Emscripten wrapper used with the vcpkg toolchain")
    message(STATUS "[Kotonoha] Chainloading Emscripten from vcpkg: ${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}")
endif()

project(Kotonoha VERSION 0.1.0 LANGUAGES CXX)
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

add_subdirectory(lib)

# Each port is configured only when explicitly selected (or BUILD_ALL).
set(_KOTONOHA_ENABLED_GAME_DETAILS "")
foreach(game IN LISTS KOTONOHA_GAMES)
    string(REPLACE "|" ";" fields "${game}")
    list(GET fields 0 game_option)
    list(GET fields 1 game_id)
    list(GET fields 2 game_directory)

    if(KOTONOHA_BUILD_${game_option})
        list(APPEND _KOTONOHA_ENABLED_GAME_DETAILS "${game_id}|${game_directory}")
        add_subdirectory("${game_directory}")
    endif()
endforeach()

include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaReport.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaCPack.cmake")
