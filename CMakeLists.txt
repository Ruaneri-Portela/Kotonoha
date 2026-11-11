cmake_minimum_required(VERSION 3.16)

# ============================================================
# Kotonoha Engine - Build configuration
# ============================================================

function(kotonoha_log label value)
    if("${value}" STREQUAL "")
        set(value "(not set)")
    endif()
    message(STATUS "[Kotonoha] ${label}: ${value}")
endfunction()

function(kotonoha_section title)
    message(STATUS "")
    message(STATUS "============================================================")
    message(STATUS "  Kotonoha | ${title}")
    message(STATUS "============================================================")
endfunction()

# Format: OPTION|ANDROID_ID|SOURCE_DIRECTORY
set(KOTONOHA_GAMES
    "EXAMPLE|example|games/example"
    "AMAGAMI|amagami|games/amagami"
    "ZERO|zero-no-tsukaima|games/zero-no-tsukaima"
    "SNOW|snow-radish-vacation|games/snow-radish-vacation"
    "SUMMER|summer-radish-vacation|games/summer-radish-vacation"
    "DDLC|doki-doki-literature-club|games/doki-doki-literature-club"
    "SCHOOL_DAYS|school-days|games/school-days"
    "REALLIVE|reallive|games/reallive"
    "KIRIKIRI|kirikiri|games/kirikiri"
    "LUCA_SYSTEM|luca-system|games/luca-system"
)

# ------------------------------------------------------------
# Android and vcpkg setup (before project())
# ------------------------------------------------------------

if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    if(NOT ANDROID_ABI AND CMAKE_ANDROID_ARCH_ABI)
        set(ANDROID_ABI "${CMAKE_ANDROID_ARCH_ABI}")
    endif()

    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(_VCPKG_ANDROID_TRIPLET "arm64-android")
    elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
        if(ANDROID_ARM_NEON)
            set(_VCPKG_ANDROID_TRIPLET "arm-neon-android")
        else()
            set(_VCPKG_ANDROID_TRIPLET "arm-android")
        endif()
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(_VCPKG_ANDROID_TRIPLET "x64-android")
    elseif(ANDROID_ABI STREQUAL "x86")
        set(_VCPKG_ANDROID_TRIPLET "x86-android")
    else()
        message(FATAL_ERROR "[Kotonoha] Unsupported Android ABI: ${ANDROID_ABI}")
    endif()

    set(VCPKG_TARGET_TRIPLET "${_VCPKG_ANDROID_TRIPLET}"
        CACHE STRING "Android vcpkg triplet" FORCE)

    if(KOTONOHA_VCPKG_BASE_DIR)
        file(TO_CMAKE_PATH "${KOTONOHA_VCPKG_BASE_DIR}" _vcpkg_base)
        set(VCPKG_INSTALLED_DIR "${_vcpkg_base}/${ANDROID_ABI}"
            CACHE PATH "Per-ABI vcpkg installation" FORCE)
    endif()

    if(CMAKE_ANDROID_NDK)
        set(ENV{ANDROID_NDK_HOME} "${CMAKE_ANDROID_NDK}")
    elseif(ANDROID_NDK)
        set(ENV{ANDROID_NDK_HOME} "${ANDROID_NDK}")
    endif()

    if(NOT DEFINED KOTONOHA_BUILD_GAME)
        set(KOTONOHA_BUILD_GAME "example" CACHE STRING "Game built by this Android APK")
    endif()

    # vcpkg manifest features must be set before project().
    if(KOTONOHA_BUILD_GAME STREQUAL "doki-doki-literature-club")
        list(APPEND VCPKG_MANIFEST_FEATURES android-python)
        list(REMOVE_DUPLICATES VCPKG_MANIFEST_FEATURES)
    endif()
endif()

# ------------------------------------------------------------
# Project and build options
# ------------------------------------------------------------

