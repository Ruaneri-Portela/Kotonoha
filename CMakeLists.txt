cmake_minimum_required(VERSION 3.16)

# Kotonoha: select target platforms and game ports in the project root.
# Platform/toolchain setup must run before project() for Android/vcpkg.
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/games/KotonohaGames.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaAndroid.cmake")

# Windows targets default to the MinGW dynamic vcpkg triplets. An explicit
# VCPKG_TARGET_TRIPLET (preset or -D=...) always wins. This runs before
# project() so the vcpkg toolchain sees the selected triplet.
if(NOT VCPKG_TARGET_TRIPLET AND
   (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" OR
    CMAKE_SYSTEM_NAME STREQUAL "Windows" OR MINGW))
    set(_kotonoha_windows_arch "${CMAKE_SYSTEM_PROCESSOR}")
    if(NOT _kotonoha_windows_arch)
        set(_kotonoha_windows_arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
    string(TOLOWER "${_kotonoha_windows_arch}" _kotonoha_windows_arch)
    if(_kotonoha_windows_arch MATCHES "^(i[3-6]86|x86|i686)$")
        set(_kotonoha_windows_triplet "x86-mingw-dynamic")
    elseif(_kotonoha_windows_arch MATCHES "^(aarch64|arm64)$")
        set(_kotonoha_windows_triplet "arm64-mingw-dynamic")
    else()
        set(_kotonoha_windows_triplet "x64-mingw-dynamic")
    endif()
    set(VCPKG_TARGET_TRIPLET "${_kotonoha_windows_triplet}" CACHE STRING
        "vcpkg target triplet (Windows defaults to MinGW dynamic)")
    unset(_kotonoha_windows_triplet)
    unset(_kotonoha_windows_arch)
endif()

project(Kotonoha VERSION 0.1.0 LANGUAGES CXX)

# Windows app targets embed the shared generated K icon as a native WinRes.
# Enable RC in the root directory (not in lib/) for all game subdirectories.
if(WIN32)
    enable_language(RC)
endif()

# MSVC cannot link or compile packages built against the MinGW/UCRT ABI.
# The engine's native media dependencies require vcpkg; only SDL/ImGui may
# build from their pinned submodules. Enforce this before any find_package().
if(MSVC)
    add_compile_options(/utf-8)
    if(NOT VCPKG_INSTALLED_DIR OR NOT VCPKG_TARGET_TRIPLET)
        message(FATAL_ERROR
            "[Kotonoha] MSVC requires the vcpkg toolchain for native "
            "FFmpeg, libass and json-c dependencies. Pass "
            "-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake "
            "on the FIRST configuration. SDL3 can use vcpkg or submodules; "
            "MSYS2/MinGW system packages are never accepted.")
    endif()
    if(VCPKG_TARGET_TRIPLET MATCHES "[Mm][Ii][Nn][Gg][Ww]")
        message(FATAL_ERROR
            "[Kotonoha] The MSVC compiler cannot use the vcpkg triplet "
            "${VCPKG_TARGET_TRIPLET}. Select x64-windows, x86-windows, "
            "arm64-windows or their MSVC static variants.")
    endif()
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
if(MSVC AND (KOTONOHA_SDL_CONFIG_SDL3 OR KOTONOHA_SDL_CONFIG_TTF))
    # Imported package targets are directory-scoped. Re-discover the SAME
    # vcpkg triplet selected in lib/, never a MinGW/system installation.
    set(_kotonoha_root_vcpkg_prefix
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(KOTONOHA_SDL_CONFIG_SDL3)
        find_package(SDL3 CONFIG REQUIRED
            PATHS "${_kotonoha_root_vcpkg_prefix}" NO_DEFAULT_PATH)
    endif()
    if(KOTONOHA_SDL_CONFIG_TTF)
        find_package(SDL3_ttf CONFIG REQUIRED
            PATHS "${_kotonoha_root_vcpkg_prefix}" NO_DEFAULT_PATH)
    endif()
else()
    if(KOTONOHA_SDL_CONFIG_SDL3)
        find_package(SDL3 CONFIG REQUIRED)
    endif()
    if(KOTONOHA_SDL_CONFIG_TTF)
        find_package(SDL3_ttf CONFIG REQUIRED)
    endif()
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/games/KotonohaBuild.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaReport.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/packaging/KotonohaCPack.cmake")
