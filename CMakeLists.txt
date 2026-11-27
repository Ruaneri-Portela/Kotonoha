cmake_minimum_required(VERSION 3.16)

# Kotonoha: select target platforms and game ports in the project root.
# Platform/toolchain setup must run before project() for Android/vcpkg.
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/games/KotonohaGames.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaAndroid.cmake")
# Declares KOTONOHA_DEPS_PROVIDER, KOTONOHA_SDL_PROVIDER and
# KOTONOHA_IMGUI_PROVIDER before project().
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/dependencies/KotonohaDepsProvider.cmake")

# Windows targets using vcpkg default to the MinGW dynamic triplets. An
# explicit VCPKG_TARGET_TRIPLET (preset or -D=...) always wins. SYSTEM and
# SUBMODULES builds (MSYS2/UCRT, apt, Homebrew) never get a vcpkg triplet.
if(NOT VCPKG_TARGET_TRIPLET AND
   (KOTONOHA_DEPS_PROVIDER STREQUAL "VCPKG" OR
    CMAKE_TOOLCHAIN_FILE MATCHES "[\\/]vcpkg\\.cmake$") AND
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

# vcpkg builds target the portable macOS 15 baseline from CMakePresets.
# SYSTEM-provider builds (Homebrew/MacPorts) link dylibs installed by the host,
# so they must target the host's own macOS version to avoid linker warnings
# about dependencies built for a newer OS.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND
   KOTONOHA_DEPS_PROVIDER STREQUAL "SYSTEM" AND
   NOT CMAKE_SYSTEM_NAME MATCHES "iOS")
    execute_process(
        COMMAND sw_vers -productVersion
        OUTPUT_VARIABLE KOTONOHA_HOST_MACOS_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "^[0-9]+" KOTONOHA_HOST_MACOS_MAJOR
           "${KOTONOHA_HOST_MACOS_VERSION}")
    if(KOTONOHA_HOST_MACOS_MAJOR)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${KOTONOHA_HOST_MACOS_MAJOR}"
            CACHE STRING "macOS deployment target" FORCE)
        message(STATUS "[Kotonoha] SYSTEM provider: macOS deployment target "
                       "${CMAKE_OSX_DEPLOYMENT_TARGET} (host OS)")
    endif()
endif()

project(Kotonoha VERSION 0.1.0 LANGUAGES CXX)

# Windows app targets embed the shared generated K icon as a native WinRes.
# Enable RC in the root directory (not in lib/) for all game subdirectories.
if(WIN32)
    enable_language(RC)
endif()

# Resolve the native dependency provider now that vcpkg (if any) is active.
kotonoha_resolve_deps_provider()

if(MSVC)
    add_compile_options(/utf-8)
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

# Upstream: distribution builds disable MCP without changing the cached DEV preference.
if(KOTONOHA_DISTRIBUTION_BUILD)
    set(KOTONOHA_ENABLE_MCP OFF)
endif()

add_subdirectory(lib)

# CMake CONFIG imported targets are directory-scoped. Make them available
# to sibling game ports too. PkgConfig fallback targets are explicitly GLOBAL.
if(KOTONOHA_DEPS_PROVIDER_RESOLVED STREQUAL "VCPKG" AND
   (KOTONOHA_SDL_CONFIG_SDL3 OR KOTONOHA_SDL_CONFIG_TTF))
    # Imported package targets are directory-scoped. Re-discover the SAME
    # vcpkg triplet selected in lib/, never a system installation.
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

# Dear ImGui CONFIG targets are directory-scoped as well. Re-discover the
# selected vcpkg package in the root scope when ImGui comes from vcpkg so
# sibling game ports can resolve the shared `imgui` wrapper's link interface.
if(KOTONOHA_IMGUI_PROVIDER_RESOLVED STREQUAL "VCPKG" AND
   KOTONOHA_IMGUI_CONFIG AND NOT TARGET imgui::imgui)
    set(_kotonoha_root_imgui_prefix
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    find_package(imgui CONFIG REQUIRED
        PATHS "${_kotonoha_root_imgui_prefix}" NO_DEFAULT_PATH)
    unset(_kotonoha_root_imgui_prefix)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/games/KotonohaBuild.cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/KotonohaReport.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/buildsystems/cmake/packaging/KotonohaCPack.cmake")