project(Kotonoha VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(buildsystems/cmake/MSVC.cmake)

option(KOTONOHA_VERBOSE_BUILD "Print full compiler and linker commands" OFF)
option(KOTONOHA_BUILD_MACOS_BUNDLES "Build macOS application bundles" ON)
option(KOTONOHA_BUILD_ALL "Build every game port" OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE "${KOTONOHA_VERBOSE_BUILD}")

if(ANDROID)
    set(BUILD_TESTING OFF CACHE BOOL "Disable tests on Android" FORCE)
endif()
include(CTest)

if(ANDROID)
    # Align Kotonoha-linked ELF shared objects for 16 KiB page devices.
    add_link_options("-Wl,-z,max-page-size=16384")
    set(KOTONOHA_BUILD_MACOS_BUNDLES OFF
        CACHE BOOL "Build macOS application bundles" FORCE)
endif()

foreach(game IN LISTS KOTONOHA_GAMES)
    string(REPLACE "|" ";" fields "${game}")
    list(GET fields 0 game_option)
    # Por padrao compila apenas a lib; cada KOTONOHA_BUILD_* liga um jogo.
    option(KOTONOHA_BUILD_${game_option} "Build ${game_option} game" OFF)
endforeach()

# KOTONOHA_BUILD_ALL liga todos os jogos (ignorados no Android).
if(KOTONOHA_BUILD_ALL AND NOT ANDROID)
    foreach(game IN LISTS KOTONOHA_GAMES)
        string(REPLACE "|" ";" fields "${game}")
        list(GET fields 0 game_option)
        set(KOTONOHA_BUILD_${game_option} ON)
    endforeach()
endif()

# ------------------------------------------------------------
# Android: enable exactly one game via KOTONOHA_BUILD_GAME
# ------------------------------------------------------------

if(ANDROID)
    set(_KOTONOHA_GAME_FOUND FALSE)
    foreach(game IN LISTS KOTONOHA_GAMES)
        string(REPLACE "|" ";" fields "${game}")
        list(GET fields 0 game_option)
        list(GET fields 1 game_id)

        if(KOTONOHA_BUILD_GAME STREQUAL game_id)
            set(KOTONOHA_BUILD_${game_option} ON CACHE BOOL "" FORCE)
            set(_KOTONOHA_GAME_FOUND TRUE)
        else()
            set(KOTONOHA_BUILD_${game_option} OFF CACHE BOOL "" FORCE)
        endif()
    endforeach()

    if(NOT _KOTONOHA_GAME_FOUND)
        message(FATAL_ERROR
            "[Kotonoha] Unknown Android game: ${KOTONOHA_BUILD_GAME}")
    endif()
endif()

# ------------------------------------------------------------
# Engine and enabled games
# ------------------------------------------------------------

add_subdirectory(lib)

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

# ------------------------------------------------------------
# One consolidated report at the end of configuration
# ------------------------------------------------------------

kotonoha_section("Configuration Summary")
kotonoha_log("Project" "${PROJECT_NAME} ${PROJECT_VERSION}")

message(STATUS "  [Platform]")
kotonoha_log("Host OS" "${CMAKE_HOST_SYSTEM_NAME}")
kotonoha_log("Host architecture" "${CMAKE_HOST_SYSTEM_PROCESSOR}")
kotonoha_log("Target OS" "${CMAKE_SYSTEM_NAME}")
kotonoha_log("Target architecture" "${CMAKE_SYSTEM_PROCESSOR}")

if(ANDROID)
    kotonoha_log("Android game" "${KOTONOHA_BUILD_GAME}")
    kotonoha_log("Android ABI" "${ANDROID_ABI}")
    kotonoha_log("Android API" "${ANDROID_PLATFORM}")
    kotonoha_log("Android NDK" "$ENV{ANDROID_NDK_HOME}")
    kotonoha_log("Android STL" "${CMAKE_ANDROID_STL_TYPE}")
    kotonoha_log("ARM NEON" "${ANDROID_ARM_NEON}")
    kotonoha_log("ELF max page size" "16384 bytes")
endif()

message(STATUS "")
message(STATUS "  [Compiler]")
kotonoha_log("C++ compiler" "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
kotonoha_log("C++ executable" "${CMAKE_CXX_COMPILER}")
kotonoha_log("C++ standard (global)" "${CMAKE_CXX_STANDARD}")
kotonoha_log("C++ flags" "${CMAKE_CXX_FLAGS}")

message(STATUS "")
message(STATUS "  [Build]")
kotonoha_log("Generator" "${CMAKE_GENERATOR}")
kotonoha_log("Build type" "${CMAKE_BUILD_TYPE}")
kotonoha_log("Available configurations" "${CMAKE_CONFIGURATION_TYPES}")
kotonoha_log("Verbose compiler commands" "${KOTONOHA_VERBOSE_BUILD}")
kotonoha_log("Compile commands database" "${CMAKE_EXPORT_COMPILE_COMMANDS}")
kotonoha_log("Tests" "${BUILD_TESTING}")
kotonoha_log("macOS bundles" "${KOTONOHA_BUILD_MACOS_BUNDLES}")
kotonoha_log("Toolchain" "${CMAKE_TOOLCHAIN_FILE}")
kotonoha_log("Source directory" "${CMAKE_SOURCE_DIR}")
kotonoha_log("Build directory" "${CMAKE_BINARY_DIR}")

# Show dependency details only when a vcpkg toolchain is in use.
set(_KOTONOHA_USES_VCPKG FALSE)
if(DEFINED VCPKG_TOOLCHAIN OR DEFINED VCPKG_ROOT)
    set(_KOTONOHA_USES_VCPKG TRUE)
elseif(CMAKE_TOOLCHAIN_FILE MATCHES "(^|[/\\\\])vcpkg[.]cmake$")
    set(_KOTONOHA_USES_VCPKG TRUE)
endif()

if(_KOTONOHA_USES_VCPKG)
    message(STATUS "")
    message(STATUS "  [Dependencies]")
    kotonoha_log("vcpkg target triplet" "${VCPKG_TARGET_TRIPLET}")
    kotonoha_log("vcpkg host triplet" "${VCPKG_HOST_TRIPLET}")
    kotonoha_log("vcpkg installed directory" "${VCPKG_INSTALLED_DIR}")
    kotonoha_log("vcpkg manifest features" "${VCPKG_MANIFEST_FEATURES}")
endif()

message(STATUS "")
message(STATUS "  [Enabled Games]")
foreach(game IN LISTS _KOTONOHA_ENABLED_GAME_DETAILS)
    string(REPLACE "|" ";" fields "${game}")
    list(GET fields 0 game_id)
    list(GET fields 1 game_directory)
    kotonoha_log("${game_id}" "${CMAKE_SOURCE_DIR}/${game_directory}")
endforeach()

message(STATUS "")
message(STATUS "[Kotonoha] Configuration completed; build files will now be generated.")
message(STATUS "")
